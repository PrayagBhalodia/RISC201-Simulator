// pipeline4.cpp
// Stage occupancy, hazards and cycle counting for the 4-stage RISC201
// pipeline. Merging the ALU and the data memory into one EX/MEM stage is what
// shapes the hazards here: every result is ready at the end of the same stage,
// so forwarding covers all of them and only a branch - which compares one
// stage earlier, in DE/OF - can be left waiting for an operand.

#include "pipeline4.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace risc201 {

namespace {

// Dependences are tracked over the 16 GPRs plus SP and LR, which live outside
// the register file but are still read and written by PUSH/POP and CALL/RET.
constexpr int kResSP = 16;
constexpr int kResLR = 17;
constexpr int kNumRes = 18;

// Execute-stage latencies of the FPU (Section 2.11). The MDU's come from
// mdu.h, where the algorithms that cost them live; both units have one
// algorithm per operation, so neither figure is a free parameter.
constexpr int kFpAddLatency = 4;
constexpr int kFpMulLatency = 5;
constexpr int kFpDivLatency = 14;
constexpr int kFpCmpLatency = 1;
constexpr int kFpCvtLatency = 2;

const char* const kNames[] = {"IF", "DE/OF", "EX/MEM", "WB"};

// POP's destination: LR when the S-format L bit is set, else the rs1 GPR.
bool popWrites(const DecodedInstruction& d, int r) {
    return d.useLR ? r == kResLR : r == d.rs1;
}

// Does `d` write resource `r`? Writes to R0 are discarded, so they never
// create a dependence.
bool writesRes(const DecodedInstruction& d, int r) {
    if (r == 0) return false;
    switch (static_cast<Op>(d.opcode)) {
        case Op::NOP: case Op::ST: case Op::RET:
        case Op::B:   case Op::BEQ: case Op::BGT:
            return false;
        case Op::PUSH: return r == kResSP;
        case Op::POP:  return r == kResSP || popWrites(d, r);
        case Op::CALL: return r == kResLR;
        default:       return r == d.rd; // every remaining opcode writes rd
    }
}

// Does `d` read resource `r`? Unused register fields decode as 0, which the
// R0 guard above already filters out, so the catch-all is safe for I-format.
bool readsRes(const DecodedInstruction& d, int r) {
    if (r == 0) return false;
    switch (static_cast<Op>(d.opcode)) {
        case Op::NOP: case Op::B:
        case Op::CALL: case Op::MOVHI:
            return false;
        case Op::RET:  return r == kResLR;
        case Op::PUSH: return r == kResSP || (d.useLR ? r == kResLR : r == d.rs1);
        case Op::POP:  return r == kResSP;
        case Op::ST:   return r == d.rs1 || r == d.rd; // rd field holds the stored value
        default:       return r == d.rs1 || r == d.rs2;
    }
}

bool isControl(Op op) {
    return op == Op::B || op == Op::CALL || op == Op::RET ||
           op == Op::BEQ || op == Op::BGT;
}

} // namespace

Pipeline4::Pipeline4(Simulator& sim) : sim_(sim) {
    fetchPC_ = sim_.state().PC;
}

void Pipeline4::reset() {
    slots_.fill(Slot{});
    fetchPC_ = sim_.state().PC;
    fetchStopped_ = false;
    cycles_ = retired_ = mduStalls_ = fpuStalls_ = branchStalls_ = squashed_ = 0;
}

bool Pipeline4::drained() const {
    for (const Slot& s : slots_)
        if (s.valid) return false;
    return true;
}

// Section 6.5, 4-stage: the control store is read in DE/OF and PCSel/CondSel
// are used there, because the target adder and the comparator sit in that
// stage. Everything else latches forward into EX/MEM and WB.
FieldSet Pipeline4::microFields(int stage) {
    switch (stage) {
        case kIF: return 0;   // the fetch itself drives no control line
        case kDEOF: return fieldBit(Field::PCSel) | fieldBit(Field::CondSel);
        case kEXMEM:
            return fieldBit(Field::ALUSrcA) | fieldBit(Field::ALUSrcB) |
                   fieldBit(Field::ALUOp) | fieldBit(Field::MDUEn) |
                   fieldBit(Field::FpuEn) |
                   fieldBit(Field::MemRead) | fieldBit(Field::MemWrite) |
                   fieldBit(Field::MemAddrSrc);
        case kWB:
            return fieldBit(Field::RegWrite) | fieldBit(Field::RegDst) |
                   fieldBit(Field::WBSrc) | fieldBit(Field::SPWrite);
        default: return 0;
    }
}

std::string Pipeline4::microinstruction(uint8_t opcode, int stage) {
    return fieldsText(controlWord(opcode), microFields(stage));
}

uint8_t Pipeline4::opcodeAt(int stage) const {
    return slots_[static_cast<size_t>(stage)].instr.opcode;
}

std::string Pipeline4::microPC(int stage) const {
    const Slot& s = slots_[static_cast<size_t>(stage)];
    if (!s.valid) return "";
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(s.instr.opcode) << ':' << std::dec << stage;
    return oss.str();
}

const char* Pipeline4::stageName(int i) {
    return kNames[static_cast<size_t>(i)];
}

int Pipeline4::latencyOf(const DecodedInstruction& d) const {
    const Op op = static_cast<Op>(d.opcode);
    // The control word already says which unit runs this, so the pipeline asks
    // it rather than keeping a second list of opcodes per unit. One divider
    // produces both the quotient and the remainder, so DIV and MOD cost alike.
    const Microword& cw = controlWord(d.opcode);
    if (cw.mduEn) {
        // Restoring division's cost depends on its operands, so the unit is
        // asked rather than consulted as a constant. Every older instruction
        // has already committed when a new one enters execute, so the register
        // file holds exactly what this one is about to read.
        const Simulator::Operands ops = sim_.operandsFor(d);
        return sim_.mdu().cyclesFor(cw.aluOp, ops.a, ops.b);
    }
    if (!cw.fpuEn) return 1;
    if (op == Op::FADD || op == Op::FSUB) return kFpAddLatency;
    if (op == Op::FMUL) return kFpMulLatency;
    if (op == Op::FDIV) return kFpDivLatency;
    if (op == Op::FEQ || op == Op::FGT) return kFpCmpLatency;
    return kFpCvtLatency;   // ITOF and FTOI
}

// A branch compares its operands in DE/OF, one stage before results appear, so
// an older instruction still sitting in EX/MEM owes it a value and the branch
// waits a cycle. Anything already in WB has produced its result and forwards.
bool Pipeline4::branchOperandPending(const DecodedInstruction& branch) const {
    const Slot& producer = slots_[kEXMEM];
    if (!producer.valid) return false;
    for (int r = 1; r < kNumRes; ++r)
        if (readsRes(branch, r) && writesRes(producer.instr, r)) return true;
    return false;
}

Pipeline4::Hold Pipeline4::holdReason(int stage) const {
    const Slot& s = slots_[static_cast<size_t>(stage)];
    if (!s.valid) return Hold::None;

    if (stage == kEXMEM && s.exLeft > 0)
        return controlWord(s.instr.opcode).fpuEn ? Hold::Fpu : Hold::Mdu;

    if (stage == kDEOF && isControl(static_cast<Op>(s.instr.opcode)) &&
        branchOperandPending(s.instr))
        return Hold::BranchOperand;

    return Hold::None;
}

void Pipeline4::squashYounger(int stage) {
    for (int i = 0; i < stage; ++i) {
        Slot& s = slots_[static_cast<size_t>(i)];
        if (s.valid) { s.valid = false; ++squashed_; }
    }
}

// Every redirect happens in DE/OF: the conditional branches have their
// operands by then and B, CALL and RET need no register read at all, so this
// pipeline resolves control in a single stage. Only the redirect happens
// early, never a register write.
void Pipeline4::resolveControl(bool moving) {
    const Slot& s = slots_[kDEOF];
    if (!s.valid || !moving) return;
    const Op op = static_cast<Op>(s.instr.opcode);

    if (s.instr.format == Format::UNKNOWN) {
        sim_.raiseIllegalOpcode(s.instr.opcode, s.pc);
        squashYounger(kDEOF);
        fetchStopped_ = true;
    } else if (isControl(op)) {
        const uint32_t target = branchTarget(sim_.state(), s.instr, s.pc);
        if (target != s.pc + 4) {   // static predict-not-taken: only a taken
            fetchPC_ = target;      // branch costs bubbles
            // Running off the end only ends the program if nothing brings the
            // PC back: a routine placed last returns to code still ahead of it.
            fetchStopped_ = false;
            squashYounger(kDEOF);
        }
    }
}

void Pipeline4::fetch() {
    if (slots_[kIF].valid || fetchStopped_ || sim_.state().halted) return;
    // Past the last instruction the program is over; the data that the image
    // holds after the code is never fetched (Section 2.9).
    if (fetchPC_ >= sim_.codeEnd()) {
        fetchStopped_ = true;
        return;
    }
    try {
        DecodedInstruction d = decode(sim_.memory().readWord(fetchPC_));
        slots_[kIF] = Slot{true, fetchPC_, d, 0, 0};
        fetchPC_ += 4;
    } catch (const MemoryAccessError&) {
        fetchStopped_ = true;
    }
}

bool Pipeline4::tick() {
    if (fetchStopped_ && drained()) return false;
    ++cycles_;

    // The execute stage burns one MDU cycle per tick.
    if (slots_[kEXMEM].valid && slots_[kEXMEM].exLeft > 0) --slots_[kEXMEM].exLeft;

    // Root-cause holds first, then propagate backwards: a stage can only hand
    // its instruction on if the stage ahead of it is moving too.
    std::array<bool, kStageCount> moves{};
    for (int i = kStageCount - 1; i >= 0; --i) {
        const size_t k = static_cast<size_t>(i);
        if (!slots_[k].valid) continue;
        const Hold h = holdReason(i);
        switch (h) {
            case Hold::Mdu:           ++mduStalls_; break;
            case Hold::Fpu:           ++fpuStalls_; break;
            case Hold::BranchOperand: ++branchStalls_; break;
            case Hold::None:          break;
        }
        const size_t ahead = static_cast<size_t>(i + 1);
        const bool aheadBusy = (i + 1 < kStageCount) && slots_[ahead].valid && !moves[ahead];
        moves[k] = (h == Hold::None) && !aheadBusy;
    }

    // The instruction leaving EX/MEM commits. Only one does so per cycle and
    // they leave in order, which is what keeps register writes in order.
    if (slots_[kEXMEM].valid && moves[kEXMEM])
        sim_.executeAt(slots_[kEXMEM].instr, slots_[kEXMEM].pc);

    // An exception squashes the faulting instruction and everything younger,
    // while older instructions complete (Section 6.1). This runs before any
    // redirect, so a squashed control instruction never gets to act.
    if (sim_.state().halted) {
        squashYounger(kEXMEM);
        fetchStopped_ = true;
    }

    resolveControl(moves[kDEOF]);

    // Shift forward, back to front so a vacated stage is free when reached.
    if (moves[kWB]) {
        slots_[kWB].valid = false;
        ++retired_;
    }
    for (int i = kStageCount - 1; i >= 1; --i) {
        const size_t to = static_cast<size_t>(i), from = static_cast<size_t>(i - 1);
        if (!moves[from]) continue;
        slots_[to] = slots_[from];
        slots_[from].valid = false;
        if (i == kEXMEM) {
            slots_[to].exTotal = latencyOf(slots_[to].instr);
            slots_[to].exLeft = slots_[to].exTotal;
        }
    }

    fetch();
    sim_.state().PC = fetchPC_; // keep the CLI's view of the PC meaningful
    return true;
}

std::string Pipeline4::stageView(int i) const {
    const Slot& s = slots_[static_cast<size_t>(i)];
    if (!s.valid) return "";

    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(s.instr.opcode) << ' ' << s.instr.mnemonic;
    if (i == kEXMEM && s.exTotal > 1) {
        const int done = std::min(s.exTotal, s.exTotal - s.exLeft + 1);
        const char* unit = controlWord(s.instr.opcode).fpuEn ? "FPU" : "MDU";
        oss << " [" << unit << ' ' << std::dec << done << '/' << s.exTotal << ']';
    }
    return oss.str();
}

} // namespace risc201

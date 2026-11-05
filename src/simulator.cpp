#include "simulator.h"
#include "alu.h"
#include "control.h"
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace risc201 {

namespace {

std::string hex(uint32_t v, int digits) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase
        << std::setw(digits) << std::setfill('0') << v;
    return oss.str();
}

} // namespace

// The target adder and comparator of the decode stage (Sections 3.1, 4.1),
// driven by PCSel and CondSel. Returns pc + 4 for anything that does not
// redirect, including a conditional branch whose condition is false.
uint32_t branchTarget(const State& s, const DecodedInstruction& d, uint32_t pc) {
    const Microword& cw = controlWord(d.opcode);
    const uint32_t next = pc + 4;

    if (cw.pcSel == PcSel::LINK) return s.LR;
    if (cw.pcSel != PcSel::TARGET) return next;

    // J-format carries a 26-bit offset, B-format an 18-bit one; both are
    // relative to the next instruction.
    const uint32_t offset = (d.format == Format::J)
                                ? static_cast<uint32_t>(d.jOffset)
                                : static_cast<uint32_t>(d.imm);
    const uint32_t a = s.readGPR(d.rs1);
    const uint32_t b = s.readGPR(d.rs2);

    bool take = false;
    switch (cw.condSel) {
        case CondSel::ALWAYS: take = true; break;
        case CondSel::EQ:     take = (a == b); break;
        case CondSel::GT:
            take = static_cast<int32_t>(a) > static_cast<int32_t>(b);
            break;
    }
    return take ? next + offset : next;
}

Simulator::Simulator(size_t memSizeBytes) : memory_(memSizeBytes) {
    state_.PC = 0;
    state_.SP = static_cast<uint32_t>(memSizeBytes); // stack grows down from top
    codeEnd_ = static_cast<uint32_t>(memSizeBytes);  // until a program is loaded
}

void Simulator::loadProgram(const std::vector<uint32_t>& words) {
    uint32_t addr = 0;
    for (uint32_t w : words) {
        memory_.writeWord(addr, w);
        addr += 4;
    }
    codeEnd_ = addr;
    stackLimit_ = addr;
    frameMarks_.clear();
}

// Section 2.12. The stack is the one region the hardware protects: PUSH may
// not grow it into the program image, POP may not take from an empty stack,
// and a plain ST may not reach inside a live frame.
void Simulator::checkStackBounds(const Microword& cw, uint32_t address, uint32_t pc) const {
    if (cw.aluSrcASP) {                                 // PUSH or POP
        if (cw.memWrite && address < stackLimit_) {
            throw MemoryAccessError("stack overflow at PC=" + hex(pc, 8) +
                                    ": PUSH to " + hex(address, 8) +
                                    " is below the program image end " + hex(stackLimit_, 8));
        }
        if (cw.memRead && address >= stackBase()) {
            throw MemoryAccessError("stack underflow at PC=" + hex(pc, 8) +
                                    ": POP with SP=" + hex(address, 8) +
                                    " at the stack base " + hex(stackBase(), 8));
        }
    } else if (cw.memWrite && address >= state_.SP) {
        throw MemoryAccessError("stack corruption at PC=" + hex(pc, 8) +
                                ": ST to " + hex(address, 8) +
                                " is inside the live stack (SP=" + hex(state_.SP, 8) + ")");
    }
}

void Simulator::raiseIllegalOpcode(uint8_t opcode, uint32_t pc) {
    lastError_ = "illegal opcode " + hex(opcode, 2) + " at PC=" + hex(pc, 8);
    state_.halted = true;
}

void Simulator::reset() {
    state_ = State{};
    state_.SP = static_cast<uint32_t>(memory_.size());
    cycles_ = 0;
    mduAdderOps_ = 0;
    frameMarks_.clear();
    lastError_.clear();
}

void Simulator::executeAt(const DecodedInstruction& instr, uint32_t pc) {
    state_.PC = pc + 4;
    try {
        execute(instr, pc);
    } catch (const MemoryAccessError& e) {
        lastError_ = e.what();
        state_.halted = true;
    }
}

void Simulator::raiseDivideByZero(uint32_t pc) {
    // Section 2.7: the faulting instruction writes no destination.
    lastError_ = "divide-by-zero exception at PC=" + hex(pc, 8);
    state_.halted = true;
}

bool Simulator::step() {
    if (state_.halted) return false;
    // Running off the end of the code is a normal end of program, so no error
    // is recorded and the data that follows the code is never fetched.
    if (state_.PC >= codeEnd_) {
        state_.halted = true;
        return false;
    }

    try {
        // --- Fetch ---
        uint32_t ir = memory_.readWord(state_.PC);
        uint32_t fetchedPC = state_.PC;
        state_.PC += 4;

        // --- Decode ---
        DecodedInstruction instr = decode(ir);
        if (instr.format == Format::UNKNOWN) {
            raiseIllegalOpcode(instr.opcode, fetchedPC);
            ++cycles_;
            return false;
        }

        // --- Execute ---
        execute(instr, fetchedPC);

    } catch (const MemoryAccessError& e) {
        lastError_ = e.what();
        state_.halted = true;
        ++cycles_;
        return false;
    }

    ++cycles_;
    return true;
}

Simulator::Operands Simulator::operandsFor(const DecodedInstruction& instr) const {
    const Microword& cw = controlWord(instr.opcode);
    Operands out;
    out.a = cw.aluSrcASP ? state_.SP : state_.readGPR(instr.rs1);
    switch (cw.aluSrcB) {
        case SrcB::RS2:  out.b = state_.readGPR(instr.rs2); break;
        case SrcB::IMM:  out.b = static_cast<uint32_t>(instr.imm); break;
        case SrcB::FOUR: out.b = 4; break;   // the SP step for PUSH and POP
    }
    return out;
}

// Single-cycle functional execution, driven entirely by the instruction's
// control word (Section 6). MDU latency is a timing concern and belongs to the
// pipeline models; here the MDU result appears in the same step.
void Simulator::execute(const DecodedInstruction& instr, uint32_t pc) {
    const Microword& cw = controlWord(instr.opcode);
    // The reserved entry drives no control line; decode raises the
    // illegal-opcode exception before execution is ever reached (Section 6.1).
    if (cw.reserved) return;

    State& s = state_;

    // ── Operand select: ALUSrcA, ALUSrcB ──────────────────────────────────
    const Operands ops = operandsFor(instr);
    const uint32_t a = ops.a, b = ops.b;

    // ── Execute: ALUOp, MDUEn, FpuEn ──────────────────────────────────────
    uint32_t result;
    if (cw.fpuEn) {
        const Fpu::Result fp = fpu_.apply(cw.aluOp, a, b);
        result = fp.value;
        // Section 2.10: FPSR is sticky, and an FP exception reports a result
        // instead of stopping the machine, so this only ever accumulates.
        s.fpsr.accumulate(fp.raised);
    } else if (cw.mduEn) {
        // MDUEn sends the operands to the multiply/divide unit beside the ALU
        // instead; the two share this result bus (Sections 2.8, 6.2).
        const Mdu::Result mdu = mdu_.apply(cw.aluOp, a, b);

        // Section 2.7: the faulting instruction writes no destination, so this
        // returns before any stage below.
        if (mdu.divideByZero) {
            raiseDivideByZero(pc);
            return;
        }
        mduAdderOps_ += static_cast<uint64_t>(mdu.adderOps);
        result = mdu.value;
    } else {
        result = alu_.apply(cw.aluOp, a, b);
    }

    // ── Memory: MemRead, MemWrite, MemAddrSrc ─────────────────────────────
    // POP reads the old top of stack, so it addresses with SP directly rather
    // than with the incremented value on the result bus.
    const uint32_t address = cw.memAddrSP ? s.SP : result;
    if (cw.memRead || cw.memWrite) checkStackBounds(cw, address, pc);
    uint32_t loaded = 0;
    if (cw.memRead) loaded = memory_.readWord(address);
    if (cw.memWrite) {
        // Which register field supplies the stored value is an instruction
        // property, not a control line: a stack operation (ALUSrcA = SP) uses
        // the S-format field or LR, a store uses the rd-position field.
        const uint32_t value = cw.aluSrcASP
                                   ? (instr.useLR ? s.LR : s.readGPR(instr.rs1))
                                   : s.readGPR(instr.rd);
        memory_.writeWord(address, value);
        if (cw.aluSrcASP && instr.useLR) frameMarks_.push_back(address);
    }

    // ── Write-back: WBSrc, RegWrite, RegDst ───────────────────────────────
    uint32_t writeBack = result;
    if (cw.wbSrc == WbSrc::MEM) writeBack = loaded;
    if (cw.wbSrc == WbSrc::PC4) writeBack = pc + 4;

    if (cw.regWrite) {
        if (cw.regDstLR) {
            s.LR = writeBack;                      // CALL
        } else if (cw.aluSrcASP) {                 // POP
            if (instr.useLR) s.LR = writeBack;
            else s.writeGPR(instr.rs1, writeBack); // the S-format destination
        } else {
            s.writeGPR(instr.rd, writeBack);
        }
    }

    // SP moves only after the access above, so a memory fault leaves the
    // stack pointer where it was.
    if (cw.spWrite) {
        s.SP = result;
        // A popped frame mark is dead; dropping it here bounds the list.
        while (!frameMarks_.empty() && frameMarks_.back() < s.SP) frameMarks_.pop_back();
    }

    // ── PC: PCSel, CondSel ────────────────────────────────────────────────
    switch (cw.pcSel) {
        case PcSel::NEXT: break;  // the caller already advanced the PC
        case PcSel::TARGET:
        case PcSel::LINK: s.PC = branchTarget(s, instr, pc); break;
    }
}

void Simulator::run(uint64_t maxCycles) {
    uint64_t start = cycles_;
    while (!state_.halted && (cycles_ - start) < maxCycles) {
        if (!step()) break;
    }
}

} // namespace risc201

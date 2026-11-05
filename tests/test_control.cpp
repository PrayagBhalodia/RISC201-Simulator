// test_control.cpp
// Checks the microprogrammed control unit against the ISA specification:
// the worked control words of Section 6.4 and the reserved entries of 6.1.

#include "control.h"
#include "pipeline4/pipeline4.h"
#include "pipeline6/pipeline6.h"
#include "simulator.h"
#include <cassert>
#include <cstdio>

using namespace risc201;

static uint8_t num(Op op) { return static_cast<uint8_t>(op); }

// Section 6.4 works out three instructions field by field and gives the
// resulting 20-bit word, so the spec grades its own implementation here.
static void worked_examples_match_the_spec() {
    assert(controlWord(num(Op::LD)).encode() == 0x220880u);   // LD rd, imm(rs1)
    assert(controlWord(num(Op::CALL)).encode() == 0x300110u); // CALL offset
    assert(controlWord(num(Op::BEQ)).encode() == 0x000014u);  // BEQ rs1, rs2, offset
}

// The six MDU instructions share one pattern; only ALUSrcB and ALUOp differ.
static void mdu_words_match_the_spec() {
    assert(controlWord(num(Op::MUL)).encode()  == 0x202002u);
    assert(controlWord(num(Op::DIV)).encode()  == 0x203002u);
    assert(controlWord(num(Op::MOD)).encode()  == 0x204002u);
    assert(controlWord(num(Op::MULI)).encode() == 0x222002u);
    assert(controlWord(num(Op::DIVI)).encode() == 0x223002u);
    assert(controlWord(num(Op::MODI)).encode() == 0x224002u);
}

// The nine FP instructions share one pattern too: an ordinary register write
// with FpuEn = 1, so only ALUOp tells them apart (Section 6.3). The two
// conversions and the three comparisons look the same because they are - what
// the operands mean is the FPU's business, not the datapath's.
static void fpu_words_match_the_spec() {
    assert(controlWord(num(Op::FADD)).encode() == 0x210001u);
    assert(controlWord(num(Op::FSUB)).encode() == 0x211001u);
    assert(controlWord(num(Op::FMUL)).encode() == 0x212001u);
    assert(controlWord(num(Op::FDIV)).encode() == 0x213001u);
    assert(controlWord(num(Op::FEQ)).encode()  == 0x214001u);
    assert(controlWord(num(Op::FGT)).encode()  == 0x215001u);
    assert(controlWord(num(Op::ITOF)).encode() == 0x216001u);
    assert(controlWord(num(Op::FTOI)).encode() == 0x217001u);

    // Every control word still fits the 22-bit store (Section 6.2).
    for (unsigned op = 0; op <= 0x3Fu; ++op)
        assert(controlWord(static_cast<uint8_t>(op)).encode() < (1u << 22));
}

// Section 6.5 assigns every control line to the stage that consumes it, so the
// per-stage field sets have to partition the microword: a line in no stage
// would never reach the datapath, and one in two stages would be driven twice.
template <class P>
static void stage_fields_partition_the_microword() {
    FieldSet seen = 0;
    for (int stage = 0; stage < P::kStageCount; ++stage) {
        const FieldSet fields = P::microFields(stage);
        assert((seen & fields) == 0);
        seen |= fields;
    }
    assert(seen == kAllFields);
}

// A stage's microinstruction drives its own lines and nothing else, which is
// what makes an opcode's routine a decomposition of its control word.
static void microinstruction_holds_only_its_own_stage_lines() {
    const Microword& ld = controlWord(num(Op::LD));

    const Microword ex = project(ld, Pipeline6::microFields(3));
    assert(ex.aluOp == AluOp::ADD);     // EX computes the address
    assert(ex.aluSrcB == SrcB::IMM);
    assert(!ex.memRead);                // the access itself belongs to MEM
    assert(!ex.regWrite);

    const Microword mem = project(ld, Pipeline6::microFields(4));
    assert(mem.memRead);
    assert(!mem.regWrite);              // the write-back belongs to WB

    const Microword wb = project(ld, Pipeline6::microFields(5));
    assert(wb.regWrite);
    assert(wb.wbSrc == WbSrc::MEM);
    assert(!wb.memRead);
}

// What executing one instruction actually moved.
struct Effects {
    bool wroteReg = false, wroteSP = false, wroteMem = false;
    bool pcIsNext = false, pcIsLR = false, stopped = false;
};

// A representative encoding for `opcode`: rd = R1, rs1 = R2 (a valid aligned
// address), rs2 = R3 (a nonzero divisor), imm = 4.
static uint32_t encodeFor(uint8_t opcode, bool useLR) {
    const uint32_t op = static_cast<uint32_t>(opcode) << 26;
    switch (formatForOpcode(opcode)) {
        case Format::R: return op | (1u << 22) | (2u << 18) | (3u << 14);
        case Format::I: return op | (1u << 22) | (2u << 18) | 4u;
        case Format::S: return op | (1u << 22) | (useLR ? (1u << 21) : 0u);
        case Format::B: return op | (2u << 22) | (3u << 18) | 8u;
        case Format::J: return op | 8u;
        default:        return op;
    }
}

static Effects executeOne(uint8_t opcode, bool useLR) {
    Simulator sim(1u << 16);
    State& s = sim.state();
    s.SP = 0x200;
    s.LR = 0x900;
    s.writeGPR(1, 0xDEADBEEFu);   // rd's prior value, unequal to any result
    s.writeGPR(2, 0x100u);        // rs1: a valid aligned address
    s.writeGPR(3, 3u);            // rs2: nonzero, so DIV and MOD do not fault
    sim.memory().writeWord(0x100, 77);
    sim.memory().writeWord(0x104, 78);
    sim.memory().writeWord(0x200, 88);   // POP reads the old top of stack

    const State before = s;
    std::array<uint32_t, 72> memBefore{};
    for (size_t i = 0; i < memBefore.size(); ++i)
        memBefore[i] = sim.memory().readWord(static_cast<uint32_t>(0x0F0 + 4 * i));

    const uint32_t pc = 0x40;
    sim.executeAt(decode(encodeFor(opcode, useLR)), pc);

    Effects e;
    e.wroteReg = (s.gpr != before.gpr) || (s.LR != before.LR);
    e.wroteSP = s.SP != before.SP;
    for (size_t i = 0; i < memBefore.size(); ++i)
        if (sim.memory().readWord(static_cast<uint32_t>(0x0F0 + 4 * i)) != memBefore[i])
            e.wroteMem = true;
    e.pcIsNext = s.PC == pc + 4;
    e.pcIsLR = s.PC == before.LR;
    e.stopped = s.halted;
    return e;
}

// The control word has to be a truthful description of the datapath, or a
// microword-driven execute cannot reproduce the hard-coded one. RegWrite,
// SPWrite, MemWrite and PCSel are each checked against what really moved.
static void control_word_predicts_architectural_effects() {
    for (uint8_t opcode = 0; opcode <= 0x3F; ++opcode) {
        const Microword& w = controlWord(opcode);
        if (w.reserved) continue;
        for (bool useLR : {false, true}) {
            const Effects e = executeOne(opcode, useLR);
            assert(w.regWrite == e.wroteReg);
            assert(w.spWrite == e.wroteSP);
            assert(w.memWrite == e.wroteMem);
            // No instruction may stop the machine: the divisors above are
            // nonzero, the addresses are valid and reserved opcodes are
            // skipped, so nothing is left that legitimately could.
            assert(!e.stopped);
            if (w.pcSel == PcSel::NEXT) assert(e.pcIsNext);
            if (w.pcSel == PcSel::LINK) assert(e.pcIsLR);
            if (w.pcSel == PcSel::TARGET && w.condSel == CondSel::ALWAYS)
                assert(!e.pcIsNext);   // an unconditional jump must redirect
        }
    }
}

// FpuEn has to reach the stall logic, not merely sit in the microword: an FP
// instruction holds the execute stage for its latency, and the hold is charged
// to the FPU rather than the MDU (Section 6.5).
template <class P>
static void fpu_en_holds_the_execute_stage() {
    Simulator fp(1u << 16);
    fp.loadProgram({encodeFor(num(Op::FADD), false)});
    P fpPipe(fp);
    while (fpPipe.tick()) { }
    assert(fpPipe.retired() == 1);
    assert(fpPipe.fpuStalls() > 0);
    assert(fpPipe.mduStalls() == 0);

    // An integer instruction of the same shape holds nothing at all.
    Simulator alu(1u << 16);
    alu.loadProgram({encodeFor(num(Op::ADD), false)});
    P aluPipe(alu);
    while (aluPipe.tick()) { }
    assert(aluPipe.retired() == 1);
    assert(aluPipe.fpuStalls() == 0);
}

int main() {
    worked_examples_match_the_spec();
    mdu_words_match_the_spec();
    fpu_words_match_the_spec();
    stage_fields_partition_the_microword<Pipeline4>();
    stage_fields_partition_the_microword<Pipeline6>();
    microinstruction_holds_only_its_own_stage_lines();
    fpu_en_holds_the_execute_stage<Pipeline4>();
    fpu_en_holds_the_execute_stage<Pipeline6>();
    control_word_predicts_architectural_effects();
    std::printf("test_control: all checks passed\n");
    return 0;
}

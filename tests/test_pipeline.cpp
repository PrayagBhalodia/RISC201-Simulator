// test_pipeline.cpp
// Pipeline timing against the worked cases of ISA Sections 3.2 and 4.2, which
// give a cycle table per hazard. The evaluation of Section 5 compares the two
// designs on cycle counts, so those counts need pinning: every figure below is
// the spec's, not whatever the model happened to produce.

#include "pipeline4/pipeline4.h"
#include "pipeline6/pipeline6.h"
#include "simulator.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

using namespace risc201;

namespace {

constexpr uint32_t kNop = 0;   // opcode 0x00 with every field zero

uint32_t encR(Op op, int rd, int rs1, int rs2) {
    return (static_cast<uint32_t>(op) << 26) | (static_cast<uint32_t>(rd) << 22) |
           (static_cast<uint32_t>(rs1) << 18) | (static_cast<uint32_t>(rs2) << 14);
}

uint32_t encI(Op op, int rd, int rs1, int imm) {
    return (static_cast<uint32_t>(op) << 26) | (static_cast<uint32_t>(rd) << 22) |
           (static_cast<uint32_t>(rs1) << 18) | (static_cast<uint32_t>(imm) & 0x3FFFFu);
}

uint32_t encB(Op op, int rs1, int rs2, int offset) {
    return (static_cast<uint32_t>(op) << 26) | (static_cast<uint32_t>(rs1) << 22) |
           (static_cast<uint32_t>(rs2) << 18) | (static_cast<uint32_t>(offset) & 0x3FFFFu);
}

uint32_t encJ(Op op, int offset) {
    return (static_cast<uint32_t>(op) << 26) | (static_cast<uint32_t>(offset) & 0x3FFFFFFu);
}

struct Timing {
    uint64_t cycles = 0, retired = 0, mdu = 0, loadUse = 0, branch = 0, squashed = 0;
    std::array<uint32_t, 16> gpr{};   // so a timing change can be shown not to alter results
    bool failed = false;
};

// Runs `image` as an all-code program to completion, on the MDU algorithms
// given; the defaults are the machine as built (Section 2.8).
template <class P>
Timing run(const std::vector<uint32_t>& image, MulAlgo mul = MulAlgo::Booth,
           DivAlgo div = DivAlgo::NonRestoring) {
    Simulator sim;
    sim.loadProgram(image);
    sim.setMduAlgorithms(mul, div);
    P pipe(sim);
    while (pipe.tick()) { }
    return Timing{pipe.cycles(),      pipe.retired(),      pipe.mduStalls(),
                  pipe.loadUseStalls(), pipe.branchStalls(), pipe.squashed(),
                  sim.state().gpr,    !sim.lastError().empty()};
}

// Three independent ALU operations overlap one stage apart, so nothing stalls
// and nothing is squashed (Sections 3.2 and 4.2, Case 1).
static void independent_instructions_never_stall() {
    const std::vector<uint32_t> image{encI(Op::ADDI, 1, 0, 1), encI(Op::ADDI, 2, 0, 2),
                                      encR(Op::ADD, 3, 1, 2)};
    for (const Timing& t : {run<Pipeline4>(image), run<Pipeline6>(image)}) {
        assert(!t.failed && t.retired == 3);
        assert(t.mdu == 0 && t.loadUse == 0 && t.branch == 0 && t.squashed == 0);
        assert(t.gpr[3] == 3);
    }
}

// A result forwarded straight into the next instruction costs nothing on
// either design (Case 2): the 4-stage forwards EX/MEM->WB, the 6-stage
// EX/MEM->EX.
static void a_forwarded_alu_result_costs_nothing() {
    const std::vector<uint32_t> image{encI(Op::ADDI, 1, 0, 4), encR(Op::ADD, 3, 1, 1),
                                      encR(Op::SUB, 6, 3, 1)};
    for (const Timing& t : {run<Pipeline4>(image), run<Pipeline6>(image)}) {
        assert(!t.failed && t.loadUse == 0 && t.branch == 0 && t.squashed == 0);
        assert(t.gpr[3] == 8 && t.gpr[6] == 4);
    }
}

// The headline difference between the designs (Case 3). Merging EX and MEM
// makes a loaded value ready exactly when an ALU result is, so the 4-stage
// pipeline has no load-use shadow at all; the 6-stage pipeline pays one cycle.
static void only_the_six_stage_pays_for_a_load_use_dependence() {
    const std::vector<uint32_t> image{encI(Op::ADDI, 1, 0, 0), encI(Op::LD, 3, 1, 0),
                                      encR(Op::ADD, 6, 3, 1)};
    const Timing four = run<Pipeline4>(image);
    const Timing six = run<Pipeline6>(image);
    assert(!four.failed && !six.failed);
    assert(four.loadUse == 0);
    assert(six.loadUse == 1);
    assert(six.cycles == four.cycles + 3);   // two extra stages, plus the stall
}

// A taken conditional branch is resolved in DE/OF on the 4-stage design and a
// stage later, in OF, on the 6-stage one, so it squashes one instruction
// against two (Case 4). The branch also waits a cycle for the operand the
// ADDI in front of it produces, on both designs.
static void a_taken_conditional_branch_costs_one_bubble_then_two() {
    const std::vector<uint32_t> image{
        encI(Op::ADDI, 1, 0, 5), encI(Op::ADDI, 2, 0, 5),
        encB(Op::BEQ, 1, 2, 8),                              // taken, over two words
        encI(Op::ADDI, 3, 0, 99), encI(Op::ADDI, 4, 0, 99),  // both must be skipped
        encI(Op::ADDI, 5, 0, 1)};                            // the target
    const Timing four = run<Pipeline4>(image);
    const Timing six = run<Pipeline6>(image);
    assert(four.squashed == 1);
    assert(six.squashed == 2);
    assert(four.branch == 1 && six.branch == 1);
    // The squashed instructions must not have written anything.
    for (const Timing& t : {four, six}) assert(t.gpr[3] == 0 && t.gpr[4] == 0 && t.gpr[5] == 1);
}

// Prediction is static not-taken, so a branch that falls through is free -
// provided its operands are older than the instruction ahead of it, which is
// what the NOP here arranges (Sections 3.3 and 4.3).
static void a_not_taken_branch_is_free() {
    const std::vector<uint32_t> image{
        encI(Op::ADDI, 1, 0, 5), encI(Op::ADDI, 2, 0, 6), kNop,
        encB(Op::BEQ, 1, 2, 8),                              // not taken
        encI(Op::ADDI, 3, 0, 7), encI(Op::ADDI, 4, 0, 8)};
    for (const Timing& t : {run<Pipeline4>(image), run<Pipeline6>(image)}) {
        assert(!t.failed && t.retired == 6);
        assert(t.branch == 0 && t.squashed == 0);
        assert(t.gpr[3] == 7 && t.gpr[4] == 8);   // the fall-through really ran
    }
}

// The comparator sits one stage ahead of the ALU, so it has its own forwarding
// path and a branch waits for a value just produced. On the 6-stage design the
// wait is one cycle behind an ALU operation but two behind a load, whose value
// only exists after MEM (Section 4.3); the 4-stage design cannot tell them
// apart, because both finish in EX/MEM.
static void a_branch_waits_for_an_operand_just_produced() {
    const std::vector<uint32_t> fromAlu{
        encI(Op::ADDI, 1, 0, 0), encR(Op::SUB, 3, 1, 1), encB(Op::BEQ, 3, 0, 8),
        encI(Op::ADDI, 4, 0, 99), encI(Op::ADDI, 5, 0, 99), encI(Op::ADDI, 6, 0, 1)};
    assert(run<Pipeline4>(fromAlu).branch == 1);
    assert(run<Pipeline6>(fromAlu).branch == 1);

    const std::vector<uint32_t> fromLoad{
        encI(Op::ADDI, 1, 0, 0), encI(Op::LD, 3, 1, 0), encB(Op::BEQ, 3, 0, 8),
        encI(Op::ADDI, 4, 0, 99), encI(Op::ADDI, 5, 0, 99), encI(Op::ADDI, 6, 0, 1)};
    assert(run<Pipeline4>(fromLoad).branch == 1);
    assert(run<Pipeline6>(fromLoad).branch == 2);
}

// B, CALL and RET need no register read, so both designs resolve them in their
// second stage and both pay exactly one bubble (Section 5's comparison table).
static void an_unconditional_jump_costs_both_designs_one_bubble() {
    const std::vector<uint32_t> jump{encI(Op::ADDI, 1, 0, 5), encJ(Op::B, 4),
                                     encI(Op::ADDI, 3, 0, 99)};
    for (const Timing& t : {run<Pipeline4>(jump), run<Pipeline6>(jump)}) {
        assert(t.squashed == 1 && t.branch == 0);
        assert(t.gpr[3] == 0);   // the shadow instruction was skipped
    }

    // One bubble for the CALL and one for the RET.
    const std::vector<uint32_t> callRet{encJ(Op::CALL, 4), encJ(Op::B, 8),
                                        encI(Op::ADDI, 1, 0, 5), encJ(Op::RET, 0)};
    for (const Timing& t : {run<Pipeline4>(callRet), run<Pipeline6>(callRet)}) {
        assert(t.squashed == 2 && t.gpr[1] == 5);
    }
}

// An MDU instruction holds the execute stage for the latency of the algorithm
// that implements it, which costs L-1 stall cycles on both designs whether or
// not anything depends on the result (Sections 3.3, 4.3). The figures come
// from mdu.h, so Booth's 32 and the non-restoring divider's 33 are what the
// loops in mdu.cpp actually cost.
static void an_mdu_instruction_holds_execute_for_its_whole_latency() {
    const std::vector<uint32_t> mul{encI(Op::ADDI, 1, 0, 3), encR(Op::MUL, 3, 1, 1),
                                    encR(Op::ADD, 6, 3, 1)};
    for (const Timing& t : {run<Pipeline4>(mul), run<Pipeline6>(mul)}) {
        assert(t.mdu == Mdu::kMulCycles - 1);
        assert(t.gpr[3] == 9 && t.gpr[6] == 12);
    }

    const std::vector<uint32_t> div{encI(Op::ADDI, 1, 0, 8), encR(Op::DIV, 3, 1, 1),
                                    encR(Op::ADD, 6, 3, 1)};
    for (const Timing& t : {run<Pipeline4>(div), run<Pipeline6>(div)}) {
        assert(t.mdu == Mdu::kDivCycles - 1);
        assert(t.gpr[3] == 1 && t.gpr[6] == 9);
    }
}

// `pipe ... default` hands the multiply to the host in one cycle. That must
// remove every MUL stall and change nothing else: a different route to the
// same product moves cycles around, never results.
static void the_host_multiplier_changes_time_and_not_results() {
    const std::vector<uint32_t> image{encI(Op::ADDI, 1, 0, 3), encR(Op::MUL, 3, 1, 1),
                                      encR(Op::MUL, 6, 3, 1), encR(Op::ADD, 7, 6, 1)};
    const Timing built = run<Pipeline4>(image);
    const Timing host = run<Pipeline4>(image, MulAlgo::Default);

    assert(built.mdu == 2 * (Mdu::kMulCycles - 1));
    assert(host.mdu == 0);
    assert(host.cycles < built.cycles);
    assert(host.gpr == built.gpr);           // identical results, less time
    assert(built.gpr[7] == 30);              // 3*3 = 9, 9*3 = 27, 27+3 = 30

    // Shift-and-add takes the same 32 cycles as Booth, so only the adder count
    // separates them and the pipeline sees no difference at all.
    const Timing shiftAdd = run<Pipeline4>(image, MulAlgo::ShiftAndAdd);
    assert(shiftAdd.cycles == built.cycles && shiftAdd.mdu == built.mdu);
    assert(shiftAdd.gpr == built.gpr);
}

// Restoring division is the one algorithm whose latency its operands decide,
// so the pipeline has to ask the unit per instruction with the values that
// instruction will actually read, instead of holding a constant. Each hold
// below has to be exactly what the unit charges for that operand pair.
static void a_restoring_divide_holds_execute_for_what_its_operands_cost() {
    const Mdu restoring{MulAlgo::Booth, DivAlgo::Restoring};
    struct Case { int a, b, quotient; };
    // Chosen so the four latencies differ, and so that reading the wrong
    // operand changes at least one of them: 1000/3 is the pair whose cost
    // moves if the divisor is lost.
    constexpr Case kCases[] = {
        {8, 8, 1}, {1000, 3, 333}, {255, 1, 255}, {65535, 1, 65535},
    };

    uint64_t seen = 0;
    for (const Case& c : kCases) {
        const std::vector<uint32_t> image{encI(Op::ADDI, 1, 0, c.a),
                                          encI(Op::ADDI, 2, 0, c.b),
                                          encR(Op::DIV, 3, 1, 2)};
        const int cost = restoring.cyclesFor(AluOp::DIV, static_cast<uint32_t>(c.a),
                                             static_cast<uint32_t>(c.b));
        assert((seen & (1ull << cost)) == 0);   // the four costs have to differ
        seen |= 1ull << cost;

        for (const Timing& t : {run<Pipeline4>(image, MulAlgo::Booth, DivAlgo::Restoring),
                                run<Pipeline6>(image, MulAlgo::Booth, DivAlgo::Restoring)}) {
            assert(t.mdu == static_cast<uint64_t>(cost - 1));
            assert(t.gpr[3] == static_cast<uint32_t>(c.quotient));
        }
    }

    // The same program on the non-restoring divider: one constant hold and the
    // same answer. Restoring costs more on this pair, which is the trade.
    const std::vector<uint32_t> image{encI(Op::ADDI, 1, 0, 8), encI(Op::ADDI, 2, 0, 8),
                                      encR(Op::DIV, 3, 1, 2)};
    const Timing fixed = run<Pipeline4>(image);
    assert(fixed.mdu == Mdu::kDivCycles - 1);
    assert(fixed.gpr[3] == 1);
    assert(restoring.cyclesFor(AluOp::DIV, 8, 8) > Mdu::kDivCycles);
}

} // namespace

int main() {
    independent_instructions_never_stall();
    a_forwarded_alu_result_costs_nothing();
    only_the_six_stage_pays_for_a_load_use_dependence();
    a_taken_conditional_branch_costs_one_bubble_then_two();
    a_not_taken_branch_is_free();
    a_branch_waits_for_an_operand_just_produced();
    an_unconditional_jump_costs_both_designs_one_bubble();
    an_mdu_instruction_holds_execute_for_its_whole_latency();
    the_host_multiplier_changes_time_and_not_results();
    a_restoring_divide_holds_execute_for_what_its_operands_cost();
    std::printf("test_pipeline: all checks passed\n");
    return 0;
}

// test_stack.cpp
// Stack boundary safety (ISA Section 2.12). The stack is the one region the
// hardware protects, so each boundary gets a case that crosses it by exactly
// one word and a case that stops on it.

#include "pipeline4/pipeline4.h"
#include "pipeline6/pipeline6.h"
#include "simulator.h"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace risc201;

static uint32_t encI(Op op, int rd, int rs1, int imm) {
    return (static_cast<uint32_t>(op) << 26) | (static_cast<uint32_t>(rd) << 22) |
           (static_cast<uint32_t>(rs1) << 18) | (static_cast<uint32_t>(imm) & 0x3FFFFu);
}

static uint32_t encS(Op op, int r, bool useLR) {
    return (static_cast<uint32_t>(op) << 26) | (static_cast<uint32_t>(r) << 22) |
           (static_cast<uint32_t>(useLR) << 21);
}

// Loads `image` as all-code and puts the stack limit where the caller wants
// the boundary, rather than making the test build an image big enough to
// reach the real one.
static Simulator loaded(const std::vector<uint32_t>& image, uint32_t stackLimit) {
    Simulator sim;
    sim.loadProgram(image);
    sim.setStackLimit(stackLimit);
    return sim;
}

static void push_stops_one_word_above_the_program_image() {
    const uint32_t base = Simulator().stackBase();
    std::vector<uint32_t> image{encI(Op::ADDI, 1, 0, 7),
                                encS(Op::PUSH, 1, false),
                                encS(Op::PUSH, 1, false)};

    Simulator ok = loaded(image, base - 4);   // room for exactly one word
    assert(ok.step() && ok.step());           // ADDI, then the last legal PUSH
    assert(ok.state().SP == base - 4);
    assert(ok.lastError().empty());

    assert(!ok.step());                       // the second PUSH crosses it
    assert(ok.state().halted);
    assert(ok.lastError().find("stack overflow") != std::string::npos);
    assert(ok.state().SP == base - 4);        // SP did not move on the fault
    assert(ok.memory().readWord(base - 8) == 0);  // and nothing was written
}

static void pop_stops_at_the_stack_base() {
    const uint32_t base = Simulator().stackBase();
    Simulator sim = loaded({encI(Op::ADDI, 1, 0, 7),
                            encS(Op::PUSH, 1, false),
                            encS(Op::POP, 2, false),
                            encS(Op::POP, 3, false)},
                           0);
    assert(sim.step() && sim.step() && sim.step());
    assert(sim.state().readGPR(2) == 7);
    assert(sim.state().SP == base);

    assert(!sim.step());                      // the stack is empty now
    assert(sim.state().halted);
    assert(sim.lastError().find("stack underflow") != std::string::npos);
}

static void a_plain_store_may_not_reach_into_a_live_frame() {
    const uint32_t base = Simulator().stackBase();
    // PUSH, then ST one word below SP (free space), then ST at SP itself.
    Simulator sim = loaded({encI(Op::ADDI, 1, 0, 7),
                            encS(Op::PUSH, 1, false),
                            encI(Op::ST, 1, 0, static_cast<int>(base - 8)),
                            encI(Op::ST, 1, 0, static_cast<int>(base - 4))},
                           0);
    assert(sim.step() && sim.step() && sim.step());
    assert(sim.memory().readWord(base - 8) == 7);   // below SP is nobody's frame
    assert(sim.lastError().empty());

    assert(!sim.step());
    assert(sim.state().halted);
    assert(sim.lastError().find("stack corruption") != std::string::npos);
}

// The S-format L bit, the only path to LR: without it a nested CALL loses the
// return address of its caller.
static void push_and_pop_reach_the_link_register() {
    Simulator sim = loaded({encS(Op::PUSH, 0, true), encS(Op::POP, 0, true)}, 0);
    sim.state().LR = 0xABCD1234u;
    assert(sim.step());
    sim.state().LR = 0;                      // as a nested CALL would
    assert(sim.step());
    assert(sim.state().LR == 0xABCD1234u);
    assert(sim.lastError().empty());
}

// The pipelines route PUSH/POP through Simulator::executeAt, so the same
// boundary has to stop them without either model knowing about the stack.
template <class P>
static void the_pipelines_see_the_same_boundary() {
    const uint32_t base = Simulator().stackBase();
    Simulator sim = loaded({encI(Op::ADDI, 1, 0, 7),
                            encS(Op::PUSH, 1, false),
                            encS(Op::PUSH, 1, false)},
                           base - 4);
    P pipe(sim);
    while (pipe.tick()) { }
    assert(sim.state().halted);
    assert(sim.lastError().find("stack overflow") != std::string::npos);
}

// Frame boundaries are recorded as PUSH LR happens, because nothing in memory
// tells a saved LR apart from a saved GPR. They have to unwind with the stack.
static void frame_marks_follow_the_call_depth() {
    Simulator sim = loaded({encS(Op::PUSH, 0, true),
                            encI(Op::ADDI, 1, 0, 7),
                            encS(Op::PUSH, 1, false),   // a local, not a frame
                            encS(Op::PUSH, 0, true),
                            encS(Op::POP, 0, true),
                            encS(Op::POP, 1, false),
                            encS(Op::POP, 0, true)},
                           0);
    assert(sim.frameMarks().empty());
    assert(sim.step() && sim.frameMarks().size() == 1);
    assert(sim.step() && sim.step() && sim.frameMarks().size() == 1);
    assert(sim.step() && sim.frameMarks().size() == 2);
    assert(sim.step() && sim.frameMarks().size() == 1);   // POP LR unwinds one
    assert(sim.step() && sim.frameMarks().size() == 1);   // the local does not
    assert(sim.step() && sim.frameMarks().empty());
    assert(sim.lastError().empty());
}

int main() {
    push_stops_one_word_above_the_program_image();
    pop_stops_at_the_stack_base();
    a_plain_store_may_not_reach_into_a_live_frame();
    push_and_pop_reach_the_link_register();
    frame_marks_follow_the_call_depth();
    the_pipelines_see_the_same_boundary<Pipeline4>();
    the_pipelines_see_the_same_boundary<Pipeline6>();
    std::printf("test_stack: all checks passed\n");
    return 0;
}

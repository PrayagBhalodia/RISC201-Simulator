// simulator.h
// Ties State + Memory + decode into a fetch-decode-execute loop.
// Functional single-cycle model: one instruction retires per step(), with no
// pipeline timing. Stalls, bubbles and MDU latency belong to the 4-stage and
// 6-stage visualizer work, which reuses this execute semantics.

#pragma once
#include "alu.h"
#include "decode.h"
#include "fpu.h"
#include "mdu.h"
#include "memory.h"
#include "state.h"
#include <string>
#include <vector>

namespace risc201 {

// The comparator and target adder that sit in the decode stage (Sections 3.1,
// 4.1): where a control instruction sends the PC, given its own address.
// Returns pc + 4 for anything that does not redirect.
uint32_t branchTarget(const State& s, const DecodedInstruction& d, uint32_t pc);

class Simulator {
public:
    explicit Simulator(size_t memSizeBytes = 1 << 16 /* 64 KB */);

    State& state() { return state_; }
    const State& state() const { return state_; }
    Memory& memory() { return memory_; }
    const Memory& memory() const { return memory_; }

    // Loads raw instruction words at address 0 and takes all of them as code.
    void loadProgram(const std::vector<uint32_t>& words);

    // The first address past the program's code. A fetch here ends the run,
    // which is how a program stops without a halt instruction; the assembler
    // reports the figure and the loader passes it on (Section 2.9).
    uint32_t codeEnd() const { return codeEnd_; }
    void setCodeEnd(uint32_t address) { codeEnd_ = address; }

    // The lowest address PUSH may write: the first word past the program
    // image, so the stack cannot grow into .data or .text (Section 2.12).
    uint32_t stackLimit() const { return stackLimit_; }
    void setStackLimit(uint32_t address) { stackLimit_ = address; }

    // The address the stack starts from, which POP may not rise above.
    uint32_t stackBase() const { return static_cast<uint32_t>(memory_.size()); }

    // Addresses where PUSH LR saved a return address, lowest last. Nothing in
    // memory distinguishes a saved LR from a saved GPR, so this is recorded as
    // it happens; it is what divides the stack into call frames. Entries below
    // SP have been popped and are not live.
    const std::vector<uint32_t>& frameMarks() const { return frameMarks_; }

    // Executes exactly one fetch-decode-execute step. Returns false if the
    // machine has stopped - past the end of the code, or on an unrecoverable
    // exception - and no step ran.
    bool step();

    // Runs step() until halted or a safety cap on cycles is hit (guards
    // against an infinite loop in a buggy test program during debugging).
    void run(uint64_t maxCycles = 1'000'000);

    // Applies one already-decoded instruction to the architectural state, as
    // if fetched from `pc`. The pipeline models call this at their execute
    // stage so the ISA semantics live in exactly one place. Memory faults are
    // recorded in lastError() rather than thrown.
    void executeAt(const DecodedInstruction& instr, uint32_t pc);

    void raiseIllegalOpcode(uint8_t opcode, uint32_t pc);

    // Back to the state of a freshly loaded program: registers, SP, the
    // performance counters, the stack's frame marks and any recorded fault.
    // Memory is left alone, so the loaded image survives. Clearing the
    // counters and lastError_ here is what lets one session measure several
    // runs; leaving either behind reports the previous run's verdict.
    void reset();

    // ALUSrcA and ALUSrcB for an instruction, as the execute stage selects
    // them (Section 6.2). Public because a pipeline needs the same pair to ask
    // the MDU how long an operation will take before it runs.
    struct Operands { uint32_t a = 0, b = 0; };
    Operands operandsFor(const DecodedInstruction& instr) const;

    // Which multiply and divide algorithms the MDU runs (Section 2.8). The
    // choice changes the adder-operation count and, for restoring, the
    // execute-stage latency; it never changes a result.
    void setMduAlgorithms(MulAlgo mul, DivAlgo div) { mdu_ = Mdu(mul, div); }
    const Mdu& mdu() const { return mdu_; }

    uint64_t cyclesExecuted() const { return cycles_; }
    // Adder operations the MDU has performed (Section 2.8). A performance
    // counter, not architectural state: Booth's count depends on the operand
    // bit patterns, which is the only way its advantage over shift-and-add
    // shows, since both take the same 32 cycles.
    uint64_t mduAdderOps() const { return mduAdderOps_; }
    const std::string& lastError() const { return lastError_; }

private:
    State state_;
    Memory memory_;
    Alu alu_;   // the execute stage's arithmetic/logic unit
    Fpu fpu_;   // and its floating-point unit, reached when FpuEn is set
    Mdu mdu_;   // and its multiply/divide unit, reached when MDUEn is set
    uint64_t cycles_ = 0;
    uint64_t mduAdderOps_ = 0;
    uint32_t codeEnd_;
    uint32_t stackLimit_ = 0;   // 0 until a program is loaded: all of memory
    std::vector<uint32_t> frameMarks_;
    std::string lastError_;

    // `pc` is the address the instruction was fetched from, needed only so a
    // divide-by-zero can name the faulting instruction (Section 2.7).
    void execute(const DecodedInstruction& instr, uint32_t pc);
    void raiseDivideByZero(uint32_t pc);

    // Throws MemoryAccessError if the access about to happen would break a
    // stack boundary. `address` is the word the memory stage will touch.
    void checkStackBounds(const Microword& cw, uint32_t address, uint32_t pc) const;
};

} // namespace risc201

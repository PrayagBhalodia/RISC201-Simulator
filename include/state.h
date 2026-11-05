// state.h
// Architectural state for RISC201: 16 GPRs + PC/SP/LR.
// Reference: RISC201 ISA Specification, Section 2.1.
//
// There is no condition-code register: BEQ and BGT resolve by comparing their
// two source registers in the decode stage, so no instruction needs one.

#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace risc201 {

// IEEE-754 exception flags (Section 2.10). Sticky: the FPU sets them and
// nothing clears them, because an FP exception reports a result rather than
// stopping the machine. Visible architectural state via the debugger.
struct FpStatus {
    bool invalid = false;    // 0/0, inf-inf, inf*0, FTOI out of range
    bool divByZero = false;  // finite / 0, which yields a correctly signed inf
    bool overflow = false;   // magnitude too large, rounded to inf
    bool underflow = false;  // subnormal result that was also inexact
    bool inexact = false;    // the rounded result is not the exact one

    bool any() const { return invalid || divByZero || overflow || underflow || inexact; }

    // Sticky: an operation's flags are added to what is already there.
    void accumulate(const FpStatus& r) {
        invalid = invalid || r.invalid;
        divByZero = divByZero || r.divByZero;
        overflow = overflow || r.overflow;
        underflow = underflow || r.underflow;
        inexact = inexact || r.inexact;
    }
    std::string toString() const;
};

// Full processor state. R0 is present in the array for indexing symmetry,
// but must always read as 0; enforce that in read/write helpers, not here.
struct State {
    // R0-R15 are all general purpose. PC, SP and LR sit outside the register
    // file and cannot be named in the rd, rs1 or rs2 fields; only PUSH, POP,
    // CALL and RET reach SP and LR, implicitly or via the S-format L bit.
    std::array<uint32_t, 16> gpr{};
    uint32_t PC = 0;
    uint32_t SP = 0;
    uint32_t LR = 0;
    FpStatus fpsr;

    bool halted = false; // set at the end of the program, or by an exception

    // Reads R0 as hardwired zero; writes to R0 are silently discarded.
    uint32_t readGPR(int index) const;
    void writeGPR(int index, uint32_t value);
};

} // namespace risc201

// fpu.h
// The floating-point unit of the execute stage (ISA Section 2.10).
//
// Combinational and stateless, exactly like Alu: the FP half of ALUOp selects
// the operation and the result comes back with the IEEE exception flags this
// one operation raised, leaving the caller to OR them into FPSR. The FPU is
// reached only when the control word sets FpuEn (Section 6.2).
//
// Single precision, IEEE-754 binary32: 1 sign bit, 8 exponent bits with an
// excess-127 bias, 23 stored mantissa bits with an implicit leading 1 for
// normal numbers. Arithmetic rounds to nearest even; FTOI truncates toward
// zero instead, which Section 2.10 records as the one exception to that rule.

#pragma once
#include "control.h"
#include "state.h"
#include <cstdint>

namespace risc201 {

class Fpu {
public:
    struct Result {
        uint32_t value = 0;
        // Only the flags this operation raised. They are sticky in FPSR, so
        // the caller accumulates rather than assigns, and an FP exception
        // never stops the machine the way an integer one does (Section 2.7).
        FpStatus raised;
    };

    // `op` must be one of the FP codes; anything else returns a zero result
    // with no flags, since FpuEn is what routes an operation here at all.
    Result apply(AluOp op, uint32_t a, uint32_t b) const;
};

} // namespace risc201

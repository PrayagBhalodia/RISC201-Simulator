// mdu.h
// The multiply/divide unit sitting next to the ALU in the execute stage
// (ISA Section 2.8).
//
// Both multipliers and both dividers come from the computer arithmetic chapter
// of Sarangi and are written as the iteration the hardware performs, so each
// one's latency and adder count are properties of its loop rather than figures
// picked for it. `default` is the deliberate exception: it hands the operation
// to the host's `*` and `/` in a single cycle, which is the combinational
// what-if the iterative algorithms are read against in Section 5.
//
// Stateless and const once its algorithms are chosen, like Fpu and Alu, and
// with no condition-code output: RISC201 has no flags register.

#pragma once
#include "control.h"
#include <cstdint>
#include <string>

namespace risc201 {

// Section 2.8. These spellings are what `pipe` accepts and what the evaluation
// table prints; the name and parse helpers below are the only place they are
// written out.
enum class MulAlgo { Default, Booth, ShiftAndAdd };
enum class DivAlgo { Default, Restoring, NonRestoring };

const char* mulAlgoName(MulAlgo a);
const char* divAlgoName(DivAlgo a);

// False when the text names no algorithm, leaving `out` untouched.
bool parseMulAlgo(const std::string& text, MulAlgo& out);
bool parseDivAlgo(const std::string& text, DivAlgo& out);

class Mdu {
public:
    // Execute-stage latencies (Section 2.8). Booth and shift-and-add both
    // inspect one multiplier bit per cycle, which is why they share a figure
    // and differ only in adder operations; non-restoring spends 32 iterations
    // plus the correction cycle it always takes. Restoring has no constant of
    // its own - it is 32 plus however many restore steps the operands force,
    // which is why a pipeline asks cyclesFor instead of holding a number.
    static constexpr int kMulCycles = 32;
    static constexpr int kDivCycles = 33;
    static constexpr int kHostCycles = 1;   // `default`: no iteration to charge for

    struct Result {
        uint32_t value = 0;
        // Adder operations the iteration actually performed. Booth drives the
        // adder only where two neighbouring multiplier bits differ while
        // shift-and-add drives it once per set bit, and restoring drives it a
        // second time for every step it undoes. This is the figure that
        // separates algorithms whose cycle counts are equal.
        int adderOps = 0;
        // Execute-stage cycles this operation cost.
        int cycles = 0;
        // DIV or MOD with a zero divisor, found in the first cycle before any
        // iteration starts. Section 2.7 forbids writing rd, so the caller
        // raises the exception instead of committing anything.
        bool divideByZero = false;
    };

    Mdu() = default;
    Mdu(MulAlgo mul, DivAlgo div) : mul_(mul), div_(div) {}

    MulAlgo mulAlgo() const { return mul_; }
    DivAlgo divAlgo() const { return div_; }

    // `op` must be MUL, DIV or MOD; the immediate forms share these codes and
    // differ only in where the datapath found `b` (Section 6.2).
    Result apply(AluOp op, uint32_t a, uint32_t b) const;

    // The execute-stage hold, which a pipeline has to know before the result
    // exists. Only restoring makes this depend on the operands.
    int cyclesFor(AluOp op, uint32_t a, uint32_t b) const {
        return apply(op, a, b).cycles;
    }

private:
    MulAlgo mul_ = MulAlgo::Booth;
    DivAlgo div_ = DivAlgo::NonRestoring;
};

} // namespace risc201

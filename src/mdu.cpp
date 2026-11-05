// mdu.cpp
// The two multipliers and two dividers of Section 2.8, plus the host-operator
// `default` each pair is read against.
//
// The four iterative algorithms are written as the loop the hardware runs
// rather than as a host operator, so the latencies in mdu.h and the
// adder-operation counts are things these loops produce rather than figures
// asserted about them. All four work in magnitudes except Booth, which takes
// two's complement directly; the sign is applied in one place at the end.

#include "mdu.h"

namespace risc201 {

namespace {

constexpr int kWidth = 32;   // operand width: one bit per iteration

bool negative(uint32_t x) { return ((x >> 31) & 1u) != 0; }

// |x| as an unsigned magnitude. Two's complement negation, so INT32_MIN maps
// to 2^31 - which does not fit in int32_t and is the reason the dividers work
// in unsigned and apply the signs at the end (Section 2.8).
uint32_t magnitude(uint32_t x) { return negative(x) ? (0u - x) : x; }

struct Product {
    uint64_t bits = 0;    // the full 64-bit product, of which rd takes the low half
    int adderOps = 0;
    int cycles = 0;
};

struct Division {
    uint32_t quotient = 0;
    uint32_t remainder = 0;
    int adderOps = 0;
    int cycles = 0;
};

// Booth radix-2. The triple {A, Q, Q_1} is shifted right arithmetically once
// per iteration, and the adder runs only where the pair (Q0, Q_1) differs: 01
// adds the multiplicand, 10 subtracts it, 00 and 11 leave the adder idle. A
// run of 1s therefore costs one add and one subtract instead of one add per
// bit, which is the whole advantage over shift-and-add - the 32 iterations are
// the same either way. Only the multiplier is recoded, so the adder count
// depends on rs2's bit pattern and not on rs1's, though the product does not.
// Works on two's complement directly, so there is no magnitude step and
// INT32_MIN needs no special case.
Product boothMultiply(uint32_t multiplicand, uint32_t multiplier) {
    const int64_t m = static_cast<int32_t>(multiplicand);
    int64_t a = 0;                   // conceptually 33 bits: one guard bit above the operands
    uint32_t q = multiplier;
    uint32_t qMinus1 = 0;
    Product out;
    out.cycles = kWidth;

    for (int i = 0; i < kWidth; ++i) {
        const uint32_t q0 = q & 1u;
        if (q0 != qMinus1) {
            a += (qMinus1 != 0) ? m : -m;
            ++out.adderOps;
        }
        // One arithmetic right shift of the whole triple.
        qMinus1 = q0;
        q = (q >> 1) | (static_cast<uint32_t>(a & 1) << 31);
        a >>= 1;
    }

    out.bits = (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) |
               static_cast<uint64_t>(q);
    return out;
}

// Shift-and-add, the baseline Booth is measured against. It works on the
// magnitudes and fixes the sign at the end, and it drives the adder once per
// set bit of the multiplier: the same 32 iterations as Booth, but a run of 1s
// costs an adder operation per bit where Booth spends two for the whole run.
Product shiftAddMultiply(uint32_t multiplicand, uint32_t multiplier) {
    const uint64_t m = magnitude(multiplicand);
    uint32_t q = magnitude(multiplier);
    Product out;
    out.cycles = kWidth;

    uint64_t acc = 0;
    for (int i = 0; i < kWidth; ++i) {
        if ((q & 1u) != 0) {
            acc += m << i;         // the partial product for this bit
            ++out.adderOps;
        }
        q >>= 1;
    }

    const bool negProduct = negative(multiplicand) != negative(multiplier);
    out.bits = negProduct ? (0ull - acc) : acc;
    return out;
}

// The host's own multiply, in one cycle and with no adder to count. Not an
// algorithm: this is the combinational what-if of Section 5 made runnable.
Product hostMultiply(uint32_t multiplicand, uint32_t multiplier) {
    Product out;
    out.cycles = Mdu::kHostCycles;
    out.bits = static_cast<uint64_t>(
        static_cast<int64_t>(static_cast<int32_t>(multiplicand)) *
        static_cast<int64_t>(static_cast<int32_t>(multiplier)));
    return out;
}

// Non-restoring division, on magnitudes. Each iteration shifts {R, Q} left and
// then adds or subtracts the divisor according to the sign R already had; a
// partial remainder that goes negative is never restored, which is what saves
// the restoring divider's extra cycle and makes this latency fixed. The
// quotient bit is just whether R came back non-negative. The one correction at
// the end fixes a negative remainder and is always spent, so L stays 33.
Division nonRestoringDivide(uint32_t dividend, uint32_t divisor) {
    const int64_t d = static_cast<int64_t>(divisor);
    int64_t r = 0;                   // partial remainder, one bit wider than the operands
    uint32_t q = dividend;
    Division out;
    out.cycles = kWidth + 1;

    for (int i = 0; i < kWidth; ++i) {
        const bool wasNegative = (r < 0);
        // Doubling rather than shifting: a signed left shift of a negative
        // value is undefined, and the partial remainder is routinely negative.
        r = r * 2 + static_cast<int64_t>((q >> 31) & 1u);
        q <<= 1;
        r += wasNegative ? d : -d;
        ++out.adderOps;
        if (r >= 0) q |= 1u;
    }
    if (r < 0) {
        r += d;
        ++out.adderOps;
    }

    out.quotient = q;
    out.remainder = static_cast<uint32_t>(r);
    return out;
}

// Restoring division, on magnitudes. Every iteration subtracts the divisor
// from the shifted partial remainder; where the trial comes out negative it is
// thrown away and the divisor put back, and that restore is both a second
// adder operation and a second cycle. The restores are the whole difference
// from non-restoring, and the reason L is 32 + k rather than a constant.
Division restoringDivide(uint32_t dividend, uint32_t divisor) {
    const int64_t d = static_cast<int64_t>(divisor);
    uint64_t r = 0;                  // partial remainder, one bit wider than the operands
    uint32_t q = dividend;
    int restores = 0;
    Division out;

    for (int i = 0; i < kWidth; ++i) {
        r = (r << 1) | ((q >> 31) & 1u);
        q <<= 1;

        const int64_t trial = static_cast<int64_t>(r) - d;
        ++out.adderOps;
        if (trial >= 0) {
            r = static_cast<uint64_t>(trial);
            q |= 1u;                 // the quotient bit is the sign of the trial
        } else {
            ++out.adderOps;          // the restore puts the divisor back
            ++restores;
        }
    }

    out.cycles = kWidth + restores;
    out.quotient = q;
    out.remainder = static_cast<uint32_t>(r);
    return out;
}

// The host's own divide, on magnitudes like the two iterative dividers. Taking
// magnitudes first is what keeps (-2^31) / (-1) away from the host operator,
// which traps on it, and makes all three dividers share one sign step.
Division hostDivide(uint32_t dividend, uint32_t divisor) {
    Division out;
    out.cycles = Mdu::kHostCycles;
    out.quotient = dividend / divisor;
    out.remainder = dividend % divisor;
    return out;
}

} // namespace

const char* mulAlgoName(MulAlgo a) {
    switch (a) {
        case MulAlgo::Default:     return "default";
        case MulAlgo::Booth:       return "booth";
        case MulAlgo::ShiftAndAdd: return "shift-and-add";
    }
    return "default";
}

const char* divAlgoName(DivAlgo a) {
    switch (a) {
        case DivAlgo::Default:      return "default";
        case DivAlgo::Restoring:    return "restoring";
        case DivAlgo::NonRestoring: return "non-restoring";
    }
    return "default";
}

bool parseMulAlgo(const std::string& text, MulAlgo& out) {
    if (text == "default")            out = MulAlgo::Default;
    else if (text == "booth")         out = MulAlgo::Booth;
    else if (text == "shift-and-add") out = MulAlgo::ShiftAndAdd;
    else return false;
    return true;
}

bool parseDivAlgo(const std::string& text, DivAlgo& out) {
    if (text == "default")             out = DivAlgo::Default;
    else if (text == "restoring")      out = DivAlgo::Restoring;
    else if (text == "non-restoring")  out = DivAlgo::NonRestoring;
    else return false;
    return true;
}

Mdu::Result Mdu::apply(AluOp op, uint32_t a, uint32_t b) const {
    Result out;

    if (op == AluOp::MUL) {
        Product p;
        switch (mul_) {
            case MulAlgo::Booth:       p = boothMultiply(a, b);    break;
            case MulAlgo::ShiftAndAdd: p = shiftAddMultiply(a, b); break;
            case MulAlgo::Default:     p = hostMultiply(a, b);     break;
        }
        out.value = static_cast<uint32_t>(p.bits);
        out.adderOps = p.adderOps;
        out.cycles = p.cycles;
        return out;
    }

    // Checked in the first execute cycle, before the iterations start
    // (Section 2.8). Section 2.7 leaves rd alone.
    if (b == 0) {
        out.divideByZero = true;
        out.cycles = 1;
        return out;
    }

    // DIV and MOD are one division: the same iteration yields both results and
    // ALUOp selects which one leaves the unit (Section 2.8).
    Division dv;
    switch (div_) {
        case DivAlgo::NonRestoring: dv = nonRestoringDivide(magnitude(a), magnitude(b)); break;
        case DivAlgo::Restoring:    dv = restoringDivide(magnitude(a), magnitude(b));    break;
        case DivAlgo::Default:      dv = hostDivide(magnitude(a), magnitude(b));         break;
    }
    out.adderOps = dv.adderOps;
    out.cycles = dv.cycles;

    if (op == AluOp::DIV) {
        const bool negQuotient = negative(a) != negative(b);
        out.value = negQuotient ? (0u - dv.quotient) : dv.quotient;
    } else {
        // The remainder takes the sign of the dividend (Section 2.7).
        out.value = negative(a) ? (0u - dv.remainder) : dv.remainder;
    }
    return out;
}

} // namespace risc201

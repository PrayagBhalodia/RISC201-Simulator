// test_fpu.cpp
// Checks the FPU against ISA Section 2.10. Arithmetic is differential-tested
// bit for bit against the host's own binary32, which is a valid oracle only
// because the formats are identical - the static_asserts below say so.
//
// Two things the host cannot grade: FTOI outside int32 range, where a C cast
// is undefined behaviour, and the exception flags, which would need <cfenv>.
// For those the tables here are the specification and are asserted directly.

#include "fpu.h"
#include "simulator.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

using namespace risc201;

static_assert(sizeof(float) == 4, "the host float is not a binary32");
static_assert(std::numeric_limits<float>::is_iec559,
              "the host float is not IEEE-754, so it cannot be the oracle");

namespace {

const Fpu kFpu;

float asFloat(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

uint32_t asBits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}

bool isNan(uint32_t bits) {
    return (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0;
}

const char* opName(AluOp op) {
    switch (op) {
        case AluOp::FADD: return "FADD";
        case AluOp::FSUB: return "FSUB";
        case AluOp::FMUL: return "FMUL";
        case AluOp::FDIV: return "FDIV";
        default:          return "?";
    }
}

// Every value that has ever broken a soft-float: the boundaries between the
// encodings, the two exact ties against 1.0, and the 2^24 precision cliff.
const uint32_t kEdges[] = {
    0x00000000u, 0x80000000u,  // +0, -0
    0x00000001u, 0x80000001u,  // smallest subnormal
    0x007FFFFFu, 0x807FFFFFu,  // largest subnormal
    0x00800000u, 0x80800000u,  // smallest normal
    0x00800001u, 0x00FFFFFFu,
    0x3F800000u, 0xBF800000u,  // 1.0, -1.0
    0x3F800001u, 0x3F000000u,  // 1.0 + 1ulp, 0.5
    0x40000000u, 0xC0000000u,  // 2.0, -2.0
    0x33800000u, 0x34000000u,  // 2^-24 (an exact tie against 1.0), 2^-23 (1 ulp)
    0x4B000000u, 0x4B7FFFFFu,  // 2^23, 2^24 - 1
    0x4B800000u, 0x4B800001u,  // 2^24, the first value that cannot count by ones
    0x4F000000u, 0xCF000000u,  // 2^31, -2^31
    0x7F7FFFFFu, 0xFF7FFFFFu,  // largest finite
    0x7F800000u, 0xFF800000u,  // +inf, -inf
    0x7FC00000u, 0xFFC00000u,  // quiet NaN
    0x7F800001u,               // a NaN with a different payload
    0x3EAAAAABu, 0x40490FDBu,  // 1/3, pi
};
constexpr size_t kEdgeCount = sizeof kEdges / sizeof kEdges[0];

void checkArith(AluOp op, uint32_t a, uint32_t b) {
    const uint32_t got = kFpu.apply(op, a, b).value;
    const float fa = asFloat(a), fb = asFloat(b);
    float want = 0.0f;
    switch (op) {
        case AluOp::FADD: want = fa + fb; break;
        case AluOp::FSUB: want = fa - fb; break;
        case AluOp::FMUL: want = fa * fb; break;
        case AluOp::FDIV: want = fa / fb; break;
        default: assert(false);
    }
    const uint32_t expected = asBits(want);
    // Section 2.10 canonicalizes NaN, so any NaN matches any NaN.
    if (isNan(got) && isNan(expected)) return;
    if (got != expected) {
        std::printf("  %s(0x%08X, 0x%08X) = 0x%08X, host says 0x%08X\n",
                    opName(op), a, b, got, expected);
        assert(false);
    }
}

uint32_t lcg(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s;
}

} // namespace

// Every pairing of the edge list, which is where the encodings meet.
static void arithmetic_matches_the_host_on_every_edge() {
    const AluOp ops[] = {AluOp::FADD, AluOp::FSUB, AluOp::FMUL, AluOp::FDIV};
    for (AluOp op : ops)
        for (size_t i = 0; i < kEdgeCount; ++i)
            for (size_t j = 0; j < kEdgeCount; ++j)
                checkArith(op, kEdges[i], kEdges[j]);
}

// A seeded corpus, half of it with the two exponents pulled close together so
// that alignment and cancellation are exercised rather than one operand simply
// swamping the other.
static void arithmetic_matches_the_host_on_a_random_corpus() {
    const AluOp ops[] = {AluOp::FADD, AluOp::FSUB, AluOp::FMUL, AluOp::FDIV};
    uint32_t seed = 0x5EED1234u;
    for (int n = 0; n < 60000; ++n) {
        const uint32_t a = lcg(seed);
        uint32_t b = lcg(seed);
        if ((lcg(seed) & 1u) != 0) {
            const int ea = static_cast<int>((a >> 23) & 0xFFu);
            int eb = ea + static_cast<int>(lcg(seed) % 5u) - 2;
            if (eb < 0) eb = 0;
            if (eb > 255) eb = 255;
            b = (b & ~0x7F800000u) | (static_cast<uint32_t>(eb) << 23);
        }
        for (AluOp op : ops) checkArith(op, a, b);
    }
}

// There is no FLT opcode. The assembler emits FGT with the operands exchanged,
// which is exact for every input including NaN.
static uint32_t flt(uint32_t a, uint32_t b) {
    return kFpu.apply(AluOp::FGT, b, a).value;
}

// The two rules a hand-rolled comparator gets wrong: NaN loses every relation,
// and the two zeros are equal despite differing in every comparable way.
static void comparisons_follow_the_nan_and_zero_rules() {
    const uint32_t nans[] = {0x7FC00000u, 0xFFC00000u, 0x7F800001u};
    for (uint32_t nan : nans) {
        for (size_t i = 0; i < kEdgeCount; ++i) {
            for (uint32_t other : {kEdges[i], nan}) {
                assert(kFpu.apply(AluOp::FEQ, nan, other).value == 0);
                assert(kFpu.apply(AluOp::FGT, nan, other).value == 0);
                assert(flt(nan, other) == 0);
                assert(kFpu.apply(AluOp::FEQ, other, nan).value == 0);
                assert(kFpu.apply(AluOp::FGT, other, nan).value == 0);
                assert(flt(other, nan) == 0);
            }
        }
        assert(!kFpu.apply(AluOp::FEQ, nan, nan).raised.any());  // quiet, so no flag
    }

    assert(kFpu.apply(AluOp::FEQ, 0x00000000u, 0x80000000u).value == 1);
    assert(flt(0x80000000u, 0x00000000u) == 0);
    assert(kFpu.apply(AluOp::FGT, 0x00000000u, 0x80000000u).value == 0);
}

static void comparisons_match_the_host() {
    uint32_t seed = 0xC0FFEEu;
    for (int n = 0; n < 40000; ++n) {
        const uint32_t a = (n < static_cast<int>(kEdgeCount * kEdgeCount))
                               ? kEdges[static_cast<size_t>(n) / kEdgeCount]
                               : lcg(seed);
        const uint32_t b = (n < static_cast<int>(kEdgeCount * kEdgeCount))
                               ? kEdges[static_cast<size_t>(n) % kEdgeCount]
                               : lcg(seed);
        const float fa = asFloat(a), fb = asFloat(b);
        const uint32_t eq = kFpu.apply(AluOp::FEQ, a, b).value;
        const uint32_t gt = kFpu.apply(AluOp::FGT, a, b).value;
        assert(eq == (fa == fb ? 1u : 0u));
        assert(gt == (fa > fb ? 1u : 0u));
        assert(flt(a, b) == (fa < fb ? 1u : 0u));

        // Trichotomy makes "neither equal nor greater" mean "less" - but only
        // for ordered operands. See the NaN case below for why FLT is a swap
        // and not that conjunction.
        const bool ordered = (fa == fa) && (fb == fb);
        if (ordered) assert(flt(a, b) == ((!eq && !gt) ? 1u : 0u));
    }
}

// Why FLT has to be FGT with the operands exchanged rather than
// !FEQ AND !FGT: every comparison against a NaN is false, so both negations
// come out true and the conjunction claims a < b when nothing is ordered.
static void the_trichotomy_identity_breaks_on_nan() {
    const uint32_t nan = 0x7FC00000u, one = 0x3F800000u;
    for (auto pair : {std::pair<uint32_t, uint32_t>{nan, one},
                      {one, nan},
                      {nan, nan}}) {
        const uint32_t a = pair.first, b = pair.second;
        const uint32_t eq = kFpu.apply(AluOp::FEQ, a, b).value;
        const uint32_t gt = kFpu.apply(AluOp::FGT, a, b).value;
        assert(eq == 0 && gt == 0);
        assert(flt(a, b) == 0);     // what IEEE-754 requires, and what we emit
        assert(!eq && !gt);         // what !FEQ AND !FGT would answer: wrong
    }
}

static void itof_matches_the_host() {
    uint32_t seed = 0x1234ABCDu;
    const uint32_t fixed[] = {
        0u, 1u, 0xFFFFFFFFu, 2u, 0x7FFFFFFFu, 0x80000000u, 0x80000001u,
        0x00FFFFFFu,  // 2^24 - 1, the largest int a float still counts by ones
        0x01000000u,  // 2^24
        0x01000001u,  // 2^24 + 1, which has no binary32 and must round to 2^24
        0x01000003u,
    };
    for (uint32_t x : fixed) {
        const uint32_t got = kFpu.apply(AluOp::ITOF, x, 0).value;
        assert(got == asBits(static_cast<float>(static_cast<int32_t>(x))));
    }
    for (int n = 0; n < 40000; ++n) {
        const uint32_t x = lcg(seed);
        const uint32_t got = kFpu.apply(AluOp::ITOF, x, 0).value;
        assert(got == asBits(static_cast<float>(static_cast<int32_t>(x))));
    }

    // Above 2^24 the significand runs out of bits, which must be reported.
    assert(kFpu.apply(AluOp::ITOF, 0x01000001u, 0).value == 0x4B800000u);
    assert(kFpu.apply(AluOp::ITOF, 0x01000001u, 0).raised.inexact);
    assert(!kFpu.apply(AluOp::ITOF, 0x01000000u, 0).raised.inexact);
    assert(!kFpu.apply(AluOp::ITOF, 0x80000000u, 0).raised.any());  // -2^31 is exact
}

// The host is not an oracle here: casting an out-of-range float to int is
// undefined behaviour in C++, so this table is the specification.
static void ftoi_truncates_and_saturates() {
    struct Case { uint32_t in, out; bool invalid, inexact; };
    const Case cases[] = {
        {0x7FC00000u, 0x7FFFFFFFu, true,  false},  // NaN
        {0xFFC00000u, 0x7FFFFFFFu, true,  false},  // NaN, sign ignored
        {0x7F800000u, 0x7FFFFFFFu, true,  false},  // +inf
        {0xFF800000u, 0x80000000u, true,  false},  // -inf
        {0x4F000000u, 0x7FFFFFFFu, true,  false},  // +2^31, one past the top
        {0xCF000000u, 0x80000000u, false, false},  // -2^31, exactly representable
        {0xCF000001u, 0x80000000u, true,  false},  // just past -2^31
        {0x4EFFFFFFu, 0x7FFFFF80u, false, false},  // 2^31 - 128, the largest below
        {0x3F800000u, 0x00000001u, false, false},  // 1.0
        {0x3FC00000u, 0x00000001u, false, true},   // 1.5 truncates down
        {0xBFC00000u, 0xFFFFFFFFu, false, true},   // -1.5 truncates toward zero
        {0x3F000000u, 0x00000000u, false, true},   // 0.5
        {0xBF000000u, 0x00000000u, false, true},   // -0.5, not -1
        {0x00000000u, 0x00000000u, false, false},  // +0
        {0x80000000u, 0x00000000u, false, false},  // -0
        {0x00000001u, 0x00000000u, false, true},   // smallest subnormal
    };
    for (const Case& c : cases) {
        const Fpu::Result r = kFpu.apply(AluOp::FTOI, c.in, 0);
        if (r.value != c.out) {
            std::printf("  FTOI(0x%08X) = 0x%08X, want 0x%08X\n", c.in, r.value, c.out);
            assert(false);
        }
        assert(r.raised.invalid == c.invalid);
        assert(r.raised.inexact == c.inexact);
    }

    // In range the host agrees, so the random corpus can still be graded.
    uint32_t seed = 0xFEEDBEEFu;
    for (int n = 0; n < 40000; ++n) {
        const uint32_t x = lcg(seed);
        const float f = asFloat(x);
        if (!(f > -2147483648.0f && f < 2147483648.0f)) continue;  // excludes NaN too
        const uint32_t got = kFpu.apply(AluOp::FTOI, x, 0).value;
        assert(got == static_cast<uint32_t>(static_cast<int32_t>(f)));
    }
}

// FPSR is sticky and never traps, so every one of these reports a result.
static void exception_flags_follow_section_2_10() {
    Fpu::Result r = kFpu.apply(AluOp::FADD, 0x7F800000u, 0xFF800000u);
    assert(r.raised.invalid && isNan(r.value));              // inf - inf

    r = kFpu.apply(AluOp::FMUL, 0x7F800000u, 0x00000000u);
    assert(r.raised.invalid && isNan(r.value));              // inf * 0

    r = kFpu.apply(AluOp::FDIV, 0x00000000u, 0x00000000u);
    assert(r.raised.invalid && isNan(r.value));              // 0 / 0

    r = kFpu.apply(AluOp::FDIV, 0x3F800000u, 0x00000000u);   // 1 / +0
    assert(r.raised.divByZero && !r.raised.invalid && r.value == 0x7F800000u);

    r = kFpu.apply(AluOp::FDIV, 0x3F800000u, 0x80000000u);   // 1 / -0
    assert(r.raised.divByZero && r.value == 0xFF800000u);

    r = kFpu.apply(AluOp::FMUL, 0x7F7FFFFFu, 0x7F7FFFFFu);   // max * max
    assert(r.raised.overflow && r.raised.inexact && r.value == 0x7F800000u);

    r = kFpu.apply(AluOp::FMUL, 0x00000001u, 0x3F000000u);   // tiny * 0.5
    assert(r.raised.underflow && r.raised.inexact && r.value == 0x00000000u);

    // Round to nearest even on an exact tie: 1.0 + 2^-24 keeps the even
    // mantissa, while 1.0 + 2^-23 is representable and raises nothing.
    r = kFpu.apply(AluOp::FADD, 0x3F800000u, 0x33800000u);
    assert(r.value == 0x3F800000u && r.raised.inexact);
    r = kFpu.apply(AluOp::FADD, 0x3F800000u, 0x34000000u);
    assert(r.value == 0x3F800001u && !r.raised.any());

    // Exact subnormal arithmetic raises nothing at all.
    r = kFpu.apply(AluOp::FSUB, 0x00800000u, 0x00000001u);
    assert(r.value == 0x007FFFFFu && !r.raised.any());
    r = kFpu.apply(AluOp::FADD, 0x00000001u, 0x00000001u);
    assert(r.value == 0x00000002u && !r.raised.any());

    // Cancellation is +0 under round-to-nearest, even for negative operands.
    r = kFpu.apply(AluOp::FADD, 0xBF800000u, 0x3F800000u);
    assert(r.value == 0x00000000u);
    // but -0 + -0 keeps the sign.
    r = kFpu.apply(AluOp::FADD, 0x80000000u, 0x80000000u);
    assert(r.value == 0x80000000u);
}

// The unit only reports; the simulator is what makes FPSR sticky, and what
// decides that an FP exception does not stop the machine (Section 2.10).
static void the_simulator_accumulates_fpsr() {
    const uint32_t fdiv = (static_cast<uint32_t>(Op::FDIV) << 26) |
                          (1u << 22) | (2u << 18);           // FDIV R1, R2, R0
    const uint32_t fadd = (static_cast<uint32_t>(Op::FADD) << 26) |
                          (3u << 22) | (2u << 18) | (2u << 14);  // FADD R3, R2, R2

    Simulator sim(1u << 16);
    sim.state().writeGPR(2, 0x3F800000u);   // R2 = 1.0; R0 is hardwired to zero
    sim.loadProgram({fdiv, fadd});
    sim.run();

    assert(sim.state().readGPR(1) == 0x7F800000u);  // 1.0 / 0 is +inf
    assert(sim.lastError().empty());                // and no exception was raised
    assert(sim.state().fpsr.divByZero);             // the flag stuck past FADD
    assert(!sim.state().fpsr.invalid);
    assert(sim.state().readGPR(3) == 0x40000000u);  // FADD still ran: 2.0

    // The integer divider still halts the machine, which is the asymmetry
    // Section 2.10 introduces: only the FPU reports instead of trapping.
    const uint32_t div = (static_cast<uint32_t>(Op::DIV) << 26) |
                         (1u << 22) | (2u << 18);
    Simulator trap(1u << 16);
    trap.state().writeGPR(2, 1);
    trap.loadProgram({div});
    trap.run();
    assert(trap.state().halted && !trap.lastError().empty());
}

int main() {
    arithmetic_matches_the_host_on_every_edge();
    arithmetic_matches_the_host_on_a_random_corpus();
    comparisons_follow_the_nan_and_zero_rules();
    comparisons_match_the_host();
    the_trichotomy_identity_breaks_on_nan();
    itof_matches_the_host();
    ftoi_truncates_and_saturates();
    exception_flags_follow_section_2_10();
    the_simulator_accumulates_fpsr();
    std::printf("test_fpu: all checks passed\n");
    return 0;
}

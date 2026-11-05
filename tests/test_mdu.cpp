// test_mdu.cpp
// Checks the multipliers and dividers of Section 2.8 against the host's own
// operators and the worked examples of Section 2.7. The algorithms may differ
// from each other in how long they take and how often they drive the adder,
// never in what they produce.

#include "mdu.h"
#include <cassert>
#include <cstdio>

using namespace risc201;

namespace {

const Mdu kMdu;   // as built: Booth and non-restoring
const Mdu kShiftAdd{MulAlgo::ShiftAndAdd, DivAlgo::NonRestoring};
const Mdu kRestoring{MulAlgo::Booth, DivAlgo::Restoring};
const Mdu kHost{MulAlgo::Default, DivAlgo::Default};

int popcount(uint32_t v) {
    int n = 0;
    for (; v != 0; v >>= 1) n += static_cast<int>(v & 1u);
    return n;
}

uint32_t magnitude(uint32_t v) { return (v >> 31) ? (0u - v) : v; }

constexpr uint32_t u(int32_t v) { return static_cast<uint32_t>(v); }
int32_t i(uint32_t v) { return static_cast<int32_t>(v); }
int64_t wide(uint32_t v) { return static_cast<int64_t>(i(v)); }

// The operands worth crossing exhaustively: the identities, both extremes, the
// alternating and all-ones words that Booth's adder count turns on, and the
// 2^16 pair whose product is exactly 2^32.
constexpr uint32_t kInteresting[] = {
    0u, 1u, u(-1), 2u, u(-2), 3u, u(-3), 7u, u(-7), 10u, u(-10), 100u, u(-100),
    0x7FFFFFFFu, 0x80000000u, 0x7FFFFFFEu, 0x80000001u,
    0x55555555u, 0xAAAAAAAAu, 0x10000u, 0xFFFFu, 0x40000000u, 0x00FF00FFu,
};

// xorshift32, so the sweep is a fixed sequence and a failure is reproducible.
uint32_t nextRandom(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

void multiplyAgreesWithHost(uint32_t a, uint32_t b) {
    const Mdu::Result r = kMdu.apply(AluOp::MUL, a, b);
    assert(r.value == static_cast<uint32_t>(wide(a) * wide(b)));
    assert(r.adderOps >= 0 && r.adderOps <= Mdu::kMulCycles);
    assert(!r.divideByZero);
}

void divisionAgreesWithHost(uint32_t a, uint32_t b) {
    // The host traps on both of these, so the ISA's answers for them are
    // pinned separately below rather than compared against C.
    if (b == 0 || (a == 0x80000000u && b == 0xFFFFFFFFu)) return;

    const Mdu::Result q = kMdu.apply(AluOp::DIV, a, b);
    const Mdu::Result m = kMdu.apply(AluOp::MOD, a, b);
    assert(i(q.value) == i(a) / i(b));               // truncated toward zero
    assert(i(m.value) == i(a) % i(b));               // remainder takes rs1's sign
    assert(wide(a) == wide(q.value) * wide(b) + wide(m.value));

    // 32 add-or-subtract iterations, plus the correction when it is needed.
    assert(q.adderOps == Mdu::kDivCycles || q.adderOps == Mdu::kDivCycles - 1);
}

// Both algorithms, over every interesting pair and then a pseudorandom sweep.
// This is the check that the iterations are arithmetic and not a guess.
static void both_algorithms_agree_with_the_host_operators() {
    for (uint32_t a : kInteresting) {
        for (uint32_t b : kInteresting) {
            multiplyAgreesWithHost(a, b);
            divisionAgreesWithHost(a, b);
        }
    }

    uint32_t seed = 0x1D0C5A11u;
    for (int n = 0; n < 20000; ++n) {
        const uint32_t a = nextRandom(seed);
        const uint32_t b = nextRandom(seed);
        multiplyAgreesWithHost(a, b);
        divisionAgreesWithHost(a, b);
    }
}

// Section 2.8: Booth drives the adder only where two neighbouring multiplier
// bits differ, so a run of 1s is nearly free while alternating bits are the
// worst case. The cycle count is 32 either way, which is why this count is
// the only way the algorithm's advantage is visible.
static void booth_drives_the_adder_only_where_bits_differ() {
    const int zero = kMdu.apply(AluOp::MUL, 12345u, 0u).adderOps;
    const int ones = kMdu.apply(AluOp::MUL, 12345u, 0xFFFFFFFFu).adderOps;
    const int three = kMdu.apply(AluOp::MUL, 12345u, 3u).adderOps;
    const int alternating = kMdu.apply(AluOp::MUL, 12345u, 0x55555555u).adderOps;

    assert(zero == 0);                  // no 01 or 10 pair anywhere
    assert(ones == 1);                  // one long run: a single subtract
    assert(three == 2);                 // a two-bit run: one subtract, one add
    assert(alternating == Mdu::kMulCycles);   // every pair differs: the worst case

    // The claim that justifies choosing Booth over shift-and-add, which would
    // have driven the adder 32 times for the all-ones multiplier.
    assert(ones < alternating);

    // Only rs2 is recoded, so the cost is asymmetric in the operands even
    // though the product is not. tests/bench/arith.s relies on this to put its
    // patterns in the operand that actually drives the count.
    const Mdu::Result pattern_in_rs2 = kMdu.apply(AluOp::MUL, 3u, 0x55555555u);
    const Mdu::Result pattern_in_rs1 = kMdu.apply(AluOp::MUL, 0x55555555u, 3u);
    assert(pattern_in_rs2.value == pattern_in_rs1.value);
    assert(pattern_in_rs2.adderOps > pattern_in_rs1.adderOps);
}

// The MUL rows of Section 2.7's worked table. rd takes the low 32 bits of the
// product, so the last two keep only what fits.
static void multiply_follows_the_worked_examples() {
    assert(i(kMdu.apply(AluOp::MUL, u(-3), 5u).value) == -15);
    assert(kMdu.apply(AluOp::MUL, 0x10000u, 0x10000u).value == 0);        // 2^32
    assert(kMdu.apply(AluOp::MUL, 0x7FFFFFFFu, 2u).value == 0xFFFFFFFEu); // 2^32 - 2
}

// The four sign combinations of Section 2.7's division table: the quotient
// rounds toward zero and the remainder carries the sign of the dividend.
static void division_follows_the_worked_examples() {
    struct Row { int32_t a, b, q, r; };
    constexpr Row kRows[] = {
        {7, 2, 3, 1}, {-7, 2, -3, -1}, {7, -2, -3, 1}, {-7, -2, 3, -1},
    };
    for (const Row& row : kRows) {
        assert(i(kMdu.apply(AluOp::DIV, u(row.a), u(row.b)).value) == row.q);
        assert(i(kMdu.apply(AluOp::MOD, u(row.a), u(row.b)).value) == row.r);
    }
}

// (-2^31) / (-1) is the one quotient that does not fit in 32 bits, and the
// host traps on it, so the sweep above skips it and the ISA's answer is pinned
// here: the iteration wraps to 0x80000000, and the remainder is still 0.
static void int_min_over_minus_one_wraps_instead_of_trapping() {
    assert(kMdu.apply(AluOp::DIV, 0x80000000u, 0xFFFFFFFFu).value == 0x80000000u);
    assert(kMdu.apply(AluOp::MOD, 0x80000000u, 0xFFFFFFFFu).value == 0);

    // The neighbouring cases are ordinary and must come out exact.
    assert(kMdu.apply(AluOp::DIV, 0x80000000u, 1u).value == 0x80000000u);
    assert(i(kMdu.apply(AluOp::DIV, 0x80000001u, 0xFFFFFFFFu).value) == 0x7FFFFFFF);
    assert(i(kMdu.apply(AluOp::DIV, 0x7FFFFFFFu, 0xFFFFFFFFu).value) == -0x7FFFFFFF);
}

// Every multiplier and every divider is a different route to the same answer:
// that is what makes the cycle and adder columns a fair comparison rather
// than a comparison of two different machines.
static void every_algorithm_computes_the_same_result() {
    auto agree = [](uint32_t a, uint32_t b) {
        const uint32_t mul = kMdu.apply(AluOp::MUL, a, b).value;
        assert(kShiftAdd.apply(AluOp::MUL, a, b).value == mul);
        assert(kHost.apply(AluOp::MUL, a, b).value == mul);
        if (b == 0) return;
        for (AluOp op : {AluOp::DIV, AluOp::MOD}) {
            const uint32_t v = kMdu.apply(op, a, b).value;
            assert(kRestoring.apply(op, a, b).value == v);
            assert(kHost.apply(op, a, b).value == v);
        }
    };

    for (uint32_t a : kInteresting)
        for (uint32_t b : kInteresting) agree(a, b);

    uint32_t seed = 0x6F1E2D3Cu;
    for (int n = 0; n < 20000; ++n) {
        const uint32_t a = nextRandom(seed);
        agree(a, nextRandom(seed));
    }
}

// Section 2.8: shift-and-add takes the magnitudes and adds one partial product
// per set bit of the multiplier, so its adder count is the population count of
// |rs2| and never the 32 iterations it always spends.
static void shift_and_add_drives_the_adder_once_per_set_bit() {
    for (uint32_t b : kInteresting) {
        const Mdu::Result r = kShiftAdd.apply(AluOp::MUL, 12345u, b);
        assert(r.adderOps == popcount(magnitude(b)));
        assert(r.cycles == Mdu::kMulCycles);
    }

    // Each recoding wins on the pattern the other is worst at, which is the
    // whole reason both are built rather than one asserted better.
    const uint32_t runOf1s = 0x7FFFFFFFu;      // one long run, two Booth steps
    assert(kMdu.apply(AluOp::MUL, 3u, runOf1s).adderOps <
           kShiftAdd.apply(AluOp::MUL, 3u, runOf1s).adderOps);

    const uint32_t alternating = 0x55555555u;  // every pair differs: Booth's worst
    assert(kShiftAdd.apply(AluOp::MUL, 3u, alternating).adderOps <
           kMdu.apply(AluOp::MUL, 3u, alternating).adderOps);
}

// Section 2.8: restoring undoes a subtraction that went negative, so each
// restore is a second adder operation and a second cycle. A trial subtraction
// succeeds exactly where the quotient gets a 1 bit, so the restores are the
// zero bits of the quotient and L is 32 + that - the one latency here the
// operands decide rather than the unit.
static void restoring_spends_a_cycle_on_every_step_it_undoes() {
    auto check = [](uint32_t a, uint32_t b) {
        const uint32_t quotient = magnitude(a) / magnitude(b);   // what it works on
        const Mdu::Result r = kRestoring.apply(AluOp::DIV, a, b);
        assert(r.cycles == 64 - popcount(quotient));
        assert(r.cycles >= 32 && r.cycles <= 64);
        assert(r.adderOps == r.cycles);   // 32 trials, plus one add back per restore
        return r.cycles;
    };

    assert(check(0x7FFFFFFFu, 1u) == 33);          // 31 quotient bits set: one restore
    assert(check(0x80000000u, 1u) == 63);          // a power of two: 31 restores
    assert(check(100u, 100u) == 63);               // a quotient of 1

    int varied = 0;
    const int first = check(3u, 2u);
    uint32_t seed = 0x4C8A91B7u;
    for (int n = 0; n < 20000; ++n) {
        const uint32_t a = nextRandom(seed);
        const uint32_t b = nextRandom(seed);
        if (b == 0) continue;
        if (check(a, b) != first) ++varied;
    }
    // The latency has to actually move, or asking the unit for it is pointless.
    assert(varied > 0);

    // MOD is the same division, so it costs the same.
    assert(kRestoring.apply(AluOp::MOD, 0x80000000u, 1u).cycles == 63);
}

// What each algorithm costs, which is what the pipelines charge for it.
static void every_algorithm_reports_its_own_latency() {
    assert(kMdu.apply(AluOp::MUL, 7u, 9u).cycles == Mdu::kMulCycles);
    assert(kShiftAdd.apply(AluOp::MUL, 7u, 9u).cycles == Mdu::kMulCycles);
    assert(kHost.apply(AluOp::MUL, 7u, 9u).cycles == Mdu::kHostCycles);

    assert(kMdu.apply(AluOp::DIV, 7u, 9u).cycles == Mdu::kDivCycles);
    assert(kHost.apply(AluOp::DIV, 7u, 9u).cycles == Mdu::kHostCycles);

    // The host operators are not an iteration, so they drive no adder at all.
    assert(kHost.apply(AluOp::MUL, 0x55555555u, 0x55555555u).adderOps == 0);
    assert(kHost.apply(AluOp::DIV, 0x55555555u, 7u).adderOps == 0);

    // cyclesFor is what a pipeline asks before the result exists.
    assert(kRestoring.cyclesFor(AluOp::DIV, 0x7FFFFFFFu, 1u) == 33);
    assert(kMdu.cyclesFor(AluOp::MUL, 1u, 1u) == Mdu::kMulCycles);
}

// The host divider would trap on this; taking magnitudes first is what keeps
// all three dividers on the same answer (Section 2.8).
static void int_min_over_minus_one_is_the_same_on_every_divider() {
    for (const Mdu* m : {&kMdu, &kRestoring, &kHost}) {
        assert(m->apply(AluOp::DIV, 0x80000000u, 0xFFFFFFFFu).value == 0x80000000u);
        assert(m->apply(AluOp::MOD, 0x80000000u, 0xFFFFFFFFu).value == 0);
    }
}

// Section 2.8 puts the zero-divisor check in the first execute cycle, before
// the iterations start, and Section 2.7 forbids writing rd.
static void zero_divisor_is_reported_before_any_iteration() {
    for (const Mdu* m : {&kMdu, &kRestoring, &kHost}) {
        for (AluOp op : {AluOp::DIV, AluOp::MOD}) {
            const Mdu::Result r = m->apply(op, 10u, 0u);
            assert(r.divideByZero);
            assert(r.value == 0);
            assert(r.adderOps == 0);   // the iteration genuinely never ran
            assert(r.cycles == 1);     // found before it would have started
        }
    }
    assert(!kMdu.apply(AluOp::DIV, 10u, 2u).divideByZero);
}

// The latencies declared in mdu.h have to be what the loops in mdu.cpp cost,
// and nothing else in the build checks that: a wrong constant would make the
// pipelines charge the wrong time while every result stayed correct. The
// worst-case adder counts tie the two together, because each algorithm can
// drive the adder at most once per iteration - Booth does so when every bit
// pair differs, and the divider does so on every iteration plus the correction.
static void the_latencies_match_the_iteration_counts() {
    // Not a random search: 0x55555555 is the only multiplier whose every pair
    // differs, so this is the one input that exposes the iteration count.
    assert(kMdu.apply(AluOp::MUL, 12345u, 0x55555555u).adderOps ==
           Mdu::kMulCycles);

    int worstDivide = 0;
    uint32_t seed = 0x2A1F3C5Du;
    for (int n = 0; n < 5000; ++n) {
        const uint32_t a = nextRandom(seed);
        const uint32_t b = nextRandom(seed);
        if (b == 0) continue;
        const int ops = kMdu.apply(AluOp::DIV, a, b).adderOps;
        if (ops > worstDivide) worstDivide = ops;
    }
    assert(worstDivide == Mdu::kDivCycles);   // 32 iterations plus the correction
}

} // namespace

int main() {
    both_algorithms_agree_with_the_host_operators();
    booth_drives_the_adder_only_where_bits_differ();
    multiply_follows_the_worked_examples();
    division_follows_the_worked_examples();
    int_min_over_minus_one_wraps_instead_of_trapping();
    every_algorithm_computes_the_same_result();
    shift_and_add_drives_the_adder_once_per_set_bit();
    restoring_spends_a_cycle_on_every_step_it_undoes();
    every_algorithm_reports_its_own_latency();
    int_min_over_minus_one_is_the_same_on_every_divider();
    zero_divisor_is_reported_before_any_iteration();
    the_latencies_match_the_iteration_counts();
    std::printf("test_mdu: all checks passed\n");
    return 0;
}

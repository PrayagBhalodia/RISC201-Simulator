// test_alu.cpp
// Checks the result of every integer ALUOp row of ISA Section 2.3, and the
// carry-lookahead adder behind ADD and SUB against the host's own operators.
// MUL, DIV and MOD belong to the MDU; test_mdu.cpp covers them.

#include "alu.h"
#include <cassert>
#include <cstdio>

using namespace risc201;

namespace {

const Alu kAlu;

int32_t i(uint32_t v) { return static_cast<int32_t>(v); }

// xorshift32, so the sweep is a fixed sequence and a failure is reproducible.
uint32_t nextRandom(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

// Section 2.3 is 32-bit two's complement throughout, so both wrap silently.
static void add_and_sub_wrap_in_two_s_complement() {
    assert(kAlu.apply(AluOp::ADD, 0xFFFFFFFFu, 1) == 0);
    assert(kAlu.apply(AluOp::ADD, 0x7FFFFFFFu, 1) == 0x80000000u);
    assert(kAlu.apply(AluOp::SUB, 5, 3) == 2);
    assert(i(kAlu.apply(AluOp::SUB, 3, 5)) == -2);
    assert(kAlu.apply(AluOp::SUB, 0x80000000u, 1) == 0x7FFFFFFFu);
}

// The adder is a two-level carry-lookahead, so what can go wrong is a carry
// that has to leave a 4-bit block, leave the low 16-bit half, or leave the
// word. Every all-ones prefix plus 1 forces the carry out of exactly one bit.
static void the_adder_carries_across_every_block_boundary() {
    for (int n = 0; n < 32; ++n) {
        const uint32_t ones = (n == 31) ? 0xFFFFFFFFu : ((1u << (n + 1)) - 1u);
        assert(kAlu.apply(AluOp::ADD, ones, 1) == ones + 1u);
        assert(kAlu.apply(AluOp::ADD, 1, ones) == ones + 1u);
        assert(kAlu.apply(AluOp::SUB, 0, ones) == 0u - ones);

        // A carry entering a block that only propagates has to cross it whole.
        const uint32_t hole = ~ones;
        assert(kAlu.apply(AluOp::ADD, hole, ones) == 0xFFFFFFFFu);
        assert(kAlu.apply(AluOp::ADD, ones, ones) == ones + ones);
    }
}

// The lookahead equations may differ from a ripple-carry adder in how long
// they take, never in what they produce.
static void the_adder_agrees_with_the_host_operators() {
    constexpr uint32_t kInteresting[] = {
        0u, 1u, 2u, 3u, 0xFu, 0x10u, 0xFFu, 0x100u, 0xFFFFu, 0x10000u,
        0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFFu, 0xFFFFFFFEu,
        0x55555555u, 0xAAAAAAAAu, 0x0F0F0F0Fu, 0xF0F0F0F0u, 0x11111111u,
    };
    for (uint32_t a : kInteresting) {
        for (uint32_t b : kInteresting) {
            assert(kAlu.apply(AluOp::ADD, a, b) == a + b);
            assert(kAlu.apply(AluOp::SUB, a, b) == a - b);
        }
    }

    uint32_t seed = 0x13579BDFu;
    for (int n = 0; n < 50000; ++n) {
        const uint32_t a = nextRandom(seed);
        const uint32_t b = nextRandom(seed);
        assert(kAlu.apply(AluOp::ADD, a, b) == a + b);
        assert(kAlu.apply(AluOp::SUB, a, b) == a - b);
        assert(kAlu.apply(AluOp::SUB, a, a) == 0);
    }
}

static void logic_ops_are_bitwise() {
    assert(kAlu.apply(AluOp::AND, 0xF0F0F0F0u, 0x0F0F0F0Fu) == 0);
    assert(kAlu.apply(AluOp::OR,  0xF0F0F0F0u, 0x0F0F0F0Fu) == 0xFFFFFFFFu);
    assert(kAlu.apply(AluOp::XOR, 0xFFu, 0xFFu) == 0);
    assert(kAlu.apply(AluOp::NOT, 0, 0xDEADu) == 0xFFFFFFFFu);  // B is unused
}

// The implemented ISA splits the spec's single SHR into a logical SHR and a
// sign-propagating SRA, so the two have to disagree on a negative operand.
static void shifts_differ_in_what_fills_the_vacated_bits() {
    assert(kAlu.apply(AluOp::SHL, 1, 1) == 2);
    assert(kAlu.apply(AluOp::SHL, 0x80000000u, 1) == 0);
    assert(kAlu.apply(AluOp::SHR, 0x80000000u, 31) == 1);
    assert(kAlu.apply(AluOp::SRA, 0x80000000u, 31) == 0xFFFFFFFFu);
    assert(kAlu.apply(AluOp::SRA, 0x40000000u, 30) == 1);   // positive: like SHR
}

// A count of 32 or more is the host's undefined shift, so the unit has to mask
// it rather than hand it to the C++ operator.
static void a_shift_amount_is_taken_modulo_32() {
    for (AluOp op : {AluOp::SHL, AluOp::SHR, AluOp::SRA}) {
        assert(kAlu.apply(op, 0xFFu, 0) == 0xFFu);    // a zero shift is a no-op
        assert(kAlu.apply(op, 0xFFu, 32) == 0xFFu);   // and 32 wraps onto it
    }
    // 63 wraps onto 31, which no narrower mask reproduces.
    assert(kAlu.apply(AluOp::SHL, 1, 63) == 0x80000000u);
    assert(kAlu.apply(AluOp::SHR, 0x80000000u, 63) == 1);
    assert(kAlu.apply(AluOp::SRA, 0x80000000u, 63) == 0xFFFFFFFFu);
}

// The two move operations take their operand from opposite sides of the unit.
static void moves_select_the_operand_their_format_supplies() {
    assert(kAlu.apply(AluOp::PASS, 0x123u, 0x999u) == 0x123u);   // MOV forwards A
    assert(kAlu.apply(AluOp::LUI, 0, 3) == (3u << 14));          // MOVHI: B << 14
    assert(kAlu.apply(AluOp::LUI, 0, 0x3FFFFu) == 0xFFFFC000u);  // the whole field
    assert(kAlu.apply(AluOp::LUI, 0, 0x40000u) == 0);            // 18 bits of it only
}

} // namespace

int main() {
    add_and_sub_wrap_in_two_s_complement();
    the_adder_carries_across_every_block_boundary();
    the_adder_agrees_with_the_host_operators();
    logic_ops_are_bitwise();
    shifts_differ_in_what_fills_the_vacated_bits();
    a_shift_amount_is_taken_modulo_32();
    moves_select_the_operand_their_format_supplies();
    std::printf("test_alu: all checks passed\n");
    return 0;
}

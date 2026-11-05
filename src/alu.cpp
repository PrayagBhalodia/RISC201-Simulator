// alu.cpp
// One result per ALUOp row of Section 2.3. MUL, DIV and MOD live in mdu.cpp.
//
// ADD and SUB go through a two-level carry-lookahead adder written as the gate
// structure rather than as the host's `+`, the same way the MDU's algorithms
// are written as their iterations: the carry into every bit is a sum of
// products of the generates and propagates below it, not a walk up the word.

#include "alu.h"

namespace risc201 {

namespace {

constexpr int kGroup = 4;              // positions one lookahead unit covers
constexpr int kBlocks = 32 / kGroup;   // 4-bit blocks in a 32-bit word

bool bit(uint32_t w, int i) { return ((w >> i) & 1u) != 0; }

uint32_t nibble(uint32_t w, int block) { return (w >> (kGroup * block)) & 0xFu; }

// One lookahead unit over four positions. `g` and `p` hold their generates and
// propagates in bits 0-3 and `c0` is the carry entering the unit; the result
// holds the carry entering each of the four positions in bits 0-3 and the
// carry leaving the unit in bit 4. The four equations are the recurrence
// c(i+1) = g(i) + p(i).c(i) expanded into sums of products, which is what lets
// each carry settle two gate levels after its inputs instead of waiting for
// the carry below it. The unit is the same at every level of the adder.
uint32_t lookahead(uint32_t g, uint32_t p, bool c0) {
    const bool g0 = bit(g, 0), g1 = bit(g, 1), g2 = bit(g, 2), g3 = bit(g, 3);
    const bool p0 = bit(p, 0), p1 = bit(p, 1), p2 = bit(p, 2), p3 = bit(p, 3);

    const bool c1 = g0 | (p0 & c0);
    const bool c2 = g1 | (p1 & g0) | (p1 & p0 & c0);
    const bool c3 = g2 | (p2 & g1) | (p2 & p1 & g0) | (p2 & p1 & p0 & c0);
    const bool c4 = g3 | (p3 & g2) | (p3 & p2 & g1) | (p3 & p2 & p1 & g0)
                       | (p3 & p2 & p1 & p0 & c0);

    return static_cast<uint32_t>(c0)        | (static_cast<uint32_t>(c1) << 1)
         | (static_cast<uint32_t>(c2) << 2) | (static_cast<uint32_t>(c3) << 3)
         | (static_cast<uint32_t>(c4) << 4);
}

// a + b + carryIn. The per-bit generates and propagates are one pair of word
// operations, the eight block terms above them need no carry and so settle
// together, and only the carry between the two 16-bit halves is handed along.
// SUB reaches this as a + ~b + 1, which is how Section 2.3 defines it.
uint32_t addCarryLookahead(uint32_t a, uint32_t b, bool carryIn) {
    const uint32_t g = a & b;   // bit generate: this bit makes a carry
    const uint32_t p = a ^ b;   // bit propagate: this bit passes one along

    uint32_t blockG = 0, blockP = 0;
    for (int k = 0; k < kBlocks; ++k) {
        // The block's carry out with nothing coming in is its generate; a
        // block whose every bit propagates passes a carry straight through.
        if (lookahead(nibble(g, k), nibble(p, k), false) >> kGroup) blockG |= 1u << k;
        if (nibble(p, k) == 0xFu) blockP |= 1u << k;
    }

    const uint32_t lowBlocks  = lookahead(blockG & 0xFu, blockP & 0xFu, carryIn);
    const uint32_t highBlocks = lookahead(blockG >> kGroup, blockP >> kGroup,
                                          bit(lowBlocks, kGroup));

    uint32_t sum = 0;
    for (int k = 0; k < kBlocks; ++k) {
        const uint32_t into = (k < kGroup) ? lowBlocks : highBlocks;
        const uint32_t carries =
            lookahead(nibble(g, k), nibble(p, k), bit(into, k % kGroup)) & 0xFu;
        sum |= (nibble(p, k) ^ carries) << (kGroup * k);   // sum bit = p XOR carry in
    }
    return sum;
}

} // namespace

uint32_t Alu::apply(AluOp op, uint32_t a, uint32_t b) const {
    // Section 2.3 takes a shift amount modulo 32, which also keeps the host
    // shifts away from the undefined count at or beyond the operand width.
    const uint32_t sh = b & 31u;

    switch (op) {
        case AluOp::ADD:  return addCarryLookahead(a, b, false);
        case AluOp::SUB:  return addCarryLookahead(a, ~b, true);
        case AluOp::AND:  return a & b;
        case AluOp::OR:   return a | b;
        case AluOp::XOR:  return a ^ b;
        case AluOp::NOT:  return ~a;
        case AluOp::SHL:  return a << sh;
        case AluOp::SHR:  return a >> sh;
        case AluOp::SRA:  return static_cast<uint32_t>(static_cast<int32_t>(a) >> sh);
        case AluOp::LUI:  return (b & 0x3FFFFu) << 14;   // MOVHI
        case AluOp::PASS: return a;

        // MDUEn and FpuEn route these to the Mdu and the Fpu instead, so they
        // never arrive here; they are listed so the compiler keeps checking
        // this switch.
        case AluOp::MUL:  case AluOp::DIV:  case AluOp::MOD:
        case AluOp::FADD: case AluOp::FSUB: case AluOp::FMUL: case AluOp::FDIV:
        case AluOp::FEQ:  case AluOp::FGT:
        case AluOp::ITOF: case AluOp::FTOI:
            break;
    }
    return 0;
}

} // namespace risc201

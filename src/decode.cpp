// decode.cpp
// Opcode table, format extraction, sign-extension.
//
// Opcode numbering (37 instructions):
//   0x00 NOP
//   0x01 ADD  0x02 SUB  0x03 MUL  0x04 DIV  0x05 MOD
//   0x06 AND  0x07 OR   0x08 XOR  0x09 NOT  0x0A SHL  0x0B SRL  0x0C SRA
//   0x0D ADDI 0x0E SUBI 0x0F MULI 0x10 DIVI 0x11 MODI
//   0x12 LD   0x13 ST   0x14 MOV  0x15 MOVHI
//   0x16 PUSH 0x17 POP
//   0x18 CALL 0x19 RET  0x1A B
//   0x1B BEQ  0x1C BGT
//   0x1D FADD 0x1E FSUB 0x1F FMUL 0x20 FDIV
//   0x21 FEQ  0x22 FGT  0x23 ITOF 0x24 FTOI
//   0x25-0x3F  ILLEGAL

#include "decode.h"

namespace risc201 {

int32_t signExtend(uint32_t value, int bits) {
    uint32_t signBit = 1u << (bits - 1);
    uint32_t mask = (1u << bits) - 1;
    value &= mask;
    return static_cast<int32_t>((value ^ signBit) - signBit);
}

struct OpcodeEntry {
    Op opcode;
    const char* mnemonic;
    Format format;
};

static const OpcodeEntry kOpcodeTable[] = {
    // N-format
    {Op::NOP, "NOP",  Format::N},
    // R-format arithmetic
    {Op::ADD, "ADD",  Format::R},
    {Op::SUB, "SUB",  Format::R},
    {Op::MUL, "MUL",  Format::R},
    {Op::DIV, "DIV",  Format::R},
    {Op::MOD, "MOD",  Format::R},
    // R-format logic / shift
    {Op::AND, "AND",  Format::R},
    {Op::OR, "OR",   Format::R},
    {Op::XOR, "XOR",  Format::R},
    {Op::NOT, "NOT",  Format::R},
    {Op::SHL, "SHL",  Format::R},
    {Op::SRL, "SRL",  Format::R},  // logical shift right
    {Op::SRA, "SRA",  Format::R},  // arithmetic shift right
    // I-format arithmetic
    {Op::ADDI, "ADDI", Format::I},
    {Op::SUBI, "SUBI", Format::I},
    {Op::MULI, "MULI", Format::I},
    {Op::DIVI, "DIVI", Format::I},
    {Op::MODI, "MODI", Format::I},
    // I-format memory / data-move
    {Op::LD, "LD",   Format::I},
    {Op::ST, "ST",   Format::I},
    {Op::MOV, "MOV",  Format::I},
    {Op::MOVHI, "MOVHI",Format::I},
    // S-format stack
    {Op::PUSH, "PUSH", Format::S},
    {Op::POP, "POP",  Format::S},
    // J-format / N-format control
    {Op::CALL, "CALL", Format::J},
    {Op::RET, "RET",  Format::N},
    {Op::B, "B",    Format::J},
    // B-format conditional branches
    {Op::BEQ, "BEQ",  Format::B},
    {Op::BGT, "BGT",  Format::B},
    // Floating point (Section 2.10); ITOF and FTOI leave rs2 unused.
    {Op::FADD, "FADD", Format::R},
    {Op::FSUB, "FSUB", Format::R},
    {Op::FMUL, "FMUL", Format::R},
    {Op::FDIV, "FDIV", Format::R},
    {Op::FEQ, "FEQ",  Format::R},
    {Op::FGT, "FGT",  Format::R},
    {Op::ITOF, "ITOF", Format::R},
    {Op::FTOI, "FTOI", Format::R},
    // 0x25-0x3F: reserved / illegal
};

Format formatForOpcode(uint8_t opcode) {
    for (const auto& e : kOpcodeTable) {
        if (e.opcode == static_cast<Op>(opcode)) return e.format;
    }
    return Format::UNKNOWN; // 0x25-0x3F: reserved, illegal-opcode exception
}

std::string mnemonicForOpcode(uint8_t opcode) {
    for (const auto& e : kOpcodeTable) {
        if (e.opcode == static_cast<Op>(opcode)) return e.mnemonic;
    }
    return "ILLEGAL";
}

DecodedInstruction decode(uint32_t word) {
    DecodedInstruction d;
    d.raw = word;
    d.opcode = static_cast<uint8_t>((word >> 26) & 0x3F);
    d.format = formatForOpcode(d.opcode);
    d.mnemonic = mnemonicForOpcode(d.opcode);

    switch (d.format) {
        case Format::R:
            d.rd  = static_cast<uint8_t>((word >> 22) & 0xF);
            d.rs1 = static_cast<uint8_t>((word >> 18) & 0xF);
            d.rs2 = static_cast<uint8_t>((word >> 14) & 0xF);
            break;
        case Format::I:
            // Note: for ST, the rd-field position carries the source register
            // per Section 2.2's encoding note.
            d.rd  = static_cast<uint8_t>((word >> 22) & 0xF);
            d.rs1 = static_cast<uint8_t>((word >> 18) & 0xF);
            d.imm = signExtend(word & 0x3FFFF, 18);
            break;
        case Format::S:
            // rs1 field doubles as POP's destination register. Bit 21 selects
            // LR instead, which is what makes nested calls possible: LR is
            // outside the register file, so PUSH/POP are its only path to
            // memory. Zero there keeps every older encoding valid.
            d.rs1 = static_cast<uint8_t>((word >> 22) & 0xF);
            d.useLR = ((word >> 21) & 1u) != 0;
            break;
        case Format::B:
            d.rs1 = static_cast<uint8_t>((word >> 22) & 0xF);
            d.rs2 = static_cast<uint8_t>((word >> 18) & 0xF);
            d.imm = signExtend(word & 0x3FFFF, 18);
            break;
        case Format::J:
            d.jOffset = signExtend(word & 0x3FFFFFF, 26);
            break;
        case Format::N:
        case Format::UNKNOWN:
        default:
            break; // no operand fields
    }
    return d;
}

} // namespace risc201

// decode.h
// Instruction formats and the opcode map: 37 instructions, 0x25-0x3F illegal.
// A program stops when the PC leaves its own code, so the ISA spends no opcode
// on halting.

#pragma once
#include <cstdint>
#include <string>

namespace risc201 {

// The single spelling of every opcode number; kOpcodeTable is built from these.
enum class Op : uint8_t {
    NOP  = 0x00,
    ADD  = 0x01, SUB  = 0x02, MUL  = 0x03, DIV  = 0x04, MOD = 0x05,
    AND  = 0x06, OR   = 0x07, XOR  = 0x08, NOT  = 0x09,
    SHL  = 0x0A, SRL  = 0x0B, SRA  = 0x0C,
    ADDI = 0x0D, SUBI = 0x0E, MULI = 0x0F, DIVI = 0x10, MODI = 0x11,
    LD   = 0x12, ST   = 0x13, MOV  = 0x14, MOVHI = 0x15,
    PUSH = 0x16, POP  = 0x17,
    CALL = 0x18, RET  = 0x19, B    = 0x1A,
    BEQ  = 0x1B, BGT  = 0x1C,

    // Floating point, Section 2.10. Single precision, same R format.
    FADD = 0x1D, FSUB = 0x1E, FMUL = 0x1F, FDIV = 0x20,
    FEQ  = 0x21, FGT  = 0x22,
    ITOF = 0x23, FTOI = 0x24,
};

enum class Format {
    R,       // the integer and the floating-point register operations
    I,       // ADDI, SUBI, MULI, DIVI, MODI, LD, ST, MOV, MOVHI
    S,       // PUSH, POP
    B,       // BEQ, BGT
    J,       // B, CALL
    N,       // NOP, RET
    UNKNOWN  // unassigned opcode (0x25-0x3F) -> illegal-opcode exception
};

// Raw fields pulled out of the 32-bit instruction word before format-specific
// interpretation. Not every field is meaningful for every format; see
// Section 2.2's format table for which fields apply to which mnemonics.
struct DecodedInstruction {
    uint32_t raw = 0;
    uint8_t opcode = 0;      // bits [31:26]
    Format format = Format::UNKNOWN;
    std::string mnemonic = "UNKNOWN";

    uint8_t rd = 0;          // R/I-type destination (or ST's source register)
    uint8_t rs1 = 0;         // R/I/S/B-type first register
    uint8_t rs2 = 0;         // R/B-type second register
    int32_t imm = 0;         // sign-extended 18-bit immediate/offset (I/B-type)
    int32_t jOffset = 0;     // sign-extended 26-bit offset (J-type)
    bool useLR = false;      // S-type bit 21: operand is LR, not the rs1 GPR
};

// Looks up format + mnemonic for a 6-bit opcode. Returns Format::UNKNOWN /
// mnemonic "ILLEGAL" for opcodes 0x25-0x3F (reserved / illegal).
Format formatForOpcode(uint8_t opcode);
std::string mnemonicForOpcode(uint8_t opcode);

// Sign-extends an n-bit field (given as the low n bits of `value`) to int32_t.
int32_t signExtend(uint32_t value, int bits);

// Full decode: extracts opcode + format-specific fields from a raw word.
DecodedInstruction decode(uint32_t instructionWord);

} // namespace risc201

// disasm.cpp
// Converts a raw RISC201 instruction word into a human-readable string.
// Canonical ISA: 37 instructions.
// Reference: RISC201/Assembler/isa.cpp, RISC201/Disassembler/disassembler.cpp.

#include "disasm.h"
#include <iomanip>
#include <sstream>

namespace risc201 {

// ── Register naming ───────────────────────────────────────────────────────

std::string regName(uint8_t index, bool longForm) {
    // R0 is hardwired zero. SP and LR live outside the register file, so
    // R14 and R15 are ordinary general-purpose registers.
    if (index == 0) return longForm ? "R0(zero)" : "R0";
    return "R" + std::to_string(index);
}

// ── Pretty immediate helpers ──────────────────────────────────────────────

static std::string fmtImm(int32_t imm) {
    // Show both decimal and hex if non-trivial
    std::ostringstream oss;
    if (imm >= 0)
        oss << "#" << imm << "  (0x" << std::hex << std::uppercase << imm << ")";
    else
        oss << "#" << imm << "  (-0x" << std::hex << std::uppercase << (-imm) << ")";
    return oss.str();
}

static std::string fmtOffset(int32_t off, uint32_t base) {
    // Branch offsets: show signed decimal and the resolved target address
    uint32_t target = static_cast<uint32_t>(static_cast<int32_t>(base) + off);
    std::ostringstream oss;
    oss << (off >= 0 ? "+" : "") << std::dec << off
        << "  -> 0x" << std::hex << std::uppercase
        << std::setw(8) << std::setfill('0') << target;
    return oss.str();
}

// ── Core disassembly ──────────────────────────────────────────────────────

std::string disassemble(uint32_t addr, const DecodedInstruction& d) {
    std::ostringstream oss;

    // Column 1: address
    oss << "0x" << std::hex << std::uppercase
        << std::setw(8) << std::setfill('0') << addr << ":  ";

    // Column 2: raw word
    oss << "0x" << std::setw(8) << d.raw << "   ";

    // Column 3: mnemonic + operands
    oss << std::dec << std::setfill(' ');
    oss << std::left << std::setw(8) << d.mnemonic;

    switch (d.format) {
        case Format::R:
            // NOT, ITOF and FTOI are two-operand: rs2 unused, encoded as 0
            if (d.mnemonic == "NOT" || d.mnemonic == "ITOF" || d.mnemonic == "FTOI")
                oss << regName(d.rd) << ", " << regName(d.rs1);
            else
                // ADD/SUB/MUL/DIV/MOD/AND/OR/XOR/SHL/SRL/SRA: Rd, Rs1, Rs2
                oss << regName(d.rd) << ", "
                    << regName(d.rs1) << ", "
                    << regName(d.rs2);
            break;

        case Format::I:
            if (d.mnemonic == "LD")
                // LD Rd, [Rs1 + imm]
                oss << regName(d.rd) << ", [" << regName(d.rs1)
                    << " + " << d.imm << "]";
            else if (d.mnemonic == "ST")
                // ST [Rs1 + imm], Rd  (rd field is the source)
                oss << "[" << regName(d.rs1) << " + " << d.imm << "], "
                    << regName(d.rd);
            else if (d.mnemonic == "MOV")
                oss << regName(d.rd) << ", " << regName(d.rs1);
            else if (d.mnemonic == "MOVHI")
                oss << regName(d.rd) << ", #0x"
                    << std::hex << std::uppercase
                    << (static_cast<uint32_t>(d.imm) & 0x3FFFF);
            else
                // ADDI / SUBI / MULI / DIVI / MODI: Rd, Rs1, #imm
                oss << regName(d.rd) << ", "
                    << regName(d.rs1) << ", "
                    << fmtImm(d.imm);
            break;

        case Format::S:
            // PUSH / POP take one operand: the rs1 GPR, or LR when bit 21 is set
            oss << (d.useLR ? "LR" : regName(d.rs1));
            break;

        case Format::B:
            // BEQ / BGT  Rs1, Rs2, offset
            // ISA: PC <- PC+4 + sext(offset)   [byte offset, no *4 scaling]
            oss << regName(d.rs1) << ", "
                << regName(d.rs2) << ", "
                << fmtOffset(d.imm, addr + 4);
            break;

        case Format::J:
            // B / CALL  offset
            // ISA: PC <- PC+4 + sext(offset)   [byte offset, no *4 scaling]
            oss << fmtOffset(d.jOffset, addr + 4);
            break;

        case Format::N:
            // NOP / RET - no operands
            break;

        case Format::UNKNOWN:
        default:
            oss << "??? (illegal opcode 0x"
                << std::hex << std::uppercase
                << static_cast<int>(d.opcode) << ")";
            break;
    }

    return oss.str();
}

std::string disassemble(uint32_t addr, uint32_t word) {
    DecodedInstruction d = decode(word);
    return disassemble(addr, d);
}

} // namespace risc201

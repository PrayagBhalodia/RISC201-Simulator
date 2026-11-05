// control.h
// Horizontal microprogrammed control unit (Section 6).
//
// One control word per opcode, read from a 64-entry control store addressed by
// the opcode itself: no mapping ROM and no micro-sequencer, because every
// RISC201 instruction needs exactly one unencoded word covering all the stages
// it passes through (Section 6.1). Each pipeline projects that word onto its
// own stages to get the instruction's microroutine; see the microcode header
// in src/pipeline4/ and src/pipeline6/.

#pragma once
#include "decode.h"
#include <cstdint>
#include <string>

namespace risc201 {

// ALUOp, Section 6.2. The codes follow the opcode order, and an immediate form
// reuses its register form's code (ADDI = ADD) with ALUSrcB = IMM. SRA takes
// the first code the spec leaves reserved, because the implemented ISA splits
// the spec's single SHR into SRL and SRA.
// The top half (bit 4 set) is the FPU's, reached when FpuEn = 1 (Section 6.2).
enum class AluOp : uint8_t {
    ADD = 0x0, SUB = 0x1, MUL = 0x2, DIV = 0x3, MOD = 0x4,
    AND = 0x5, OR = 0x6, XOR = 0x7, NOT = 0x8, SHL = 0x9,
    SHR = 0xA, LUI = 0xB, PASS = 0xC, SRA = 0xD,

    FADD = 0x10, FSUB = 0x11, FMUL = 0x12, FDIV = 0x13,
    FEQ = 0x14, FGT = 0x15, ITOF = 0x16, FTOI = 0x17,
};

enum class SrcB : uint8_t { RS2 = 0, IMM = 1, FOUR = 2 };        // ALUSrcB
enum class WbSrc : uint8_t { EXEC = 0, MEM = 1, PC4 = 2 };       // WBSrc
enum class PcSel : uint8_t { NEXT = 0, TARGET = 1, LINK = 2 };
enum class CondSel : uint8_t { ALWAYS = 0, EQ = 1, GT = 2 };

// The 14 unencoded fields of Section 6.2, 22 bits in total. Nothing here needs
// decoding before it drives the datapath - that is what makes it horizontal.
struct Microword {
    bool regWrite = false;
    bool regDstLR = false;      // RegDst: 1 selects LR (CALL only)
    bool aluSrcASP = false;     // ALUSrcA: 1 selects SP (PUSH/POP only)
    SrcB aluSrcB = SrcB::RS2;
    AluOp aluOp = AluOp::ADD;
    bool memRead = false;
    bool memWrite = false;
    bool memAddrSP = false;     // MemAddrSrc: 1 uses SP directly (POP only)
    WbSrc wbSrc = WbSrc::EXEC;
    bool spWrite = false;
    PcSel pcSel = PcSel::NEXT;
    CondSel condSel = CondSel::ALWAYS;
    bool mduEn = false;
    // FpuEn, like MDUEn, could be decoded from ALUOp; a horizontal word keeps
    // it a line of its own so the stall logic needs no decoder (Section 6.2).
    bool fpuEn = false;

    // Opcodes 0x25-0x3F hold the reserved entry, which drives no control line
    // and raises an illegal-opcode exception in decode (Section 6.1).
    bool reserved = false;

    // The 22-bit control store word, fields packed MSB-first in the order of
    // Section 6.2. The spec's worked examples in 6.4 are the test vectors.
    uint32_t encode() const;
};

// The 14 control lines of Section 6.2 as a set, so a pipeline stage can name
// the lines it drives. Section 6.5 assigns every line to exactly one stage.
enum class Field : int {
    RegWrite, RegDst, ALUSrcA, ALUSrcB, ALUOp, MemRead, MemWrite,
    MemAddrSrc, WBSrc, SPWrite, PCSel, CondSel, MDUEn, FpuEn,
};
constexpr int kFieldCount = 14;

using FieldSet = uint16_t;
constexpr FieldSet fieldBit(Field f) {
    return static_cast<FieldSet>(1u << static_cast<int>(f));
}
constexpr FieldSet kAllFields = static_cast<FieldSet>((1u << kFieldCount) - 1u);

// `m` restricted to `fields`: every line outside the set reads back idle. One
// stage's microinstruction is the control word projected onto its own lines.
Microword project(const Microword& m, FieldSet fields);

// "RegWrite=1  ALUOp=ADD" for the lines in `fields` that are active, or "" when
// the stage drives nothing for this instruction.
std::string fieldsText(const Microword& m, FieldSet fields);

// The control store (Section 6.1): 64 entries, addressed by the opcode field
// of IR. Only the low 6 bits of `opcode` are used.
const Microword& controlWord(uint8_t opcode);

} // namespace risc201

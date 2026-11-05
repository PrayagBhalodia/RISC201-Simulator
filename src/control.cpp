// control.cpp
// The control store of Section 6.1 and the per-instruction control of 6.3.
//
// The store is built once and addressed by opcode. Anything not filled in
// below keeps the reserved entry, so no control line is ever left undefined
// and the 26 unused opcodes all raise an illegal-opcode exception.

#include "control.h"
#include <array>
#include <sstream>

namespace risc201 {

namespace {

// Appends `field` to the low end of `word`, MSB-first packing per Section 6.2.
uint32_t append(uint32_t word, uint32_t field, int bits) {
    const uint32_t mask = (1u << bits) - 1u;
    return (word << bits) | (field & mask);
}

// A plain ALU instruction writing rd from the execute-stage result.
Microword alu(AluOp op, SrcB b) {
    Microword m;
    m.regWrite = true;
    m.aluOp = op;
    m.aluSrcB = b;
    return m;
}

// The MDU instructions differ from `alu` only in holding the execute stage.
Microword mdu(AluOp op, SrcB b) {
    Microword m = alu(op, b);
    m.mduEn = true;
    return m;
}

// A floating-point instruction: shaped like `alu`, but FpuEn sends the
// operands to the FPU and ALUOp is read from its half of the field.
Microword fpu(AluOp op) {
    Microword m = alu(op, SrcB::RS2);
    m.fpuEn = true;
    return m;
}

// A control transfer: PCSel = target with the comparator choice in CondSel.
Microword branch(CondSel cond) {
    Microword m;
    m.pcSel = PcSel::TARGET;
    m.condSel = cond;
    return m;
}

const char* aluOpName(AluOp op) {
    switch (op) {
        case AluOp::ADD: return "ADD";   case AluOp::SUB: return "SUB";
        case AluOp::MUL: return "MUL";   case AluOp::DIV: return "DIV";
        case AluOp::MOD: return "MOD";   case AluOp::AND: return "AND";
        case AluOp::OR:  return "OR";    case AluOp::XOR: return "XOR";
        case AluOp::NOT: return "NOT";   case AluOp::SHL: return "SHL";
        case AluOp::SHR: return "SHR";   case AluOp::LUI: return "LUI";
        case AluOp::PASS: return "PASS"; case AluOp::SRA: return "SRA";
        case AluOp::FADD: return "FADD"; case AluOp::FSUB: return "FSUB";
        case AluOp::FMUL: return "FMUL"; case AluOp::FDIV: return "FDIV";
        case AluOp::FEQ:  return "FEQ";  case AluOp::FGT:  return "FGT";
        case AluOp::ITOF: return "ITOF";
        case AluOp::FTOI: return "FTOI";
    }
    return "?";
}

const char* condSelName(CondSel c) {
    switch (c) {
        case CondSel::ALWAYS: return "PCSel=target";
        case CondSel::EQ:     return "PCSel=target if EQ";
        case CondSel::GT:     return "PCSel=target if GT";
    }
    return "?";
}

// Whether printing ALUOp tells the reader anything. It names the operation for
// the ALU and for the FPU, but an MDU instruction's code only repeats what
// MDUEn = 1 already says, and an idle execute stage has no operation at all.
bool namesTheOperation(const Microword& m) {
    if (m.fpuEn) return true;
    if (m.mduEn) return false;
    return (m.regWrite && m.wbSrc == WbSrc::EXEC) || m.memRead || m.memWrite ||
           m.spWrite;
}

std::array<Microword, 64> buildControlStore() {
    std::array<Microword, 64> store;
    for (Microword& m : store) m.reserved = true;

    auto at = [&store](Op op) -> Microword& {
        return store[static_cast<size_t>(op)];
    };
    auto put = [&at](Op op, Microword m) {
        m.reserved = false;
        at(op) = m;
    };

    put(Op::NOP, Microword{});                  // every line idle, PC + 4

    put(Op::ADD, alu(AluOp::ADD, SrcB::RS2));
    put(Op::SUB, alu(AluOp::SUB, SrcB::RS2));
    put(Op::MUL, mdu(AluOp::MUL, SrcB::RS2));
    put(Op::DIV, mdu(AluOp::DIV, SrcB::RS2));
    put(Op::MOD, mdu(AluOp::MOD, SrcB::RS2));

    put(Op::AND, alu(AluOp::AND, SrcB::RS2));
    put(Op::OR,  alu(AluOp::OR,  SrcB::RS2));
    put(Op::XOR, alu(AluOp::XOR, SrcB::RS2));
    put(Op::NOT, alu(AluOp::NOT, SrcB::RS2));
    put(Op::SHL, alu(AluOp::SHL, SrcB::RS2));
    put(Op::SRL, alu(AluOp::SHR, SrcB::RS2));
    put(Op::SRA, alu(AluOp::SRA, SrcB::RS2));

    put(Op::ADDI, alu(AluOp::ADD, SrcB::IMM));
    put(Op::SUBI, alu(AluOp::SUB, SrcB::IMM));
    put(Op::MULI, mdu(AluOp::MUL, SrcB::IMM));
    put(Op::DIVI, mdu(AluOp::DIV, SrcB::IMM));
    put(Op::MODI, mdu(AluOp::MOD, SrcB::IMM));

    Microword ld = alu(AluOp::ADD, SrcB::IMM);  // rs1 + imm is the address
    ld.memRead = true;
    ld.wbSrc = WbSrc::MEM;
    put(Op::LD, ld);

    Microword st;                               // same address, no write-back
    st.aluSrcB = SrcB::IMM;
    st.memWrite = true;
    put(Op::ST, st);

    put(Op::MOV, alu(AluOp::PASS, SrcB::IMM));  // PASS ignores the B operand
    put(Op::MOVHI, alu(AluOp::LUI, SrcB::IMM));

    Microword push;                             // SP - 4, then store there
    push.aluSrcASP = true;
    push.aluSrcB = SrcB::FOUR;
    push.aluOp = AluOp::SUB;
    push.memWrite = true;
    push.spWrite = true;
    put(Op::PUSH, push);

    Microword pop = alu(AluOp::ADD, SrcB::FOUR); // read at the old SP, then SP + 4
    pop.aluSrcASP = true;
    pop.memRead = true;
    pop.memAddrSP = true;
    pop.wbSrc = WbSrc::MEM;
    pop.spWrite = true;
    put(Op::POP, pop);

    Microword call = branch(CondSel::ALWAYS);
    call.regWrite = true;
    call.regDstLR = true;                        // the destination is LR
    call.wbSrc = WbSrc::PC4;
    put(Op::CALL, call);

    Microword ret;
    ret.pcSel = PcSel::LINK;
    put(Op::RET, ret);

    put(Op::B,   branch(CondSel::ALWAYS));
    put(Op::BEQ, branch(CondSel::EQ));
    put(Op::BGT, branch(CondSel::GT));

    put(Op::FADD, fpu(AluOp::FADD));
    put(Op::FSUB, fpu(AluOp::FSUB));
    put(Op::FMUL, fpu(AluOp::FMUL));
    put(Op::FDIV, fpu(AluOp::FDIV));
    // The comparisons write an integer 0 or 1, so they are ordinary register
    // writes; only the operands are floating point (Section 2.10).
    put(Op::FEQ, fpu(AluOp::FEQ));
    put(Op::FGT, fpu(AluOp::FGT));
    put(Op::ITOF, fpu(AluOp::ITOF));
    put(Op::FTOI, fpu(AluOp::FTOI));

    return store;
}

} // namespace

uint32_t Microword::encode() const {
    uint32_t w = 0;
    w = append(w, regWrite, 1);
    w = append(w, regDstLR, 1);
    w = append(w, aluSrcASP, 1);
    w = append(w, static_cast<uint32_t>(aluSrcB), 2);
    w = append(w, static_cast<uint32_t>(aluOp), 5);
    w = append(w, memRead, 1);
    w = append(w, memWrite, 1);
    w = append(w, memAddrSP, 1);
    w = append(w, static_cast<uint32_t>(wbSrc), 2);
    w = append(w, spWrite, 1);
    w = append(w, static_cast<uint32_t>(pcSel), 2);
    w = append(w, static_cast<uint32_t>(condSel), 2);
    w = append(w, mduEn, 1);
    w = append(w, fpuEn, 1);
    return w;
}

Microword project(const Microword& m, FieldSet fields) {
    Microword out;
    out.reserved = m.reserved;
    const auto has = [fields](Field f) { return (fields & fieldBit(f)) != 0; };
    if (has(Field::RegWrite))   out.regWrite   = m.regWrite;
    if (has(Field::RegDst))     out.regDstLR   = m.regDstLR;
    if (has(Field::ALUSrcA))    out.aluSrcASP  = m.aluSrcASP;
    if (has(Field::ALUSrcB))    out.aluSrcB    = m.aluSrcB;
    if (has(Field::ALUOp))      out.aluOp      = m.aluOp;
    if (has(Field::MemRead))    out.memRead    = m.memRead;
    if (has(Field::MemWrite))   out.memWrite   = m.memWrite;
    if (has(Field::MemAddrSrc)) out.memAddrSP  = m.memAddrSP;
    if (has(Field::WBSrc))      out.wbSrc      = m.wbSrc;
    if (has(Field::SPWrite))    out.spWrite    = m.spWrite;
    if (has(Field::PCSel))      out.pcSel      = m.pcSel;
    if (has(Field::CondSel))    out.condSel    = m.condSel;
    if (has(Field::MDUEn))      out.mduEn      = m.mduEn;
    if (has(Field::FpuEn))      out.fpuEn      = m.fpuEn;
    return out;
}

std::string fieldsText(const Microword& m, FieldSet fields) {
    const Microword p = project(m, fields);
    std::ostringstream oss;
    const auto add = [&oss](const char* text) {
        if (oss.tellp() > 0) oss << "  ";
        oss << text;
    };
    if (p.regWrite)  add(p.regDstLR ? "RegWrite=1(LR)" : "RegWrite=1");
    if (p.aluSrcASP) add("ALUSrcA=SP");
    if (p.aluSrcB == SrcB::IMM)  add("ALUSrcB=imm");
    if (p.aluSrcB == SrcB::FOUR) add("ALUSrcB=4");
    if ((fields & fieldBit(Field::ALUOp)) && namesTheOperation(m)) {
        oss << (oss.tellp() > 0 ? "  " : "") << "ALUOp=" << aluOpName(p.aluOp);
    }
    if (p.mduEn)     add("MDUEn=1");
    if (p.fpuEn)     add("FpuEn=1");
    if (p.memRead)   add(p.memAddrSP ? "MemRead=1(SP)" : "MemRead=1");
    if (p.memWrite)  add("MemWrite=1");
    if (p.wbSrc == WbSrc::MEM) add("WBSrc=mem");
    if (p.wbSrc == WbSrc::PC4) add("WBSrc=PC+4");
    if (p.spWrite)   add("SPWrite=1");
    if (p.pcSel == PcSel::TARGET) add(condSelName(p.condSel));
    if (p.pcSel == PcSel::LINK)   add("PCSel=LR");
    if ((fields & fieldBit(Field::CondSel)) && !(fields & fieldBit(Field::PCSel)) &&
        m.pcSel == PcSel::TARGET && m.condSel != CondSel::ALWAYS) {
        oss << (oss.tellp() > 0 ? "  " : "") << condSelName(m.condSel);
    }
    return oss.str();
}

const Microword& controlWord(uint8_t opcode) {
    static const std::array<Microword, 64> kStore = buildControlStore();
    return kStore[opcode & 0x3Fu];
}

} // namespace risc201

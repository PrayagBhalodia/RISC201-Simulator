// isa.cpp - opcode table and helper functions shared by the assembler and disassembler
#include "isa.h"
#include <cctype>

OpInfo opTable[NUM_OPS] = {
    {"NOP",0x00,'N'},
    {"ADD",0x01,'R'}, {"SUB",0x02,'R'}, {"MUL",0x03,'R'}, {"DIV",0x04,'R'}, {"MOD",0x05,'R'},
    {"AND",0x06,'R'}, {"OR",0x07,'R'},  {"XOR",0x08,'R'}, {"NOT",0x09,'R'},
    {"SHL",0x0A,'R'}, {"SRL",0x0B,'R'}, {"SRA",0x0C,'R'},
    {"ADDI",0x0D,'I'}, {"SUBI",0x0E,'I'}, {"MULI",0x0F,'I'}, {"DIVI",0x10,'I'}, {"MODI",0x11,'I'},
    {"LD",0x12,'I'}, {"ST",0x13,'I'}, {"MOV",0x14,'I'}, {"MOVHI",0x15,'I'},
    {"PUSH",0x16,'S'}, {"POP",0x17,'S'},
    {"CALL",0x18,'J'}, {"RET",0x19,'N'}, {"B",0x1A,'J'},
    {"BEQ",0x1B,'B'}, {"BGT",0x1C,'B'},
    {"FADD",0x1D,'R'}, {"FSUB",0x1E,'R'}, {"FMUL",0x1F,'R'}, {"FDIV",0x20,'R'},
    {"FEQ",0x21,'R'}, {"FGT",0x22,'R'},
    {"ITOF",0x23,'R'}, {"FTOI",0x24,'R'}
};

// Linear search by mnemonic. Returns the index into opTable or -1.
int findByName(string name) {
    for (int i = 0; i < NUM_OPS; i++) {
        if (opTable[i].name == name) return i;
    }
    return -1;
}

// Linear search by opcode. Returns -1 for unused opcodes (0x25-0x3F).
int findByOpcode(int opcode) {
    for (int i = 0; i < NUM_OPS; i++) {
        if (opTable[i].opcode == opcode) return i;
    }
    return -1;
}

// Accepts "R0".."R15" (R or r) with nothing after it. Returns -1 otherwise.
int parseReg(string s) {
    if (s.size() < 2 || s.size() > 3) return -1;
    if (s[0] != 'R' && s[0] != 'r') return -1;
    for (size_t i = 1; i < s.size(); i++) {
        if (!isdigit((unsigned char)s[i])) return -1;
    }
    if (s.size() == 3 && s[1] == '0') return -1;   // reject "R01", "R00"
    int n = (s[1] - '0');
    if (s.size() == 3) n = n * 10 + (s[2] - '0');
    if (n > 15) return -1;
    return n;
}

// Treats the low 'bits' bits of v as a signed number and extends it to 32 bits.
int32_t signExtend(uint32_t v, int bits) {
    if (bits >= 32) return (int32_t)v;
    v = v & ((1u << bits) - 1);
    if ((v >> (bits - 1)) & 1) v |= ~0u << bits;
    return (int32_t)v;
}

// True if v fits in a signed field of the given width.
bool fitsSigned(long long v, int bits) {
    long long lo = -(1LL << (bits - 1));
    long long hi = (1LL << (bits - 1)) - 1;
    return v >= lo && v <= hi;
}

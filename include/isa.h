#ifndef ISA_H
#define ISA_H
#include <string>
#include <cstdint>
using namespace std;

struct OpInfo {
    string name;   // "ADD"
    int opcode;    // 0x01
    char fmt;      // 'R', 'I', 'S', 'B', 'J', 'N'
};

const int NUM_OPS = 37;
extern OpInfo opTable[NUM_OPS];

int findByName(string name);      // index into opTable, or -1
int findByOpcode(int opcode);     // index into opTable, or -1
int parseReg(string s);           // "R0".."R15" -> 0..15, else -1
int32_t signExtend(uint32_t v, int bits);
bool fitsSigned(long long v, int bits);
#endif

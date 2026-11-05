// assembler.cpp - two-pass assembler for RISC201
//
// Usage:  ./asm prog.s   ->  prog.hex  (one memory image)
//
// Von Neumann memory model: code and data share one memory. The code (.text)
// starts at address 0 and the data (.data) is placed straight after the last
// instruction, so prog.hex is the whole memory image from address 0.
//
// Pass 1 splits every line into label / mnemonic / operands, works out how many
// words each line produces and records the address of every label.
// Data labels are then moved up by the size of the code.
// Pass 2 turns each line into 32-bit words using the finished label table.

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <map>
#include <string>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "isa.h"
using namespace std;

// One source line after pass 1 has split it up.
struct Line {
    int lineNo;            // line number in the .s file, for error messages
    string context;        // set when the line came out of a macro
    bool inData;           // true if the line is in the .data section
    uint32_t addr;         // byte address of the first word this line produces
                           // (for .data lines it is counted from the start of the data)
    string op;             // mnemonic or directive, e.g. "ADD", "LI", ".WORD"
    vector<string> args;   // operands, already trimmed
};

// A label and the address it stands for.
struct Symbol {
    uint32_t addr;
    bool inData;           // data labels cannot be branch targets
};

// A source line on its way to pass 1, carrying the .s line it came from. A
// line produced by a macro keeps the line number of the invocation, which is
// the one the programmer can act on.
struct SourceLine {
    string text;
    int lineNo;
    string context;        // "" for a line the programmer wrote, otherwise
                           // which macro body line produced it
};

// A .macro definition, kept as raw body text so expansion is a text
// substitution and pass 1 stays the only thing that parses instructions.
struct Macro {
    vector<string> params;
    vector<string> body;
    vector<int> bodyLine;      // where each body line was written, so a typo
                               // in the body is reported there, not at the call
};

// A macro that invokes itself has no base case the preprocessor can see, so
// expansion stops at a depth instead.
const int kMaxMacroDepth = 32;

// ---------------- small helper functions ----------------

string trim(string s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

string toUpper(string s) {
    for (size_t i = 0; i < s.size(); i++) {
        s[i] = (char)toupper((unsigned char)s[i]);
    }
    return s;
}

// A label starts with a letter or '_' and contains letters, digits and '_'.
// Register names like R5, and LR, are not allowed as labels.
bool isValidLabel(string s) {
    if (s.empty()) return false;
    if (!isalpha((unsigned char)s[0]) && s[0] != '_') return false;
    for (size_t i = 1; i < s.size(); i++) {
        if (!isalnum((unsigned char)s[i]) && s[i] != '_') return false;
    }
    return parseReg(s) == -1 && s != "LR";
}

// Reads a decimal, negative decimal or 0x hex number (input is already uppercase).
// Returns false if s is not a number.
bool parseNumber(string s, long long& value) {
    size_t i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
        neg = (s[i] == '-');
        i++;
    }
    int base = 10;
    if (i + 1 < s.size() && s[i] == '0' && s[i + 1] == 'X') {
        base = 16;
        i += 2;
    }
    if (i >= s.size()) return false;
    if (s.size() - i > 15) return false;          // far too big, avoid overflow

    long long v = 0;
    for (; i < s.size(); i++) {
        char c = s[i];
        int digit;
        if (isdigit((unsigned char)c)) digit = c - '0';
        else if (base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        v = v * base + digit;
    }
    value = neg ? -v : v;
    return true;
}

bool fits32(long long v) {
    return v >= -2147483648LL && v <= 4294967295LL;   // signed or unsigned 32-bit
}

string hex8(uint32_t w) {
    stringstream ss;
    ss << hex << uppercase << setw(8) << setfill('0') << w;
    return ss.str();
}

// ---------------- instruction encoders ----------------

uint32_t encR(int op, int rd, int rs1, int rs2) {
    return ((uint32_t)op << 26) | ((uint32_t)rd << 22) | ((uint32_t)rs1 << 18) | ((uint32_t)rs2 << 14);
}

// I format: op | rd | rs1 | imm[17:0].
// The B format has the same layout (op | rs1 | rs2 | offset[17:0]), so it uses this too.
uint32_t encI(int op, int rd, int rs1, long long imm) {
    return ((uint32_t)op << 26) | ((uint32_t)rd << 22) | ((uint32_t)rs1 << 18) | ((uint32_t)imm & 0x3FFFF);
}

// L (bit 21) names LR instead of the rs1 field; LR is outside the register
// file, so this bit is the only way PUSH/POP can reach it.
uint32_t encS(int op, int r, bool useLR) {
    return ((uint32_t)op << 26) | ((uint32_t)r << 22) | ((uint32_t)useLR << 21);
}

uint32_t encJ(int op, long long offset) {
    return ((uint32_t)op << 26) | ((uint32_t)offset & 0x3FFFFFF);
}

int opcodeOf(string name) {
    return opTable[findByName(name)].opcode;
}

bool isPseudo(string op) {
    return op == "LI" || op == "FLI" || op == "CLR" || op == "BLT" ||
           op == "BGE" || op == "BLE" || op == "PUSHM" || op == "POPM" ||
           op == "FLT";
}

// A float literal and the binary32 bits it assembles to. Turning decimal text
// into those bits is a toolchain job, so it uses the host's converter; the
// processor's own arithmetic is the software FPU of Section 2.10.
bool parseFloat(string s, uint32_t& bits) {
    const char* begin = s.c_str();
    char* end = nullptr;
    float v = strtof(begin, &end);
    if (end == begin || *end != '\0') return false;
    if (std::isinf(v)) return false;   // too large for binary32
    memcpy(&bits, &v, sizeof bits);
    return true;
}

// ---------------- the assembler ----------------

class Assembler {
public:
    Assembler();
    bool assemble(string fileName);
    bool writeHex(string fileName);
    bool writeBin(string fileName);
    void printErrors();

    // The one memory is textWords followed by dataWords.
    vector<uint32_t> textWords;   // code, from address 0
    vector<uint32_t> dataWords;   // data, from address 4 * textWords.size()

private:
    vector<Line> lines;
    map<string, Symbol> symbols;
    map<string, Macro> macros;
    int macroSerial;              // feeds \@, so a label in a macro body is
                                  // unique to each expansion
    vector<string> errors;
    int curLine;                  // line being processed, for error messages
    string curContext;            // where it came from, when that is a macro
    bool inData;                  // current section during pass 1
    uint32_t textAddr, dataAddr;  // next free address in each section

    void error(string msg);

    // preprocessor, ahead of pass 1
    bool preprocess(istream& in, vector<SourceLine>& out);
    void expand(const string& text, int lineNo, int depth, vector<SourceLine>& out);
    string substitute(const string& body, int bodyLine, const Macro& m,
                      const vector<string>& args, const string& serial);

    // pass 1
    void pass1Line(string text, int lineNo);
    void addLabel(string name);
    bool splitArgs(string rest, vector<string>& args);
    int wordCount(Line& L);

    // pass 2
    void pass2Line(Line& L);
    void doWord(Line& L);
    void doFloat(Line& L);
    void emitLoad32(int rd, uint32_t value);
    void encodeReal(Line& L);
    uint32_t encodeIType(Line& L, int op);
    void encodePseudo(Line& L);
    void encodeLI(Line& L);

    // operand helpers
    bool argCount(Line& L, int n);
    int reg(string s);
    bool value(string s, long long& v);
    bool imm18(string s, long long& v);
    void mem(string s, int& base, long long& offset);
    long long branchOffset(string s, uint32_t pc, int bits);
};

Assembler::Assembler() {
    curLine = 0;
    macroSerial = 0;
    inData = false;
    textAddr = 0;
    dataAddr = 0;
}

void Assembler::error(string msg) {
    errors.push_back("line " + to_string(curLine) + curContext + ": " + msg);
}

void Assembler::printErrors() {
    for (size_t i = 0; i < errors.size(); i++) {
        cerr << "Error, " << errors[i] << endl;
    }
    cerr << errors.size() << " error(s), no output written." << endl;
}

bool Assembler::assemble(string fileName) {
    ifstream in(fileName.c_str());
    if (!in.is_open()) {
        errors.push_back("cannot open file '" + fileName + "'");
        return false;
    }

    vector<SourceLine> source;
    if (!preprocess(in, source)) return false;
    for (size_t i = 0; i < source.size(); i++) {
        curContext = source[i].context;
        pass1Line(source[i].text, source[i].lineNo);
    }
    curContext.clear();

    // Skip pass 2 if pass 1 failed, otherwise one bad label causes many extra errors.
    if (!errors.empty()) return false;

    // Pass 1 counted data addresses from 0. The data really starts right after
    // the code, so move every data label up by the size of the code.
    map<string, Symbol>::iterator it;
    for (it = symbols.begin(); it != symbols.end(); it++) {
        if (it->second.inData) it->second.addr += textAddr;
    }

    for (size_t i = 0; i < lines.size(); i++) {
        pass2Line(lines[i]);
    }
    return errors.empty();
}

// Writes the whole memory image: the code first, then the data.
bool Assembler::writeHex(string fileName) {
    ofstream out(fileName.c_str());
    if (!out.is_open()) {
        errors.push_back("cannot write file '" + fileName + "'");
        return false;
    }
    // The loader needs the code/data split: a program ends when the PC leaves
    // the code, and the data that follows must never be fetched.
    out << "; code " << textWords.size() << "\n";
    for (size_t i = 0; i < textWords.size(); i++) {
        out << hex8(textWords[i]) << "\n";
    }
    for (size_t i = 0; i < dataWords.size(); i++) {
        out << hex8(dataWords[i]) << "\n";
    }
    return true;
}

// The same image as raw bytes, most significant first, which is the order the
// loader reads. A raw image has nowhere to put the code/data split, so a
// program with a .data section executes its data when loaded this way.
bool Assembler::writeBin(string fileName) {
    ofstream out(fileName.c_str(), ios::binary);
    if (!out.is_open()) {
        errors.push_back("cannot write file '" + fileName + "'");
        return false;
    }
    vector<uint32_t> image = textWords;
    image.insert(image.end(), dataWords.begin(), dataWords.end());
    for (size_t i = 0; i < image.size(); i++) {
        char b[4];
        b[0] = static_cast<char>((image[i] >> 24) & 0xFF);
        b[1] = static_cast<char>((image[i] >> 16) & 0xFF);
        b[2] = static_cast<char>((image[i] >>  8) & 0xFF);
        b[3] = static_cast<char>( image[i]        & 0xFF);
        out.write(b, 4);
    }
    return out.good();
}

// ================= PREPROCESSOR =================
// Macro expansion runs over the raw text before pass 1, so pass 1 and pass 2
// never learn that macros exist. Reference: ISA Section 2.13.

// The mnemonic or directive a line starts with, upper-cased, with the comment
// gone. `labels` collects any "NAME:" prefixes and `rest` the operand text.
string lineHead(const string& text, string& labels, string& rest) {
    string t = text;
    size_t semi = t.find(';');
    if (semi != string::npos) t = t.substr(0, semi);
    t = trim(toUpper(t));

    labels.clear();
    size_t colon = t.find(':');
    while (colon != string::npos) {
        labels += trim(t.substr(0, colon)) + ": ";
        t = trim(t.substr(colon + 1));
        colon = t.find(':');
    }
    size_t space = t.find_first_of(" \t");
    rest = (space == string::npos) ? "" : trim(t.substr(space));
    return t.substr(0, space);
}

// Replaces \param with its argument and \@ with the expansion's serial number.
// A \name that is not a parameter is a typo, not something to pass through.
string Assembler::substitute(const string& body, int bodyLine, const Macro& m,
                             const vector<string>& args, const string& serial) {
    string out;
    size_t i = 0;
    while (i < body.size()) {
        if (body[i] != '\\') { out += body[i++]; continue; }
        if (i + 1 < body.size() && body[i + 1] == '@') { out += serial; i += 2; continue; }

        size_t j = i + 1;
        while (j < body.size() && (isalnum((unsigned char)body[j]) || body[j] == '_')) j++;
        string name = toUpper(body.substr(i + 1, j - i - 1));
        size_t k = 0;
        while (k < m.params.size() && m.params[k] != name) k++;
        if (k == m.params.size()) {
            const int at = curLine;
            const string ctx = curContext;
            curLine = bodyLine;
            curContext.clear();
            error("'\\" + name + "' is not a parameter of this macro");
            curLine = at;
            curContext = ctx;
            return out;
        }
        out += args[k];
        i = j;
    }
    return out;
}

// Emits `text` into `out`, expanding it first if it invokes a macro.
void Assembler::expand(const string& text, int lineNo, int depth, vector<SourceLine>& out) {
    string labels, rest;
    const string head = lineHead(text, labels, rest);

    map<string, Macro>::iterator it = macros.find(head);
    if (head.empty() || it == macros.end()) {
        SourceLine sl = {text, lineNo, curContext};
        out.push_back(sl);
        return;
    }

    curLine = lineNo;
    if (depth >= kMaxMacroDepth) {
        error("macro '" + head + "' is nested more than " +
              to_string(kMaxMacroDepth) + " deep; is it recursive?");
        return;
    }
    // A label on the invocation belongs to the first instruction the macro
    // produces, so it goes out ahead of the body on a line of its own.
    if (!labels.empty()) {
        SourceLine sl = {labels, lineNo, curContext};
        out.push_back(sl);
    }

    vector<string> args;
    if (!splitArgs(rest, args)) return;
    const Macro& m = it->second;
    if (args.size() != m.params.size()) {
        error("macro '" + head + "' takes " + to_string(m.params.size()) +
              " operand(s) but got " + to_string(args.size()));
        return;
    }

    const string serial = to_string(macroSerial++);
    const string outer = curContext;
    for (size_t i = 0; i < m.body.size(); i++) {
        curContext = outer;
        const string line = substitute(m.body[i], m.bodyLine[i], m, args, serial);
        curLine = lineNo;
        curContext = " (" + head + ", macro line " + to_string(m.bodyLine[i]) + ")";
        expand(line, lineNo, depth + 1, out);
    }
    curContext = outer;
}

// Reads the whole file, takes the .macro definitions out of it and expands
// every invocation. A macro has to be defined before it is used.
bool Assembler::preprocess(istream& in, vector<SourceLine>& out) {
    vector<SourceLine> raw;
    string text;
    int lineNo = 0;
    while (getline(in, text)) {
        lineNo++;
        SourceLine sl = {text, lineNo, ""};
        raw.push_back(sl);
    }

    for (size_t i = 0; i < raw.size(); i++) {
        string labels, rest;
        const string head = lineHead(raw[i].text, labels, rest);
        curLine = raw[i].lineNo;

        if (head == ".ENDM") {
            error(".ENDM without a matching .MACRO");
            continue;
        }
        if (head != ".MACRO") {
            expand(raw[i].text, raw[i].lineNo, 0, out);
            continue;
        }

        size_t space = rest.find_first_of(" \t");
        const string name = rest.substr(0, space);
        const string plist = (space == string::npos) ? "" : trim(rest.substr(space));

        // The body is collected before the header is judged, so a rejected
        // definition still swallows its own lines instead of leaving every
        // one of them to be reported again as stray text.
        Macro m;
        size_t j = i + 1;
        bool closed = false;
        for (; j < raw.size(); j++) {
            string l2, r2;
            const string h2 = lineHead(raw[j].text, l2, r2);
            if (h2 == ".ENDM") { closed = true; break; }
            if (h2 == ".MACRO") {
                curLine = raw[j].lineNo;
                error("a macro cannot be defined inside another");
                return false;
            }
            m.body.push_back(raw[j].text);
            m.bodyLine.push_back(raw[j].lineNo);
        }
        if (!closed) {
            error("macro '" + name + "' has no matching .ENDM");
            return false;
        }
        i = j;                       // skip the body and the .ENDM

        if (!labels.empty()) {
            error(".MACRO cannot carry a label");
            continue;
        }
        if (!isValidLabel(name)) {
            error("invalid macro name '" + name + "'");
            continue;
        }
        if (findByName(name) != -1 || isPseudo(name)) {
            error("macro '" + name + "' would hide an instruction of the same name");
            continue;
        }
        if (macros.count(name)) {
            error("macro '" + name + "' is defined more than once");
            continue;
        }
        if (!plist.empty() && !splitArgs(plist, m.params)) continue;
        bool badParam = false;
        for (size_t k = 0; k < m.params.size(); k++) {
            if (!isValidLabel(m.params[k])) {
                error("invalid macro parameter '" + m.params[k] + "'");
                badParam = true;
            }
        }
        if (badParam) continue;
        macros[name] = m;
    }
    return errors.empty();
}

// ================= PASS 1 =================

void Assembler::pass1Line(string text, int lineNo) {
    curLine = lineNo;

    size_t semi = text.find(';');                // remove comment
    if (semi != string::npos) text = text.substr(0, semi);
    text = trim(toUpper(text));

    // Labels: "NAME:" at the start of the line (there may be more than one).
    size_t colon = text.find(':');
    while (colon != string::npos) {
        string name = trim(text.substr(0, colon));
        if (!isValidLabel(name)) {
            error("invalid label name '" + name + "'");
            return;
        }
        addLabel(name);
        text = trim(text.substr(colon + 1));
        colon = text.find(':');
    }
    if (text.empty()) return;

    Line L;
    L.lineNo = lineNo;
    L.context = curContext;
    size_t space = text.find_first_of(" \t");
    L.op = text.substr(0, space);
    string rest = (space == string::npos) ? "" : trim(text.substr(space));
    if (!splitArgs(rest, L.args)) return;

    if (L.op == ".TEXT" || L.op == ".DATA") {
        if (!L.args.empty()) error(L.op + " takes no operands");
        inData = (L.op == ".DATA");
        return;
    }

    L.inData = inData;
    L.addr = inData ? dataAddr : textAddr;
    int n = wordCount(L);
    if (n < 0) return;                           // error already reported
    if (inData) dataAddr += 4 * n;
    else textAddr += 4 * n;
    lines.push_back(L);
}

void Assembler::addLabel(string name) {
    if (symbols.count(name)) {
        error("label '" + name + "' is defined more than once");
        return;
    }
    Symbol s;
    s.inData = inData;
    s.addr = inData ? dataAddr : textAddr;
    symbols[name] = s;
}

// Splits "R1, R2 ,R3" into {"R1", "R2", "R3"}. Returns false on an empty operand.
bool Assembler::splitArgs(string rest, vector<string>& args) {
    if (rest.empty()) return true;
    string piece;
    stringstream ss(rest);
    while (getline(ss, piece, ',')) {
        args.push_back(trim(piece));
    }
    if (rest[rest.size() - 1] == ',') args.push_back("");   // trailing comma

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].empty()) {
            error("missing operand (check the commas)");
            return false;
        }
    }
    return true;
}

// Number of 32-bit words a line produces, or -1 on error.
int Assembler::wordCount(Line& L) {
    if (L.op == ".WORD") {
        if (L.args.empty()) {
            error(".WORD needs at least one value");
            return -1;
        }
        return (int)L.args.size();
    }
    if (L.op == ".FLOAT") {
        if (L.args.empty()) {
            error(".FLOAT needs at least one value");
            return -1;
        }
        return (int)L.args.size();
    }
    if (L.op == ".SPACE") {
        long long n = 0;
        if (!L.inData) {
            error(".SPACE is only allowed in the .data section");
            return -1;
        }
        if (L.args.size() != 1 || !parseNumber(L.args[0], n) || n < 0 || n > 1048576) {
            error(".SPACE needs one count between 0 and 1048576");
            return -1;
        }
        return (int)n;
    }
    if (L.op[0] == '.') {
        error("unknown directive '" + L.op + "'");
        return -1;
    }
    if (findByName(L.op) == -1 && !isPseudo(L.op)) {
        error("unknown instruction '" + L.op + "'");
        return -1;
    }
    if (L.inData) {
        error("instruction '" + L.op + "' is not allowed in the .data section");
        return -1;
    }
    if (L.op == "PUSHM" || L.op == "POPM") {
        if (L.args.empty()) {
            error(L.op + " needs at least one register");
            return -1;
        }
        return (int)L.args.size();
    }
    if (L.op == "LI" || L.op == "FLI" || L.op == "BGE" || L.op == "BLE") return 2;
    return 1;
}

// ================= PASS 2 =================

void Assembler::pass2Line(Line& L) {
    curLine = L.lineNo;
    curContext = L.context;
    if (L.op == ".WORD") {
        doWord(L);
    } else if (L.op == ".FLOAT") {
        doFloat(L);
    } else if (L.op == ".SPACE") {
        long long n = 0;
        parseNumber(L.args[0], n);               // already checked in pass 1
        for (long long i = 0; i < n; i++) dataWords.push_back(0);
    } else if (isPseudo(L.op)) {
        encodePseudo(L);
    } else {
        encodeReal(L);
    }
}

void Assembler::doWord(Line& L) {
    for (size_t i = 0; i < L.args.size(); i++) {
        long long v = 0;
        if (value(L.args[i], v) && !fits32(v)) {
            error("value '" + L.args[i] + "' does not fit in 32 bits");
        }
        if (L.inData) dataWords.push_back((uint32_t)v);
        else textWords.push_back((uint32_t)v);
    }
}

void Assembler::doFloat(Line& L) {
    for (size_t i = 0; i < L.args.size(); i++) {
        uint32_t bits = 0;
        if (!parseFloat(L.args[i], bits)) {
            error("bad float literal '" + L.args[i] + "'");
        }
        if (L.inData) dataWords.push_back(bits);
        else textWords.push_back(bits);
    }
}

// Encodes one of the 37 real instructions from the opcode table.
void Assembler::encodeReal(Line& L) {
    OpInfo info = opTable[findByName(L.op)];
    int op = info.opcode;
    vector<string>& a = L.args;
    uint32_t w = 0;

    if (info.fmt == 'N') {                       // NOP, RET
        if (argCount(L, 0)) w = encJ(op, 0);
    } else if (info.fmt == 'R' &&
               (L.op == "NOT" || L.op == "ITOF" || L.op == "FTOI")) {  // op rd, rs1
        if (argCount(L, 2)) {
            int rd = reg(a[0]);
            int rs1 = reg(a[1]);
            w = encR(op, rd, rs1, 0);
        }
    } else if (info.fmt == 'R') {                // ADD rd, rs1, rs2
        if (argCount(L, 3)) {
            int rd = reg(a[0]);
            int rs1 = reg(a[1]);
            int rs2 = reg(a[2]);
            w = encR(op, rd, rs1, rs2);
        }
    } else if (info.fmt == 'S') {                // PUSH rs1 / POP rd / PUSH LR
        if (argCount(L, 1)) {
            const bool useLR = a[0] == "LR";
            w = encS(op, useLR ? 0 : reg(a[0]), useLR);
        }
    } else if (info.fmt == 'B') {                // BEQ rs1, rs2, label
        if (argCount(L, 3)) {
            int rs1 = reg(a[0]);
            int rs2 = reg(a[1]);
            long long off = branchOffset(a[2], L.addr, 18);
            w = encI(op, rs1, rs2, off);
        }
    } else if (info.fmt == 'J') {                // B label / CALL label
        if (argCount(L, 1)) w = encJ(op, branchOffset(a[0], L.addr, 26));
    } else {
        w = encodeIType(L, op);
    }
    textWords.push_back(w);
}

uint32_t Assembler::encodeIType(Line& L, int op) {
    vector<string>& a = L.args;

    if (L.op == "LD" || L.op == "ST") {          // LD rd, imm(rs1) / ST rs2, imm(rs1)
        if (!argCount(L, 2)) return 0;
        int r = reg(a[0]);                       // for ST this is rs2, kept in the rd field
        int base;
        long long off;
        mem(a[1], base, off);
        return encI(op, r, base, off);
    }
    if (L.op == "MOV") {                         // MOV rd, rs1 (imm field is 0)
        if (!argCount(L, 2)) return 0;
        int rd = reg(a[0]);
        int rs1 = reg(a[1]);
        return encI(op, rd, rs1, 0);
    }
    if (L.op == "MOVHI") {                       // MOVHI rd, imm (18 bits, signed or unsigned)
        if (!argCount(L, 2)) return 0;
        int rd = reg(a[0]);
        long long v = 0;
        if (value(a[1], v) && (v < -131072 || v > 0x3FFFF)) {
            error("MOVHI immediate '" + a[1] + "' does not fit in 18 bits");
        }
        return encI(op, rd, 0, v);
    }

    // ADDI, SUBI, MULI, DIVI, MODI: rd, rs1, imm
    if (!argCount(L, 3)) return 0;
    int rd = reg(a[0]);
    int rs1 = reg(a[1]);
    long long v = 0;
    if (imm18(a[2], v) && v == 0 && (L.op == "DIVI" || L.op == "MODI")) {
        error(L.op + " with immediate 0 is a divide by zero");
    }
    return encI(op, rd, rs1, v);
}

void Assembler::encodePseudo(Line& L) {
    vector<string>& a = L.args;

    if (L.op == "LI") {
        encodeLI(L);
        return;
    }
    if (L.op == "FLI") {                         // FLI rd, 3.14159
        if (!argCount(L, 2)) return;
        int rd = reg(a[0]);
        uint32_t bits = 0;
        if (!parseFloat(a[1], bits)) {
            error("bad float literal '" + a[1] + "'");
            return;
        }
        emitLoad32(rd, bits);
        return;
    }
    if (L.op == "CLR") {                         // CLR rd -> XOR rd, rd, rd
        if (!argCount(L, 1)) return;
        int rd = reg(a[0]);
        textWords.push_back(encR(opcodeOf("XOR"), rd, rd, rd));
        return;
    }

    if (L.op == "FLT") {                         // FLT a, b -> FGT b, a
        // Not !FGT and not !FEQ: with a NaN operand every comparison is false,
        // so a negation would come out true. Exchanging the operands is exact.
        if (!argCount(L, 3)) return;
        int rd = reg(a[0]);
        int rs1 = reg(a[1]);
        int rs2 = reg(a[2]);
        textWords.push_back(encR(opcodeOf("FGT"), rd, rs2, rs1));
        return;
    }

    // PUSHM/POPM take a register list, LR included. POPM walks it backwards,
    // so both take the list in the same order and the full-descending LIFO
    // discipline is the assembler's job rather than the programmer's.
    if (L.op == "PUSHM" || L.op == "POPM") {
        const bool push = L.op == "PUSHM";
        for (size_t i = 0; i < a.size(); i++) {
            const string& r = a[push ? i : a.size() - 1 - i];
            const bool useLR = r == "LR";
            textWords.push_back(encS(opcodeOf(push ? "PUSH" : "POP"),
                                     useLR ? 0 : reg(r), useLR));
        }
        return;
    }

    // BLT, BGE, BLE: rs1, rs2, label
    if (!argCount(L, 3)) return;
    int rs1 = reg(a[0]);
    int rs2 = reg(a[1]);
    long long off = branchOffset(a[2], L.addr, 18);
    int beq = opcodeOf("BEQ");
    int bgt = opcodeOf("BGT");

    if (L.op == "BLT") {                         // BLT a, b -> BGT b, a
        textWords.push_back(encI(bgt, rs2, rs1, off));
        return;
    }

    // BGE and BLE take two words. The second branch is 4 bytes further on,
    // so its offset is 4 smaller to reach the same target.
    long long off2 = off - 4;
    if (!fitsSigned(off2, 18)) error("branch target '" + a[2] + "' is too far away");
    textWords.push_back(encI(beq, rs1, rs2, off));
    if (L.op == "BGE") textWords.push_back(encI(bgt, rs1, rs2, off2));   // BGE a, b -> BEQ a, b; BGT a, b
    else textWords.push_back(encI(bgt, rs2, rs1, off2));                 // BLE a, b -> BEQ a, b; BGT b, a
}

// LI rd, imm32 -> MOVHI rd, upper ; ADDI rd, rd, lower
void Assembler::encodeLI(Line& L) {
    if (!argCount(L, 2)) return;
    int rd = reg(L.args[0]);
    long long v = 0;
    if (!value(L.args[1], v)) return;
    if (!fits32(v)) {
        error("LI value '" + L.args[1] + "' does not fit in 32 bits");
        return;
    }

    emitLoad32(rd, (uint32_t)v);
}

// MOVHI then ADDI: the only way to get an arbitrary 32-bit pattern into a
// register. LI and FLI differ only in how that pattern was arrived at.
void Assembler::emitLoad32(int rd, uint32_t u) {
    long long upper = (u >> 14) & 0x3FFFF;       // bits 31..14
    long long lower = u & 0x3FFF;                // bits 13..0
    if (lower & 0x2000) {
        // ADDI sign-extends, so a lower part with bit 13 set would subtract.
        // Add 1 to the upper part and make the lower part negative to make up for it.
        upper = (upper + 1) & 0x3FFFF;
        lower = lower - 0x4000;
    }
    textWords.push_back(encI(opcodeOf("MOVHI"), rd, 0, upper));
    textWords.push_back(encI(opcodeOf("ADDI"), rd, rd, lower));
}

// ================= OPERAND HELPERS =================

bool Assembler::argCount(Line& L, int n) {
    if ((int)L.args.size() == n) return true;
    error(L.op + " expects " + to_string(n) + " operand(s) but got " + to_string(L.args.size()));
    return false;
}

int Assembler::reg(string s) {
    int r = parseReg(s);
    if (r < 0) {
        error("bad register '" + s + "' (use R0 to R15, or LR with PUSH/POP)");
        return 0;
    }
    return r;
}

// A number or a label (replaced by its address).
bool Assembler::value(string s, long long& v) {
    v = 0;
    if (parseNumber(s, v)) return true;
    if (!isValidLabel(s)) {
        error("bad value '" + s + "'");
        return false;
    }
    if (!symbols.count(s)) {
        error("undefined label '" + s + "'");
        return false;
    }
    v = symbols[s].addr;
    return true;
}

// An 18-bit signed immediate (-131072 to 131071).
bool Assembler::imm18(string s, long long& v) {
    if (!value(s, v)) return false;
    if (!fitsSigned(v, 18)) {
        error("immediate '" + s + "' does not fit in 18 bits (-131072 to 131071)");
        return false;
    }
    return true;
}

// Memory operand "imm(reg)". The imm part may be a number, a label or empty (= 0).
void Assembler::mem(string s, int& base, long long& offset) {
    base = 0;
    offset = 0;
    size_t open = s.find('(');
    if (open == string::npos || s[s.size() - 1] != ')') {
        error("expected memory operand imm(reg) but got '" + s + "'");
        return;
    }
    string immPart = trim(s.substr(0, open));
    string regPart = trim(s.substr(open + 1, s.size() - open - 2));
    base = reg(regPart);
    if (!immPart.empty()) imm18(immPart, offset);
}

// Byte offset for a branch at address pc: target - (pc + 4) for a label,
// or the number itself if a number is written.
long long Assembler::branchOffset(string s, uint32_t pc, int bits) {
    long long off = 0;
    if (!parseNumber(s, off)) {
        if (!isValidLabel(s)) {
            error("bad branch target '" + s + "'");
            return 0;
        }
        if (!symbols.count(s)) {
            error("undefined label '" + s + "'");
            return 0;
        }
        if (symbols[s].inData) {
            error("branch target '" + s + "' is a data label");
            return 0;
        }
        off = (long long)symbols[s].addr - ((long long)pc + 4);
    }
    if (off % 4 != 0) {
        error("branch offset " + to_string(off) + " is not a multiple of 4");
        return 0;
    }
    if (!fitsSigned(off, bits)) {
        error("branch target '" + s + "' is too far away");
        return 0;
    }
    return off;
}

// ================= MAIN =================

int main(int argc, char* argv[]) {
    string inFile;
    bool binary = false;
    int paths = 0;
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--bin") { binary = true; continue; }
        inFile = arg;
        paths++;
    }
    if (paths != 1) {
        cout << "Usage: ./asm [--bin] prog.asm" << endl;
        return 1;
    }

    // "dir/prog.s" -> "dir/prog"
    string base = inFile;
    size_t dot = base.find_last_of('.');
    size_t slash = base.find_last_of("/\\");
    if (dot != string::npos && (slash == string::npos || dot > slash)) {
        base = base.substr(0, dot);
    }
    string outFile = base + (binary ? ".bin" : ".hex");

    Assembler as;
    if (!as.assemble(inFile)) {
        as.printErrors();
        return 1;
    }
    if (!(binary ? as.writeBin(outFile) : as.writeHex(outFile))) {
        as.printErrors();
        return 1;
    }
    size_t codeWords = as.textWords.size();
    size_t dataWords = as.dataWords.size();
    cout << "Wrote " << outFile << " (" << codeWords + dataWords << " words)" << endl;
    cout << "  code: " << codeWords << " words from address 0x0" << endl;
    cout << "  data: " << dataWords << " words from address 0x" << hex << uppercase
         << codeWords * 4 << endl;
    return 0;
}

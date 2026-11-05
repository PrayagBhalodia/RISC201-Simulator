// simulator_main.cpp  –  RISC201 Interactive Simulator / Debugger  (interactive CLI)
//
// Commands
// ────────────────────────────────────────────────────────────────────
//  load <file>              load a .hex or .bin program (resets CPU first)
//  reset                    reset CPU state (keep loaded memory)
//  run  [4|6|off] [maxcyc]  run on a pipeline (6-stage unless one is already chosen)
//  step [n]                 execute n instructions (default 1)
//  disasm [addr] [n]        disassemble n words starting at addr (default: PC, 10)\n//  decode [@addr] <word>..  disassemble words given as hex or binary text
//  reg                      show all registers
//  stack [n|all]            show the live stack as ASCII call frames
//  mem  <addr> [n]          show n words of memory (default 1)
//  set  reg <Rn> <val>      write a value into a register
//  set  mem <addr> <val>    write a word into memory
//  break <addr>             set breakpoint at address
//  blist                    list all breakpoints + watchpoints
//  bdel  <id>               delete breakpoint or watchpoint by id
//  watch <addr>             watch a memory word for changes
//  info                     print simulator info (mem size, cycles, PC, status)
//  pipe <4|6|off> [mul] [div]  select the pipeline timing model and MDU algorithms
//  cycle [n]                advance the selected pipeline n clock cycles
//  ustore [op]              show the control store, or one opcode's microroutine
//  help [cmd]               show all commands, or detail for one command
//  quit                     exit
// ────────────────────────────────────────────────────────────────────

#include "debugger.h"
#include "disasm.h"
#include "loader.h"
#include "control.h"
#include "pipeline4/pipeline4.h"
#include "pipeline6/pipeline6.h"
#include "simulator.h"

#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

using namespace risc201;

// ═══════════════════════════════════════════════════════════════════════════
//  Terminal colour helpers (ANSI – gracefully degraded on old Windows consoles
//  by checking TERM or just flushing plain text if escape codes are unsupported)
// ═══════════════════════════════════════════════════════════════════════════

// Colour is for a terminal only: escape codes written into a pipe or a file
// corrupt whatever reads them, and the evaluation harness reads `stats`.
#ifdef _WIN32
#  include <io.h>
#  include <windows.h>
static bool stdoutIsTerminal() { return _isatty(_fileno(stdout)) != 0; }
static bool enableAnsi() {
    if (!stdoutIsTerminal()) return false;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode)) return false;
    return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}
#else
#  include <unistd.h>
static bool stdoutIsTerminal() { return isatty(STDOUT_FILENO) != 0; }
static bool enableAnsi() { return stdoutIsTerminal(); }
#endif

static bool gColor = false;  // set in main() after enableAnsi()
// A prompt is for someone typing. Piped input gets none, so a command's output
// starts at the beginning of a line and a parser needs no prompt-stripping.
static bool gPrompt = false;

static const char* RESET  = "\033[0m";
static const char* BOLD   = "\033[1m";
static const char* GREEN  = "\033[32m";
static const char* YELLOW = "\033[33m";
static const char* CYAN   = "\033[36m";
static const char* RED    = "\033[31m";
static const char* DIM    = "\033[2m";

static std::string col(const char* code, const std::string& s) {
    if (!gColor) return s;
    return std::string(code) + s + RESET;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Pretty-printer helpers
// ═══════════════════════════════════════════════════════════════════════════

static void printBanner() {
    std::cout << "\n";
    if (gColor) std::cout << BOLD << CYAN;
    std::cout << "    ____  _________ ______   ___   ____ ___ \n"
              << "   / __ \\/  _/ ___// ____/  |__ \\ / __ <  / \n"
              << "  / /_/ // / \\__ \\/ /       __/ // / / / /  \n"
              << " / _, _// / ___/ / /___    / __// /_/ / /   \n"
              << "/_/ |_/___//____/\\____/   /____/\\____/_/    \n";
    if (gColor) std::cout << RESET;
    std::cout << "\n"
              << col(DIM, "  RISC201 Simulator  |  Interactive CLI Debugger\n")
              << col(DIM, "  Type 'help' for available commands.\n")
              << "\n";
}

static void printSeparator(char c = '-', int w = 62) {
    std::cout << col(DIM, std::string(w, c)) << "\n";
}

// Print all 16 GPRs + PC/SP/LR in a formatted table
// How a stopped machine reads: reaching the end of a program is not a failure.
static std::string statusText(const Simulator& sim) {
    if (!sim.state().halted) return col(GREEN, "RUNNING");
    return sim.lastError().empty() ? col(GREEN, "ENDED") : col(RED, "STOPPED");
}

static void printRegisters(const Simulator& sim) {
    const State& s = sim.state();
    printSeparator();
    std::cout << col(BOLD, "  Registers\n");
    printSeparator();

    // GPRs: 4 per row
    for (int i = 0; i < 16; ++i) {
        uint32_t val = s.readGPR(i);
        std::ostringstream label;
        if (i == 0) label << "R0  (zero)";
        else        label << "R" << std::left << std::setw(9) << i;

        std::ostringstream hex;
        hex << "0x" << std::hex << std::uppercase
            << std::setw(8) << std::setfill('0') << val;

        std::string display = "  " + label.str() + " = "
                            + col(val == 0 ? DIM : GREEN, hex.str());
        std::cout << display;
        if ((i + 1) % 4 == 0) std::cout << "\n";
        else                   std::cout << "   ";
    }

    printSeparator();
    // PC, SP and LR sit outside the register file (Section 2.1).
    auto fmtSpecial = [](const std::string& name, uint32_t val) {
        std::ostringstream oss;
        oss << "  " << name << " = 0x" << std::hex << std::uppercase
            << std::setw(8) << std::setfill('0') << val;
        return oss.str();
    };
    std::cout << col(YELLOW, fmtSpecial("PC", s.PC))
              << fmtSpecial("SP", s.SP)
              << fmtSpecial("LR", s.LR) << "\n";

    if (s.halted) {
        std::cout << "  [" << statusText(sim) << "]\n";
    }
    printSeparator();
}

// The register file read as binary32 (Section 2.10). Separate from `reg`
// because a decoded float needs far more room than eight hex digits, and the
// four-column layout there should not have to shrink to make space.
static void printFloatRegisters(const Simulator& sim) {
    printSeparator();
    std::cout << col(BOLD, "  Registers as binary32\n");
    printSeparator();

    const State& s = sim.state();
    for (int i = 0; i < 16; ++i) {
        const uint32_t bits = s.readGPR(i);
        float value;
        std::memcpy(&value, &bits, sizeof value);

        std::ostringstream hex;
        hex << "0x" << std::hex << std::uppercase << std::setw(8)
            << std::setfill('0') << bits;
        std::ostringstream dec;
        // Nine significant digits is what a binary32 round-trips in, so the
        // text here names exactly one value and never rounds two together.
        dec << std::setprecision(9) << value;

        std::ostringstream name;
        name << "R" << std::left << std::setw(3) << i;
        std::cout << "  " << name.str() << "= " << col(bits == 0 ? DIM : GREEN, hex.str())
                  << " = " << std::right << std::setw(14) << dec.str();
        std::cout << ((i % 2 == 1) ? "\n" : "  ");
    }

    const FpStatus& f = sim.state().fpsr;
    std::cout << "  FPSR  = " << col(f.any() ? YELLOW : DIM, f.toString())
              << col(DIM, "   (Invalid | divide-by-Zero | Overflow | Underflow | ineXact)\n");
    printSeparator();
}

// An ASCII view of the live stack: every word between SP and the base, cut
// into call frames at the addresses where PUSH LR saved a return address.
// `budget` caps how many words are drawn; a deeper stack is elided in the
// middle so both the outermost frame and the newest one stay visible.
static void printStack(const Simulator& sim, uint32_t budget) {
    const State& s = sim.state();
    const uint32_t base  = sim.stackBase();
    const uint32_t limit = sim.stackLimit();

    auto addr8 = [](uint32_t v) {
        std::ostringstream o;
        o << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << v;
        return o.str();
    };
    auto pad = [](std::string t, size_t w) {
        if (t.size() < w) t += std::string(w - t.size(), ' ');
        return t;
    };

    // Only marks at or above SP are live; what is below has been popped.
    std::vector<uint32_t> marks;
    for (uint32_t m : sim.frameMarks()) {
        if (m >= s.SP) marks.push_back(m);
    }

    printSeparator();
    std::cout << col(BOLD, "  Stack\n");
    printSeparator();
    std::cout << "  base " << addr8(base)
              << "   SP " << col(s.SP == base ? DIM : YELLOW, addr8(s.SP))
              << "   limit " << addr8(limit) << col(DIM, "  (program image end)\n")
              << "  " << std::dec << (base - s.SP) / 4 << " word(s) in use, "
              << marks.size() << " frame(s), "
              << (s.SP > limit ? s.SP - limit : 0) << " byte(s) before overflow\n";

    if (s.SP >= base) {
        std::cout << col(DIM, "  (empty - nothing has been pushed)\n");
        printSeparator();
        return;
    }

    const std::string indent(18, ' ');
    const std::string rule = indent + "+" + std::string(37, '-') + "+";
    const uint32_t words = (base - s.SP) / 4;
    int frame = 0;

    const bool elide    = words > budget;
    const uint32_t head = elide ? budget / 2 : words;
    const uint32_t tail = elide ? words - (budget - head) : words;

    // The separator is held back until its labels are known, so a frame that
    // starts at the base shares one line with it instead of adding a stray rule.
    std::string pending = rule + col(DIM, "  base");
    for (uint32_t i = 0; i < words; ++i) {
        const uint32_t a = base - 4 - i * 4;
        // A frame begins at its saved LR, the first word a callee pushes.
        const bool isMark = std::find(marks.begin(), marks.end(), a) != marks.end();
        if (isMark) ++frame;

        if (i >= head && i < tail) {
            if (i == head) {
                if (!pending.empty()) { std::cout << pending << "\n"; pending.clear(); }
                std::cout << indent << col(DIM, "   ... " + std::to_string(tail - head) +
                                                " more word(s), 'stack all' to see them ...")
                          << "\n";
            }
            continue;
        }
        if (i == tail && elide) pending = rule;
        if (isMark) {
            if (pending.empty()) pending = rule;
            pending += col(DIM, "  frame " + std::to_string(frame));
        }
        if (!pending.empty()) {
            std::cout << pending << "\n";
            pending.clear();
        }

        uint32_t val = 0;
        try { val = sim.memory().readWord(a); } catch (const MemoryAccessError&) { break; }

        std::ostringstream note;
        if (isMark) note << "saved LR -> " << addr8(val);
        else        note << std::dec << static_cast<int32_t>(val);

        std::cout << "  " << addr8(a)
                  << (a == s.SP ? col(YELLOW, " SP ->") : std::string(6, ' '))
                  << "| " << addr8(val) << "  "
                  << (isMark ? col(CYAN, pad(note.str(), 24)) : pad(note.str(), 24))
                  << "|\n";
    }
    std::cout << rule << col(DIM, "  free space down to the image end") << "\n";
    printSeparator();
}

// A 32-bit instruction word written as text: "0x48400040", a bare hex word, a
// "0b"-prefixed binary word, or 32 bare binary digits. The shape is parsed, not
// guessed: 32 digits cannot be hex (eight is the most a word takes) and eight
// or fewer cannot be the binary spelling, so the two never collide.
static bool parseWord(const std::string& tok, uint32_t& out) {
    const auto all = [&](const std::string& t, const char* set) {
        return !t.empty() && t.find_first_not_of(set) == std::string::npos;
    };
    std::string t = tok;
    int base = 0;
    if (t.size() > 2 && (t.compare(0, 2, "0x") == 0 || t.compare(0, 2, "0X") == 0)) {
        t = t.substr(2); base = 16;
    } else if (t.size() > 2 && (t.compare(0, 2, "0b") == 0 || t.compare(0, 2, "0B") == 0)) {
        t = t.substr(2); base = 2;
    } else if (t.size() == 32 && all(t, "01")) {
        base = 2;
    } else {
        base = 16;
    }
    if (base == 2 ? !all(t, "01") : !all(t, "0123456789abcdefABCDEF")) return false;
    if (t.size() > (base == 2 ? 32u : 8u)) return false;
    out = static_cast<uint32_t>(std::stoul(t, nullptr, base));
    return true;
}

// Reconstruct instructions from words given as text rather than read out of
// memory. The words are laid out from `base` so a PC-relative target in a
// pasted sequence still points at the right member of it.
static void printDecode(const std::vector<std::string>& toks, uint32_t base) {
    printSeparator();
    std::cout << col(BOLD, "  Decode\n");
    printSeparator();
    uint32_t addr = base;
    for (const std::string& tok : toks) {
        uint32_t word = 0;
        if (!parseWord(tok, word)) {
            std::cout << "   " << col(RED, "not a 32-bit word: " + tok) << "\n";
            continue;
        }
        std::cout << "   " << disassemble(addr, word) << "\n";
        addr += 4;
    }
    printSeparator();
}

// Disassemble a range of instructions
static void printDisasm(Simulator& sim, uint32_t startAddr, int count) {
    printSeparator();
    std::cout << col(BOLD, "  Disassembly\n");
    printSeparator();
    uint32_t addr = startAddr;
    uint32_t pc   = sim.state().PC;
    for (int i = 0; i < count; ++i) {
        try {
            uint32_t word = sim.memory().readWord(addr);
            std::string line = disassemble(addr, word);
            if (addr == pc)
                std::cout << col(YELLOW, " > ") << col(BOLD, line) << "\n";
            else
                std::cout << "   " << line << "\n";
        } catch (const MemoryAccessError& e) {
            std::cout << "   0x" << std::hex << std::uppercase
                      << std::setw(8) << std::setfill('0') << addr
                      << ":  " << col(RED, std::string("(") + e.what() + ")") << "\n";
            break;
        }
        addr += 4;
    }
    printSeparator();
}

// Print memory words
static void printMemory(Simulator& sim, uint32_t startAddr, int wordCount) {
    printSeparator();
    std::cout << col(BOLD, "  Memory\n");
    printSeparator();
    for (int i = 0; i < wordCount; ++i) {
        uint32_t addr = startAddr + static_cast<uint32_t>(i * 4);
        try {
            uint32_t val = sim.memory().readWord(addr);
            std::cout << "  0x" << std::hex << std::uppercase
                      << std::setw(8) << std::setfill('0') << addr
                      << ":  0x" << std::setw(8) << val
                      << "   (" << std::dec << val << ")\n";
        } catch (const MemoryAccessError& e) {
            std::cout << "  0x" << std::hex << std::uppercase
                      << std::setw(8) << std::setfill('0') << addr
                      << ":  " << col(RED, std::string("(") + e.what() + ")") << "\n";
            break;
        }
    }
    printSeparator();
}

// Print breakpoints and watchpoints from the debugger
static void printBreakpointsAndWatches(const Debugger& dbg) {
    const auto& bps = dbg.breakpoints();
    const auto& wps = dbg.watchpoints();

    if (bps.empty() && wps.empty()) {
        std::cout << col(DIM, "  No breakpoints or watchpoints set.\n");
        return;
    }

    printSeparator();
    if (!bps.empty()) {
        std::cout << col(BOLD, "  Breakpoints\n");
        for (const auto& [id, bp] : bps) {
            std::cout << "  #" << id
                      << "  addr=0x" << std::hex << std::uppercase
                      << std::setw(8) << std::setfill('0') << bp.address
                      << "  hits=" << std::dec << bp.hitCount
                      << "  " << (bp.enabled ? col(GREEN,"[enabled]") : col(DIM,"[disabled]"))
                      << "\n";
        }
    }
    if (!wps.empty()) {
        std::cout << col(BOLD, "  Watchpoints\n");
        for (const auto& [id, wp] : wps) {
            std::cout << "  #" << id
                      << "  addr=0x" << std::hex << std::uppercase
                      << std::setw(8) << std::setfill('0') << wp.address
                      << "  hits=" << std::dec << wp.hitCount
                      << "  " << (wp.enabled ? col(GREEN,"[enabled]") : col(DIM,"[disabled]"))
                      << "\n";
        }
    }
    printSeparator();
}

// One line per stage: what the debugger shows stepping through a cycle (§6.5).
// Pipeline4 and Pipeline6 are unrelated types, so the pipeline commands below
// are templates over whichever one is selected rather than virtual calls.
template <class P>
static void printPipeline(const P& p) {
    std::cout << col(BOLD, "  cycle " + std::to_string(p.cycles())) << "\n";
    for (int i = 0; i < p.stages(); ++i) {
        std::string view = p.stageView(i);
        std::cout << "   " << std::left << std::setw(7) << std::setfill(' ')
                  << p.stageName(i) << " ";
        const std::string upc = p.microPC(i);
        if (upc.empty()) {
            std::cout << col(DIM, "--") << "\n";
            continue;
        }
        // setw pads the plain view only; a coloured string would mis-measure.
        std::cout << std::setw(22) << view << col(CYAN, "uPC=" + upc);
        const std::string fields = P::microinstruction(p.opcodeAt(i), i);
        if (!fields.empty()) std::cout << "  " << col(DIM, fields);
        std::cout << "\n";
    }
}

// An `ustore` argument: 0x13, 19, or the mnemonic LD. -1 if it names nothing.
static int parseOpcode(const std::string& token) {
    try {
        const int value = std::stoi(token, nullptr, 0);
        if (value >= 0 && value <= 0x3F) return value;
    } catch (...) {
        // not a number, so try it as a mnemonic below
    }
    std::string upper = token;
    for (char& c : upper) c = static_cast<char>(std::toupper(c));
    for (unsigned op = 0; op <= 0x3F; ++op)
        if (mnemonicForOpcode(static_cast<uint8_t>(op)) == upper)
            return static_cast<int>(op);
    return -1;
}

// `ustore` with no argument: the control store itself, one line per opcode.
static void printControlStore() {
    printSeparator();
    std::cout << col(BOLD, "  Control store - 64 entries, addressed by opcode\n");
    printSeparator();
    for (unsigned op = 0; op <= 0x3F; ++op) {
        const Microword& w = controlWord(static_cast<uint8_t>(op));
        if (w.reserved) continue;
        std::cout << "  0x" << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << op << "  " << std::left << std::setw(6)
                  << std::setfill(' ') << mnemonicForOpcode(static_cast<uint8_t>(op))
                  << "  0x" << std::right << std::hex << std::setw(6)
                  << std::setfill('0') << w.encode() << std::dec << "\n";
    }
    std::cout << col(DIM, "  0x25-0x3F: reserved entry, raises illegal-opcode\n");
    printSeparator();
}

// `ustore <op>`: the opcode's microroutine, one microinstruction per stage.
template <class P>
static void printMicroroutine(uint8_t opcode, const char* pipelineName) {
    const Microword& w = controlWord(opcode);
    printSeparator();
    std::cout << col(BOLD, "  Microroutine for 0x") << std::hex << std::uppercase
              << std::setw(2) << std::setfill('0') << static_cast<unsigned>(opcode)
              << std::dec << std::setfill(' ') << " "
              << col(BOLD, mnemonicForOpcode(opcode))
              << col(DIM, std::string("   (") + pipelineName + ")") << "\n";
    printSeparator();
    if (w.reserved) {
        std::cout << col(RED, "  reserved entry - raises an illegal-opcode exception\n");
        printSeparator();
        return;
    }
    std::cout << col(DIM, "  uPC    stage    microinstruction\n");
    for (int stage = 0; stage < P::kStageCount; ++stage) {
        std::ostringstream upc;
        upc << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<unsigned>(opcode) << ':' << std::dec << stage;
        const std::string text = P::microinstruction(opcode, stage);
        std::cout << "  " << std::left << std::setw(7) << std::setfill(' ') << upc.str()
                  << std::setw(9) << P::stageName(stage)
                  << (text.empty() ? col(DIM, "-") : text) << "\n";
    }
    std::cout << col(DIM, "  union of all steps = 0x") << std::right << std::hex
              << std::uppercase << std::setw(6) << std::setfill('0') << w.encode()
              << std::dec << std::setfill(' ')
              << col(DIM, "  (Section 6.2 control word)\n");
    printSeparator();
}

template <class P>
static void printPipelineStats(const P& p) {
    printSeparator();
    std::cout << col(BOLD, "  Pipeline\n");
    printSeparator();
    std::cout << std::dec
              << "  Clock cycles      : " << p.cycles() << "\n"
              << "  Instructions      : " << p.retired() << "\n"
              << "  MDU stalls        : " << p.mduStalls() << "\n"
              << "  FPU stalls        : " << p.fpuStalls() << "\n"
              << "  Load-use stalls   : " << p.loadUseStalls() << "\n"
              << "  Branch-op stalls  : " << p.branchStalls() << "\n"
              << "  Squashed (bubbles): " << p.squashed() << "\n";
    printSeparator();
}

// The same figures as one key=value per line. The boxed version above is for
// a reader; a harness needs a known format to parse rather than output to
// scrape, so tools/evaluate.sh reads this.
template <class P>
static void printStatsKV(const P& p, const Simulator& sim) {
    std::cout << "pipeline=" << p.stages() << "\n"
              << "mul_algo=" << mulAlgoName(sim.mdu().mulAlgo()) << "\n"
              << "div_algo=" << divAlgoName(sim.mdu().divAlgo()) << "\n"
              << "cycles=" << p.cycles() << "\n"
              << "instructions=" << p.retired() << "\n"
              << "mdu_stalls=" << p.mduStalls() << "\n"
              << "fpu_stalls=" << p.fpuStalls() << "\n"
              << "load_use_stalls=" << p.loadUseStalls() << "\n"
              << "branch_stalls=" << p.branchStalls() << "\n"
              << "squashed=" << p.squashed() << "\n"
              << "mdu_adder_ops=" << sim.mduAdderOps() << "\n"
              << "error=" << (sim.lastError().empty() ? "none" : sim.lastError())
              << "\n";
}

// `pipe 4` / `pipe 6` confirmation line.
template <class P>
static void announcePipeline(const P& p, MulAlgo mul, DivAlgo div) {
    std::cout << col(GREEN, "  " + std::to_string(p.stages()) + "-stage pipeline: ");
    for (int i = 0; i < p.stages(); ++i)
        std::cout << (i ? ", " : "") << p.stageName(i);
    std::cout << "\n" << col(DIM, "  MDU: multiply ") << mulAlgoName(mul)
              << col(DIM, ", divide ") << divAlgoName(div)
              << col(DIM, "; static predict-not-taken.\n");
}

// A whole number of cycles, 1 or more. The digit check comes first because
// stoull quietly wraps a negative instead of refusing it.
static bool readCycleCap(const std::string& tok, uint64_t& out) {
    if (tok.empty() || tok.find_first_not_of("0123456789") != std::string::npos) return false;
    try {
        size_t used = 0;
        const unsigned long long n = std::stoull(tok, &used);
        if (used == tok.size() && n >= 1) { out = n; return true; }
    } catch (...) {
    }
    return false;
}

// The functional model's end-of-run box; each pipeline prints its own.
static void printRunSummary(const RunResult& res, const Simulator& sim) {
    printSeparator();
    std::cout << col(BOLD, "  Run finished\n")
              << "  Cycles this run : " << res.cyclesThisRun << "\n"
              << "  Total cycles    : " << sim.cyclesExecuted() << "\n"
              << "  Stop reason     : ";
    switch (res.reason) {
        case StopReason::Breakpoint: std::cout << col(YELLOW, "Breakpoint"); break;
        case StopReason::Watchpoint: std::cout << col(YELLOW, "Watchpoint"); break;
        case StopReason::Halted:     std::cout << col(sim.lastError().empty() ? GREEN : RED, "Stopped"); break;
        case StopReason::MaxCycles:  std::cout << col(RED, "Cycle cap"); break;
        default:                     std::cout << "Unknown"; break;
    }
    std::cout << "\n";
    if (!res.detail.empty())
        std::cout << "  Detail          : " << res.detail << "\n";
    printSeparator();
}

template <class P>
static void cyclePipeline(P& p, int n) {
    for (int i = 0; i < n; ++i) {
        if (!p.tick()) { std::cout << col(DIM, "  Pipeline drained.\n"); break; }
        printPipeline(p);
    }
}

template <class P>
static void runPipeline(P& p, const Simulator& sim, uint64_t maxCycles) {
    uint64_t spun = 0;
    while (spun < maxCycles && p.tick()) ++spun;
    printPipelineStats(p);
    if (!sim.lastError().empty())
        std::cout << "  " << col(RED, sim.lastError()) << "\n";
}

// Print simulator info summary
static void printInfo(const Simulator& sim) {
    printSeparator();
    std::cout << col(BOLD, "  Simulator Info\n");
    printSeparator();
    const State& s = sim.state();
    std::cout << "  Memory size : " << (sim.memory().size() / 1024) << " KB ("
              << sim.memory().size() << " bytes)\n"
              << "  Cycles      : " << sim.cyclesExecuted() << "\n"
              << "  PC          : 0x" << std::hex << std::uppercase
              << std::setw(8) << std::setfill('0') << s.PC << "\n"
              << std::dec
              << "  Status      : " << statusText(sim) << "\n";
    if (!sim.lastError().empty())
        std::cout << "  Last error  : " << col(RED, sim.lastError()) << "\n";
    printSeparator();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Help system
// ═══════════════════════════════════════════════════════════════════════════

struct CmdHelp {
    std::string name;
    std::string args;
    std::string brief;
    std::string detail;
};

static const std::vector<CmdHelp> kHelp = {
    {"load",   "<file>",              "Load a .hex or .bin file (resets CPU)",
     "  load <file>\n"
     "  Loads a program from a hex-text (.hex) or raw binary (.bin) file.\n"
     "  Resets all CPU registers to their initial state first.\n"
     "  Example:  load prog.hex"},
    {"reset",  "",                    "Reset CPU state (keep memory)",
     "  reset\n"
     "  Resets PC to 0, SP to top-of-memory and clears the registers.\n"
     "  Loaded memory is NOT cleared; use 'load' to overwrite it."},
    {"run",    "[4|6|off] [maxcycles]", "Run until the program ends / breakpoint / watchpoint",
     "  run               -- on the model already selected, or the 6-stage\n"
     "  run 4             -- on the 4-stage pipeline\n"
     "  run off           -- on the functional model, with no timing\n"
     "  run 6 5000        -- the 6-stage, giving up after 5000 cycles\n"
     "\n"
     "  Executes instructions until the PC leaves the program's code, an\n"
     "  exception stops the CPU, a breakpoint is hit, or a watched memory word\n"
     "  changes.  The optional cap defaults to 1 000 000 cycles.\n"
     "\n"
     "  With no model named, a session that has not chosen one yet gets the\n"
     "  6-stage pipeline; otherwise the last choice stands, including an\n"
     "  explicit 'pipe off'.  Naming one here selects it for later commands\n"
     "  too, and unlike 'pipe' it leaves the MDU algorithms as they are."},
    {"step",   "[n]",                 "Execute n instructions (default 1)",
     "  step [n]\n"
     "  Executes exactly n fetch-decode-execute cycles (default 1).\n"
     "  Prints the disassembled instruction and register state after each step.\n"
     "  Alias: s"},
    {"disasm", "[addr] [n]",          "Disassemble n words starting at addr",
     "  disasm [addr] [n]\n"
     "  Disassembles n instruction words (default 10) from address addr\n"
     "  (default: current PC).  Marks the PC with >.\n"
     "  Alias: da"},
    {"decode", "<word> [word ...]",   "Disassemble words given as hex or binary text",
     "  decode 0x48400040            -- one word written in hex\n"
     "  decode 48400040 60000008     -- the 0x is optional, several at a time\n"
     "  decode 0b01001000010000000000000001000000\n"
     "  decode @0x30 0x64000000      -- lay the words out from 0x30 instead of 0\n"
     "  Reconstructs instructions from text rather than from memory, so a word\n"
     "  from a listing or a hex dump can be checked without loading it. The\n"
     "  words are placed at consecutive addresses from the base, which is what\n"
     "  a PC-relative branch target is resolved against.  Alias: dec"},
    {"reg",    "",                    "Show all registers",
     "  reg\n"
     "  Displays all 16 GPRs, then PC, SP and LR.\n"
     "  Non-zero registers are highlighted."},
    {"freg",   "",                    "Show all registers read as binary32 + FPSR",
     "  freg\n"
     "  Displays all 16 GPRs decoded as single-precision floats, and the FPU\n"
     "  status register. The registers are the same ones 'reg' shows: RISC201\n"
     "  has one register file, and nothing marks a word as integer or float."},
    {"stack",  "[n|all]",             "Show the live stack as ASCII call frames",
     "  stack                 -- the top and bottom 16 words of the stack\n"
     "  stack 64              -- up to 64 words, elided in the middle\n"
     "  stack all             -- every word in use\n"
     "  Renders the words between SP and the stack base, divided into call\n"
     "  frames at the addresses where PUSH LR saved a return address, with\n"
     "  the base, SP and the overflow limit (Section 2.12).  Alias: st"},
    {"mem",    "<addr> [n]",          "Show n words of memory (default 1)",
     "  mem <addr> [n]\n"
     "  Prints n 32-bit words from memory starting at addr.\n"
     "  addr can be decimal or 0x-prefixed hex.\n"
     "  Alias: m"},
    {"set",    "reg <Rn> <val>|mem <addr> <val>", "Write to register or memory",
     "  set reg <Rn|SP|LR> <val>  -- write val into R0-R15, SP or LR\n"
     "  set mem <addr> <val>   -- write val into memory word at addr\n"
     "  Values can be decimal or 0x-prefixed hex.\n"
     "  Example:  set reg R3 0xFF\n"
     "            set mem 0x100 42"},
    {"break",  "<addr>",              "Set a breakpoint at address",
     "  break <addr>\n"
     "  Pauses execution (before fetch) when PC == addr.\n"
     "  Prints the new breakpoint ID.\n"
     "  Alias: b"},
    {"blist",  "",                    "List all breakpoints and watchpoints",
     "  blist\n"
     "  Shows IDs, addresses, enable state, and hit counts for all\n"
     "  breakpoints and watchpoints."},
    {"bdel",   "<id>",                "Delete breakpoint or watchpoint by ID",
     "  bdel <id>\n"
     "  Removes the breakpoint or watchpoint with the given numeric ID."},
    {"watch",  "<addr>",              "Watch a memory word for changes",
     "  watch <addr>\n"
     "  Pauses execution after any instruction that changes the word at addr.\n"
     "  addr must be word-aligned (multiple of 4)."},
    {"ustore", "[<opcode>|<mnemonic>]", "Show the control store or one microroutine",
     "  ustore                -- every control store entry, opcode and 22-bit word\n"
     "  ustore 0x13           -- the microroutine for that opcode, step by step\n"
     "  ustore LD             -- the same, named by mnemonic\n"
     "  Each opcode owns one microinstruction per pipeline stage (Section 6.5);\n"
     "  the union of the steps is its Section 6.2 control word.  Alias: u"},
    {"pipe",   "<4|6|off> [mul] [div]", "Select the pipeline model and MDU algorithms",
     "  pipe 4            -- IF, DE/OF, EX/MEM, WB\n"
     "  pipe 6            -- IF, ID, OF, EX, MEM, WB\n"
     "  pipe off          -- back to the functional model (no timing)\n"
     "\n"
     "  The optional pair chooses which multiply and divide algorithms the MDU\n"
     "  runs (Section 2.8).  They change how long an operation takes and how\n"
     "  often it drives the adder, never what it computes.\n"
     "    mul: booth (default)         32 cycles, adder only where two\n"
     "                                 neighbouring multiplier bits differ\n"
     "         shift-and-add           32 cycles, adder once per set bit\n"
     "         default                 1 cycle, the host\'s own *\n"
     "    div: non-restoring (default) 33 cycles, no restore steps\n"
     "         restoring               32 + one cycle per step it undoes, so\n"
     "                                 its latency depends on the operands\n"
     "         default                 1 cycle, the host\'s own /\n"
     "  Omitting either one means the machine as built: booth, non-restoring.\n"
     "  Example:  pipe 6 shift-and-add restoring\n"
     "\n"
     "  Branch handling is static predict-not-taken: a taken branch costs\n"
     "  bubbles, a not-taken one costs nothing."},
    {"stats",  "",                    "Print the run's counters as key=value lines",
     "  stats\n"
     "  The same figures the boxed summary shows, one key=value per line, for\n"
     "  a script to read: cycles, instructions, each stall counter, squashed\n"
     "  bubbles, the MDU's adder operations and any error.  Used by\n"
     "  'make evaluate'."},
    {"cycle",  "[n]",                 "Advance n clock cycles, showing each stage",
     "  cycle [n]\n"
     "  Advances the selected pipeline by n clock cycles (default 1) and\n"
     "  prints what occupies every stage, with the MDU iteration count.\n"
     "  Requires 'pipe 4' or 'pipe 6' first.  Alias: c"},
    {"info",   "",                    "Print simulator info (mem size, cycles, ...)",
     "  info\n"
     "  Displays memory size, cycle count, current PC, and run status."},
    {"help",   "[cmd]",               "Show help (all or for one command)",
     "  help            -- list all commands\n"
     "  help <command>  -- detailed description of one command"},
    {"quit",   "",                    "Exit the simulator",
     "  quit / q / exit\n"
     "  Exits the interactive session."},
};

static void printHelp(const std::string& topic = "") {
    if (!topic.empty()) {
        for (const auto& h : kHelp) {
            if (h.name == topic) {
                printSeparator();
                std::cout << col(BOLD, "  " + h.name)
                          << (h.args.empty() ? "" : "  " + col(DIM, h.args))
                          << "   -- " << h.brief << "\n\n"
                          << h.detail << "\n";
                printSeparator();
                return;
            }
        }
        std::cout << col(RED, "  Unknown command: " + topic) << "\n";
        return;
    }

    printSeparator('=');
    std::cout << col(BOLD, "  RISC201 Simulator  --  Command Reference\n");
    printSeparator('=');
    for (const auto& h : kHelp) {
        std::cout << "  " << col(CYAN, h.name);
        if (!h.args.empty())
            std::cout << " " << col(DIM, h.args);
        std::cout << "\n"
                  << "      " << h.brief << "\n";
    }
    printSeparator('=');
    std::cout << col(DIM, "  Tip: use 'help <command>' for detailed usage.\n");
    printSeparator('=');
}

// ═══════════════════════════════════════════════════════════════════════════
//  Argument parsing helpers
// ═══════════════════════════════════════════════════════════════════════════

static uint32_t parseAddr(const std::string& tok) {
    return static_cast<uint32_t>(std::stoul(tok, nullptr, 0));
}

// R0-R15 only. SP and LR are not in the register file, so the caller handles
// them separately rather than mapping them onto a GPR index.
static int parseReg(const std::string& tok) {
    std::string s = tok;
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    if (s.size() >= 2 && s[0] == 'R') {
        int idx = std::stoi(s.substr(1));
        if (idx < 0 || idx > 15) throw std::out_of_range("register out of range");
        return idx;
    }
    throw std::invalid_argument("not a register: " + tok);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    gColor = enableAnsi();
    gPrompt = stdoutIsTerminal();

    size_t memKB = 64;
    const char* envMem = std::getenv("RISC201_MEM_KB");
    if (envMem) {
        try { memKB = static_cast<size_t>(std::stoul(envMem)); } catch (...) {}
    }

    Simulator sim(memKB * 1024);
    Debugger  dbg(sim);
    // At most one is non-null; both null means the functional model. The flag
    // separates "never chosen", where `run` picks the 6-stage, from an
    // explicit `pipe off`, which it leaves alone.
    std::unique_ptr<Pipeline4> pipe4;
    std::unique_ptr<Pipeline6> pipe6;
    bool modelChosen = false;

    std::string loadedFile;
    if (argc >= 2) {
        try {
            size_t codeWords = 0;
            size_t n = loadIntoMemory(argv[1], sim.memory(), 0, &codeWords);
            sim.setCodeEnd(static_cast<uint32_t>(4 * codeWords));
            sim.setStackLimit(static_cast<uint32_t>(4 * n));
            loadedFile = argv[1];
            std::cout << col(GREEN, "  Loaded " + std::to_string(n) + " word(s) (" +
                                    std::to_string(codeWords) + " code) from ")
                      << loadedFile << "\n";
        } catch (const LoadError& e) {
            std::cerr << col(RED, "  Load error: ") << e.what() << "\n";
        }
    }

    if (loadedFile.empty()) {
        // Built-in demo: NOP x3 then ADDI R1,R0,#5. It ends when the PC walks
        // past the last word, which loadProgram takes as the end of the code.
        sim.loadProgram({
            0x00000000,  // NOP
            0x00000000,  // NOP
            0x00000000,  // NOP
            0x34400005,  // ADDI R1, R0, #5  (opcode=0x0D, rd=0001, rs1=0000, imm=5)
        });
        std::cout << col(DIM, "  No file supplied - built-in demo program loaded.\n");
        std::cout << col(DIM, "  Try: disasm 0 4\n");
    }

    printBanner();

    std::string line;
    while (true) {
        // hex, fill and alignment are sticky, and the printers below set them
        // freely. Clearing them here keeps one command's formatting out of the
        // next one's output - otherwise a cycle count can come out in hex.
        std::cout << std::dec << std::setfill(' ') << std::right;

        std::ostringstream prompt;
        prompt << col(CYAN, "risc201")
               << "["
               << col(YELLOW, "PC=0x")
               << col(YELLOW, [&]{
                       std::ostringstream o;
                       o << std::hex << std::uppercase
                         << std::setw(8) << std::setfill('0')
                         << sim.state().PC;
                       return o.str();
                   }())
               << "]> ";
        if (gPrompt) std::cout << prompt.str() << std::flush;

        if (!std::getline(std::cin, line)) break;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd.empty()) continue;

        if (cmd == "quit" || cmd == "q" || cmd == "exit") {
            std::cout << col(DIM, "  Goodbye.\n");
            break;
        }
        else if (cmd == "help" || cmd == "h" || cmd == "?") {
            std::string topic; iss >> topic;
            printHelp(topic);
        }
        else if (cmd == "info") {
            printInfo(sim);
        }
        else if (cmd == "load") {
            std::string path; iss >> path;
            if (path.empty()) { std::cout << col(RED, "  usage: load <file>\n"); continue; }
            try {
                sim.reset();
                size_t codeWords = 0;
                size_t n = loadIntoMemory(path, sim.memory(), 0, &codeWords);
                sim.setCodeEnd(static_cast<uint32_t>(4 * codeWords));
                sim.setStackLimit(static_cast<uint32_t>(4 * n));
                loadedFile = path;
                std::cout << col(GREEN, "  Loaded ") << n << " word(s) ("
                          << codeWords << " code) from " << path << "\n";
            } catch (const LoadError& e) {
                std::cout << col(RED, "  Load error: ") << e.what() << "\n";
            } catch (const MemoryAccessError& e) {
                std::cout << col(RED, "  Memory error: ") << e.what() << "\n";
            }
        }
        else if (cmd == "reset") {
            sim.reset();
            std::cout << col(GREEN, "  CPU state reset. Memory unchanged.\n");
        }
        else if (cmd == "pipe") {
            std::string kind, mulTok, divTok; iss >> kind >> mulTok >> divTok;
            // Omitted algorithms mean the machine as built, so a bare `pipe 4`
            // always describes the same processor however the last one was set.
            MulAlgo mul = MulAlgo::Booth;
            DivAlgo div = DivAlgo::NonRestoring;
            if (!mulTok.empty() && !parseMulAlgo(mulTok, mul)) {
                std::cout << col(RED, "  Multiplier must be default,"
                                      " booth or shift-and-add.\n");
                continue;
            }
            if (!divTok.empty() && !parseDivAlgo(divTok, div)) {
                std::cout << col(RED, "  Divider must be default,"
                                      " restoring or non-restoring.\n");
                continue;
            }
            if (kind != "off" && kind != "4" && kind != "6") {
                std::cout << col(RED, "  usage: pipe <4|6|off> [mul] [div]\n");
                continue;
            }
            pipe4.reset();
            pipe6.reset();
            modelChosen = true;
            // The algorithms belong to the unit, not to a pipeline: they
            // change the adder-operation count with the pipeline off too.
            sim.setMduAlgorithms(mul, div);
            if (kind == "off") {
                std::cout << col(GREEN, "  Pipeline off - functional model.\n")
                          << col(DIM, "  MDU: multiply ") << mulAlgoName(mul)
                          << col(DIM, ", divide ") << divAlgoName(div) << "\n";
            } else if (kind == "4") {
                pipe4 = std::make_unique<Pipeline4>(sim);
                announcePipeline(*pipe4, mul, div);
            } else {
                pipe6 = std::make_unique<Pipeline6>(sim);
                announcePipeline(*pipe6, mul, div);
            }
        }

        else if (cmd == "ustore" || cmd == "u") {
            std::string tok; iss >> tok;
            if (tok.empty()) { printControlStore(); continue; }
            const int op = parseOpcode(tok);
            if (op < 0) {
                std::cout << col(RED, "  usage: ustore [<opcode>|<mnemonic>]\n");
                continue;
            }
            const uint8_t opcode = static_cast<uint8_t>(op);
            if (pipe4)      printMicroroutine<Pipeline4>(opcode, "4-stage");
            else if (pipe6) printMicroroutine<Pipeline6>(opcode, "6-stage");
            else {
                printMicroroutine<Pipeline6>(opcode, "6-stage");
                std::cout << col(DIM, "  No pipeline selected; showing the"
                                      " 6-stage decomposition.\n");
            }
        }
        else if (cmd == "stats") {
            if (pipe4)      printStatsKV(*pipe4, sim);
            else if (pipe6) printStatsKV(*pipe6, sim);
            else std::cout << "pipeline=off"
                           << "\nmul_algo=" << mulAlgoName(sim.mdu().mulAlgo())
                           << "\ndiv_algo=" << divAlgoName(sim.mdu().divAlgo())
                           << "\ncycles=" << sim.cyclesExecuted()
                           << "\nmdu_adder_ops=" << sim.mduAdderOps()
                           << "\nerror="
                           << (sim.lastError().empty() ? "none" : sim.lastError())
                           << "\n";
        }
        else if (cmd == "cycle" || cmd == "c") {
            if (!pipe4 && !pipe6) { std::cout << col(RED, "  No pipeline selected; use 'pipe 4' or 'pipe 6'.\n"); continue; }
            int n = 1;
            std::string tok; iss >> tok;
            if (!tok.empty()) { try { n = std::stoi(tok); } catch (...) {} }
            if (pipe4) cyclePipeline(*pipe4, n); else cyclePipeline(*pipe6, n);
        }
        else if (cmd == "run") {
            // The model comes first and the cycle cap second, so neither has
            // to be told apart from the other by its value.
            std::string kind, capTok; iss >> kind >> capTok;
            if (!kind.empty() && kind != "4" && kind != "6" && kind != "off") {
                std::cout << col(RED, "  usage: run [4|6|off] [maxcycles]\n");
                continue;
            }
            uint64_t maxCyc = 1'000'000;
            if (!capTok.empty() && !readCycleCap(capTok, maxCyc)) {
                std::cout << col(RED, "  The cycle cap must be a whole number of"
                                      " cycles, 1 or more.\n");
                continue;
            }

            // Naming no model means the one already selected; on a fresh
            // session that is the 6-stage, so a bare `run` is a timed run.
            // An explicit `pipe off` is a choice and is left alone.
            if (kind.empty()) kind = modelChosen ? "" : "6";
            if (kind == "off") {
                pipe4.reset();
                pipe6.reset();
            } else if (kind == "4" && !pipe4) {
                pipe6.reset();
                pipe4 = std::make_unique<Pipeline4>(sim);
                announcePipeline(*pipe4, sim.mdu().mulAlgo(), sim.mdu().divAlgo());
            } else if (kind == "6" && !pipe6) {
                pipe4.reset();
                pipe6 = std::make_unique<Pipeline6>(sim);
                announcePipeline(*pipe6, sim.mdu().mulAlgo(), sim.mdu().divAlgo());
            }
            if (!kind.empty()) modelChosen = true;

            // A pipeline run ticks a clock and never consults the debugger,
            // so a breakpoint set for the functional model would pass
            // unnoticed. Say so rather than let it look honoured.
            if ((pipe4 || pipe6) && (!dbg.breakpoints().empty() || !dbg.watchpoints().empty()))
                std::cout << col(YELLOW, "  Note: breakpoints and watchpoints are not checked"
                                         " on a pipeline; use 'run off' for those.\n");

            if (pipe4) {
                runPipeline(*pipe4, sim, maxCyc);
            } else if (pipe6) {
                runPipeline(*pipe6, sim, maxCyc);
            } else {
                printRunSummary(dbg.run(maxCyc), sim);
            }
        }

        else if (cmd == "step" || cmd == "s") {
            int n = 1;
            std::string tok; iss >> tok;
            if (!tok.empty()) { try { n = std::stoi(tok); } catch (...) {} }
            for (int i = 0; i < n; ++i) {
                if (sim.state().halted) {
                    std::cout << col(RED, "  CPU has stopped. Use 'reset' or 'load' to restart.\n");
                    break;
                }
                uint32_t stepPC = sim.state().PC;
                try {
                    uint32_t word = sim.memory().readWord(stepPC);
                    std::cout << col(DIM, "  > ") << disassemble(stepPC, word) << "\n";
                } catch (...) {}
                RunResult res = dbg.step();
                if (res.reason == StopReason::Halted && !res.detail.empty())
                    std::cout << col(sim.lastError().empty() ? GREEN : RED, "  Stopped: ")
                              << res.detail << "\n";
                if (res.reason == StopReason::Watchpoint)
                    std::cout << col(YELLOW, "  ! ") << res.detail << "\n";
            }
            printRegisters(sim);
        }
        else if (cmd == "disasm" || cmd == "da" || cmd == "d") {
            uint32_t addr = sim.state().PC;
            int cnt = 10;
            std::string tok1, tok2; iss >> tok1 >> tok2;
            if (!tok1.empty()) { try { addr = parseAddr(tok1); } catch (...) { std::cout << col(RED, "  invalid address\n"); continue; } }
            if (!tok2.empty()) { try { cnt = std::stoi(tok2); } catch (...) {} }
            printDisasm(sim, addr, cnt);
        }
        else if (cmd == "decode" || cmd == "dec") {
            std::vector<std::string> toks;
            uint32_t base = 0;
            for (std::string t; iss >> t; ) {
                if (t.size() > 1 && t[0] == '@') {
                    try { base = parseAddr(t.substr(1)); } catch (...) {
                        std::cout << col(RED, "  invalid base address\n");
                        toks.clear();
                        break;
                    }
                } else {
                    toks.push_back(t);
                }
            }
            if (toks.empty()) std::cout << col(RED, "  usage: decode [@addr] <word> [word ...]\n");
            else              printDecode(toks, base);
        }
        else if (cmd == "reg" || cmd == "regs") {
            printRegisters(sim);
        }
        else if (cmd == "stack" || cmd == "st") {
            std::string tok; iss >> tok;
            uint32_t budget = 32;
            if (tok == "all") budget = 0xFFFFFFFFu;
            else if (!tok.empty()) { try { budget = static_cast<uint32_t>(std::stoul(tok)); } catch (...) {} }
            printStack(sim, budget);
        }
        else if (cmd == "freg") {
            printFloatRegisters(sim);
        }
        else if (cmd == "mem" || cmd == "m") {
            std::string tok1, tok2; iss >> tok1 >> tok2;
            if (tok1.empty()) { std::cout << col(RED, "  usage: mem <addr> [count]\n"); continue; }
            uint32_t addr = 0; int cnt = 1;
            try { addr = parseAddr(tok1); } catch (...) { std::cout << col(RED, "  invalid address\n"); continue; }
            if (!tok2.empty()) { try { cnt = std::stoi(tok2); } catch (...) {} }
            printMemory(sim, addr, cnt);
        }
        else if (cmd == "set") {
            std::string sub, target, valStr; iss >> sub >> target >> valStr;
            if (sub == "reg") {
                if (target.empty() || valStr.empty()) { std::cout << col(RED, "  usage: set reg <Rn> <val>\n"); continue; }
                try {
                    std::string up = target;
                    std::transform(up.begin(), up.end(), up.begin(), ::toupper);
                    uint32_t val = static_cast<uint32_t>(std::stoul(valStr, nullptr, 0));
                    if (up == "SP")      sim.state().SP = val;
                    else if (up == "LR") sim.state().LR = val;
                    else                 sim.state().writeGPR(parseReg(target), val);
                    std::cout << col(GREEN, "  Set " + up + " = 0x") << std::hex << std::uppercase << val << "\n";
                } catch (const std::exception& e) { std::cout << col(RED, "  Error: ") << e.what() << "\n"; }
            } else if (sub == "mem") {
                if (target.empty() || valStr.empty()) { std::cout << col(RED, "  usage: set mem <addr> <val>\n"); continue; }
                try {
                    uint32_t addr = parseAddr(target);
                    uint32_t val  = static_cast<uint32_t>(std::stoul(valStr, nullptr, 0));
                    sim.memory().writeWord(addr, val);
                    std::cout << col(GREEN, "  Set [0x") << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << addr << "] = 0x" << std::setw(8) << val << "\n";
                } catch (const std::exception& e) { std::cout << col(RED, "  Error: ") << e.what() << "\n"; }
            } else { std::cout << col(RED, "  usage: set reg|mem ...\n"); }
        }
        else if (cmd == "break" || cmd == "b") {
            std::string tok; iss >> tok;
            if (tok.empty()) { std::cout << col(RED, "  usage: break <addr>\n"); continue; }
            try {
                uint32_t addr = parseAddr(tok);
                int id = dbg.addBreakpoint(addr);
                std::cout << col(GREEN, "  Breakpoint #") << id << " set at 0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << addr << "\n";
            } catch (...) { std::cout << col(RED, "  invalid address\n"); }
        }
        else if (cmd == "blist") {
            printBreakpointsAndWatches(dbg);
        }
        else if (cmd == "bdel") {
            std::string tok; iss >> tok;
            if (tok.empty()) { std::cout << col(RED, "  usage: bdel <id>\n"); continue; }
            try {
                int id = std::stoi(tok);
                bool ok = dbg.removeBreakpoint(id) || dbg.removeWatchpoint(id);
                if (ok) std::cout << col(GREEN, "  Removed #") << id << "\n";
                else    std::cout << col(RED,   "  No breakpoint/watchpoint #") << id << "\n";
            } catch (...) { std::cout << col(RED, "  invalid id\n"); }
        }
        else if (cmd == "watch" || cmd == "w") {
            std::string tok; iss >> tok;
            if (tok.empty()) { std::cout << col(RED, "  usage: watch <addr>\n"); continue; }
            try {
                uint32_t addr = parseAddr(tok);
                int id = dbg.addWatchpoint(addr);
                std::cout << col(GREEN, "  Watchpoint #") << id << " set on [0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << addr << "]\n";
            } catch (const std::exception& e) { std::cout << col(RED, "  Error: ") << e.what() << "\n"; }
        }
        else {
            std::cout << col(RED, "  Unknown command: ") << cmd << col(DIM, "  (type 'help')\n");
        }
    }

    return 0;
}

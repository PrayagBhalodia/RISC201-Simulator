// debugger.h
// Breakpoint and watchpoint management for the RISC201 CLI debugger.
//
// Breakpoints pause execution before the instruction AT the given address.
// Watchpoints pause execution when a given memory word CHANGES value.
//
// The Debugger wraps a Simulator reference and replaces the bare `run` loop
// with a richer loop that checks these conditions after every step.

#pragma once
#include "simulator.h"
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace risc201 {

struct Breakpoint {
    uint32_t address;
    bool     enabled = true;
    uint32_t hitCount = 0;
};

struct Watchpoint {
    uint32_t address;       // word-aligned memory address to watch
    uint32_t lastValue;     // value at the time the watch was set
    bool     enabled = true;
    uint32_t hitCount = 0;
};

// Reason a debugged run paused / stopped.
enum class StopReason {
    Breakpoint,   // PC hit a breakpoint
    Watchpoint,   // watched memory word changed
    Halted,       // end of program / illegal opcode / memory fault
    MaxCycles,    // safety cycle cap reached
    UserInterrupt // not used in Phase 1 (no signal handling)
};

struct RunResult {
    StopReason reason;
    uint64_t   cyclesThisRun;
    uint32_t   hitAddress;   // bp/wp address that triggered, or 0
    std::string detail;      // human-readable explanation
};

class Debugger {
public:
    explicit Debugger(Simulator& sim);

    // ── Breakpoints ────────────────────────────────────────────────────────
    // Returns the new breakpoint ID (1-based).
    int addBreakpoint(uint32_t address);
    bool removeBreakpoint(int id);
    bool enableBreakpoint(int id, bool on);
    const std::map<int, Breakpoint>& breakpoints() const { return bps_; }

    // ── Watchpoints ────────────────────────────────────────────────────────
    // `address` must be word-aligned (multiple of 4).
    // Returns the new watchpoint ID (1-based). Throws if address unaligned.
    int addWatchpoint(uint32_t address);
    bool removeWatchpoint(int id);
    const std::map<int, Watchpoint>& watchpoints() const { return wps_; }

    // ── Execution ──────────────────────────────────────────────────────────
    // Like Simulator::run() but respects breakpoints / watchpoints.
    RunResult run(uint64_t maxCycles = 1'000'000);

    // Single step, then check watchpoints. Returns a RunResult even for
    // a single step (reason == Halted if the step halted the machine).
    RunResult step();

private:
    Simulator& sim_;
    std::map<int, Breakpoint>  bps_;
    std::map<int, Watchpoint>  wps_;
    int nextBpId_ = 1;
    int nextWpId_ = 1;

    // Samples all watchpoint current values; returns wp id if any changed, else 0.
    int checkWatchpoints();
};

} // namespace risc201

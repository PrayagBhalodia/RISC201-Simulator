// debugger.cpp
// Breakpoint / watchpoint management and instrumented execution loop.

#include "debugger.h"
#include <sstream>
#include <stdexcept>

namespace risc201 {

Debugger::Debugger(Simulator& sim) : sim_(sim) {}

// ── Breakpoints ───────────────────────────────────────────────────────────

int Debugger::addBreakpoint(uint32_t address) {
    int id = nextBpId_++;
    bps_[id] = {address, true, 0};
    return id;
}

bool Debugger::removeBreakpoint(int id) {
    return bps_.erase(id) > 0;
}

bool Debugger::enableBreakpoint(int id, bool on) {
    auto it = bps_.find(id);
    if (it == bps_.end()) return false;
    it->second.enabled = on;
    return true;
}

// ── Watchpoints ───────────────────────────────────────────────────────────

int Debugger::addWatchpoint(uint32_t address) {
    if (address % 4 != 0) {
        std::ostringstream oss;
        oss << "watchpoint address 0x" << std::hex << address
            << " is not word-aligned";
        throw std::invalid_argument(oss.str());
    }
    int id = nextWpId_++;
    uint32_t initial = 0;
    try {
        initial = sim_.memory().readWord(address);
    } catch (...) {
        // If address is invalid the watch still registers; it'll trip on first read.
    }
    wps_[id] = {address, initial, true, 0};
    return id;
}

bool Debugger::removeWatchpoint(int id) {
    return wps_.erase(id) > 0;
}

// ── Internal watchpoint checker ───────────────────────────────────────────

int Debugger::checkWatchpoints() {
    for (auto& [id, wp] : wps_) {
        if (!wp.enabled) continue;
        try {
            uint32_t current = sim_.memory().readWord(wp.address);
            if (current != wp.lastValue) {
                wp.lastValue = current;
                ++wp.hitCount;
                return id;
            }
        } catch (...) {
            // Memory fault on watchpoint address — skip
        }
    }
    return 0; // no watchpoint triggered
}

// ── Step (with watchpoint check) ─────────────────────────────────────────

RunResult Debugger::step() {
    uint64_t before = sim_.cyclesExecuted();

    bool ok = sim_.step();
    int wpId = checkWatchpoints();

    uint64_t delta = sim_.cyclesExecuted() - before;
    RunResult r;
    r.cyclesThisRun = delta;
    r.hitAddress    = 0;

    if (wpId != 0) {
        auto& wp = wps_.at(wpId);
        r.reason = StopReason::Watchpoint;
        r.hitAddress = wp.address;
        std::ostringstream oss;
        oss << "watchpoint #" << wpId
            << " hit: [0x" << std::hex << wp.address << "] changed";
        r.detail = oss.str();
    } else if (!ok) {
        r.reason = StopReason::Halted;
        r.detail = sim_.lastError().empty() ? "end of program" : sim_.lastError();
    } else {
        r.reason = StopReason::Halted; // placeholder — caller checks halted state
        r.detail = "";
    }

    return r;
}

// ── Run (with breakpoints + watchpoints) ─────────────────────────────────

RunResult Debugger::run(uint64_t maxCycles) {
    uint64_t start   = sim_.cyclesExecuted();
    uint64_t stepped = 0;
    RunResult r;
    r.hitAddress    = 0;

    while (!sim_.state().halted && stepped < maxCycles) {
        // Check for a breakpoint at the CURRENT PC (before stepping)
        uint32_t pc = sim_.state().PC;
        for (auto& [id, bp] : bps_) {
            if (bp.enabled && bp.address == pc) {
                ++bp.hitCount;
                r.reason = StopReason::Breakpoint;
                r.hitAddress = pc;
                std::ostringstream oss;
                oss << "breakpoint #" << id
                    << " hit at 0x" << std::hex << pc
                    << " (hit count: " << std::dec << bp.hitCount << ")";
                r.detail = oss.str();
                r.cyclesThisRun = sim_.cyclesExecuted() - start;
                return r;
            }
        }

        bool ok = sim_.step();
        ++stepped;

        // Check watchpoints after the step
        int wpId = checkWatchpoints();
        if (wpId != 0) {
            auto& wp = wps_.at(wpId);
            r.reason = StopReason::Watchpoint;
            r.hitAddress = wp.address;
            std::ostringstream oss;
            oss << "watchpoint #" << wpId
                << " hit: [0x" << std::hex << wp.address << "] = 0x"
                << std::hex << wp.lastValue;
            r.detail = oss.str();
            r.cyclesThisRun = sim_.cyclesExecuted() - start;
            return r;
        }

        if (!ok) break;
    }

    r.cyclesThisRun = sim_.cyclesExecuted() - start;

    if (sim_.state().halted) {
        r.reason = StopReason::Halted;
        r.detail = sim_.lastError().empty() ? "end of program" : sim_.lastError();
    } else {
        r.reason = StopReason::MaxCycles;
        r.detail = "reached cycle cap (" + std::to_string(maxCycles) + ")";
    }

    return r;
}

} // namespace risc201

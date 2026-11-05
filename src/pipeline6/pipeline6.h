// pipeline6.h
// The 6-stage RISC201 pipeline: IF, ID, OF, EX, MEM, WB (Section 4).
//
// Self-contained: this directory holds the whole timing model for the 6-stage
// machine and shares no code with src/pipeline4/. Instruction semantics still
// come from Simulator - the pipeline is in-order with full forwarding, so
// running each instruction at a single point against the architectural
// register file yields exactly what the forwarding paths would, and these
// files add stage occupancy and cycle counts, not a second copy of the ISA.

#pragma once
#include "control.h"
#include "mdu.h"
#include "simulator.h"
#include <array>
#include <cstdint>
#include <string>

namespace risc201 {

class Pipeline6 {
public:
    // Which multiply/divide algorithms run is the simulator's MDU setting
    // (Section 2.8), so the execute-stage hold is asked of that unit rather
    // than fixed here: restoring division's cost depends on its operands.
    explicit Pipeline6(Simulator& sim);

    // Advances one clock cycle. False once the machine has stopped fetching
    // and the pipeline has drained.
    bool tick();
    void reset();

    bool drained() const;
    int stages() const { return kStageCount; }
    static const char* stageName(int i);

    // Number of microinstructions in every opcode's routine: one per stage.
    static constexpr int kStageCount = 6;

    // The control lines this stage drives (Section 6.5). Across all stages
    // these sets partition the microword.
    static FieldSet microFields(int stage);

    // "RegWrite=1  WBSrc=mem": the microinstruction this opcode's routine
    // holds at `stage`, or "" when the stage drives nothing for it.
    static std::string microinstruction(uint8_t opcode, int stage);

    // "13:4", the microPC of whatever occupies `stage` - the control store
    // address and the step within the routine - or "" for a bubble.
    std::string microPC(int stage) const;

    // The control store address of whatever occupies `stage`; meaningful only
    // when microPC(stage) is non-empty.
    uint8_t opcodeAt(int stage) const;

    // "0x04 MUL [MDU 7/32]" for the debugger's per-cycle view (Section 6.5),
    // or "" when the stage holds a bubble.
    std::string stageView(int i) const;

    uint64_t cycles() const { return cycles_; }
    uint64_t retired() const { return retired_; }
    uint64_t mduStalls() const { return mduStalls_; }
    uint64_t fpuStalls() const { return fpuStalls_; }
    uint64_t loadUseStalls() const { return loadUseStalls_; }
    uint64_t branchStalls() const { return branchStalls_; }
    uint64_t squashed() const { return squashed_; }

private:
    static constexpr int kIF = 0;    // instruction fetch
    static constexpr int kID = 1;    // decode; B, CALL and RET redirect here
    static constexpr int kOF = 2;    // register read; conditional branches compare
    static constexpr int kEX = 3;    // ALU / MDU
    static constexpr int kMEM = 4;   // data memory access
    static constexpr int kWB = 5;    // register write-back

    struct Slot {
        bool valid = false;
        uint32_t pc = 0;
        DecodedInstruction instr;
        int exLeft = 0;    // execute-stage cycles still owed, MDU or FPU
        int exTotal = 0;
    };

    // Why a stage could not hand its instruction on this cycle.
    enum class Hold { None, Mdu, Fpu, LoadUse, BranchOperand };

    Simulator& sim_;
    std::array<Slot, kStageCount> slots_{};
    uint32_t fetchPC_ = 0;
    bool fetchStopped_ = false;

    uint64_t cycles_ = 0, retired_ = 0, mduStalls_ = 0, fpuStalls_ = 0,
             loadUseStalls_ = 0, branchStalls_ = 0, squashed_ = 0;

    int latencyOf(const DecodedInstruction& d) const;
    int resolveStageOf(const DecodedInstruction& d) const;
    int readyStageOf(const DecodedInstruction& d, int r) const;
    Hold holdReason(int stage) const;
    bool operandsPending(const DecodedInstruction& consumer, int from,
                         bool neededThisCycle) const;
    void squashYounger(int stage);
    void fetch();
};

} // namespace risc201

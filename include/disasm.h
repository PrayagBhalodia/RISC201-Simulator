// disasm.h
// Human-readable disassembly of a single RISC201 instruction word.
// Used by the CLI debugger to display the current instruction before and
// after each step, and by the `disasm` command for bulk listing.
//
// Reference: RISC201 ISA Specification, Sections 2.2 / 2.3.

#pragma once
#include "decode.h"
#include <cstdint>
#include <string>

namespace risc201 {

// Returns a string like:
//   "0x00000004:  0x080A000A   ADDI  R2, R1, #10"
// for the instruction at the given address.
// `addr` is the PC value at which `word` was fetched.
std::string disassemble(uint32_t addr, uint32_t word);

// Convenience: disassemble an already-decoded instruction.
// `addr` and `instr.raw` supply the two leftmost columns.
std::string disassemble(uint32_t addr, const DecodedInstruction& instr);

// Format a register name: R0..R15, with aliases for special-purpose regs.
//   R0  -> "R0 (zero)"    (hardwired zero)
//   R13 -> "R13"          (general – ISA does not name it)
//   R14 -> "SP"
//   R15 -> "LR"
std::string regName(uint8_t index, bool longForm = false);

} // namespace risc201

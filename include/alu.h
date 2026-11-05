// alu.h
// The arithmetic/logic unit of the execute stage (ISA Section 2.3).
//
// Combinational and stateless: ALUOp from the control word selects the
// operation and the result goes straight to the datapath. There is no
// condition-code output, because RISC201 has no flags register - BEQ and BGT
// compare their source registers in the decode stage instead.
//
// The multiply and divide codes belong to the MDU sitting next to the ALU
// (Section 2.8), which shares this unit's result bus; MDUEn routes them there,
// so they never arrive here. See mdu.h.

#pragma once
#include "control.h"
#include <cstdint>

namespace risc201 {

class Alu {
public:
    uint32_t apply(AluOp op, uint32_t a, uint32_t b) const;
};

} // namespace risc201

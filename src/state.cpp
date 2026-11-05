#include "state.h"

namespace risc201 {

std::string FpStatus::toString() const {
    std::string s;
    s += (invalid ? 'I' : 'i');
    s += (divByZero ? 'Z' : 'z');
    s += (overflow ? 'O' : 'o');
    s += (underflow ? 'U' : 'u');
    s += (inexact ? 'X' : 'x');
    return s;
}

uint32_t State::readGPR(int index) const {
    if (index == 0) return 0; // R0 hardwired to 0 (Section 2.1)
    return gpr[static_cast<size_t>(index)];
}

void State::writeGPR(int index, uint32_t value) {
    if (index == 0) return; // writes to R0 discarded
    gpr[static_cast<size_t>(index)] = value;
}

} // namespace risc201

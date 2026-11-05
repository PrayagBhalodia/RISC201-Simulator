// memory.h
// Byte-addressable memory. Instruction fetch and LD/ST both deal in
// word-aligned 32-bit accesses (ISA spec, Section 2.1/2.3).

#pragma once
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace risc201 {

// Thrown for misaligned or out-of-range accesses; the simulator's
// exception framework (Person 5's Phase 2 deliverable) will catch these
// and turn them into a reported exception rather than a crash.
class MemoryAccessError : public std::runtime_error {
public:
    explicit MemoryAccessError(const std::string& what) : std::runtime_error(what) {}
};

class Memory {
public:
    explicit Memory(size_t sizeBytes);

    uint32_t readWord(uint32_t address) const;
    void writeWord(uint32_t address, uint32_t value);

    uint8_t readByte(uint32_t address) const;
    void writeByte(uint32_t address, uint8_t value);

    size_t size() const { return data_.size(); }

private:
    std::vector<uint8_t> data_;
    void checkBounds(uint32_t address, size_t width) const;
};

} // namespace risc201

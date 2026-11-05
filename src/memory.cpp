#include "memory.h"
#include <sstream>

namespace risc201 {

Memory::Memory(size_t sizeBytes) : data_(sizeBytes, 0) {}

void Memory::checkBounds(uint32_t address, size_t width) const {
    if (static_cast<size_t>(address) + width > data_.size()) {
        std::ostringstream oss;
        oss << "memory access out of range: address=0x" << std::hex << address;
        throw MemoryAccessError(oss.str());
    }
}

uint32_t Memory::readWord(uint32_t address) const {
    if (address % 4 != 0) {
        std::ostringstream oss;
        oss << "unaligned word read at address=0x" << std::hex << address;
        throw MemoryAccessError(oss.str());
    }
    checkBounds(address, 4);
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(data_[address + i]) << (8 * i); // little-endian
    }
    return value;
}

void Memory::writeWord(uint32_t address, uint32_t value) {
    if (address % 4 != 0) {
        std::ostringstream oss;
        oss << "unaligned word write at address=0x" << std::hex << address;
        throw MemoryAccessError(oss.str());
    }
    checkBounds(address, 4);
    for (int i = 0; i < 4; ++i) {
        data_[address + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
    }
}

uint8_t Memory::readByte(uint32_t address) const {
    checkBounds(address, 1);
    return data_[address];
}

void Memory::writeByte(uint32_t address, uint8_t value) {
    checkBounds(address, 1);
    data_[address] = value;
}

} // namespace risc201

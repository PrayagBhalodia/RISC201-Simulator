// loader.h
// Utility for loading programs into RISC201 simulator memory.
//
// Supported input formats (Phase 1):
//   - Raw binary   (.bin)  : stream of big-endian 32-bit instruction words
//   - Hex text     (.hex)  : one hex word per line, optional "0x" prefix,
//                            optional "address: word" format
//
// The assembler's output format (Phase 3 integration) will extend this.

#pragma once
#include "memory.h"
#include <cstdint>
#include <string>
#include <vector>

namespace risc201 {

// Thrown when a file cannot be opened or contains malformed content.
class LoadError : public std::runtime_error {
public:
    explicit LoadError(const std::string& msg) : std::runtime_error(msg) {}
};

// Reads a raw binary file (big-endian 32-bit words) and returns the words.
// Throws LoadError on file I/O or size errors.
std::vector<uint32_t> loadBinaryFile(const std::string& path);

// Reads a plain hex text file (one word per line, base-16) and returns words.
// Each line may be:
//   DEADBEEF
//   0xDEADBEEF
//   0x0000: 0xDEADBEEF    <- address:word format (address is ignored)
// Blank lines and lines starting with '#' or ';' are skipped.
// `codeWords`, when non-null, receives how many of the leading words are code:
// the count from the assembler's "; code <n>" header, or every word when the
// file has no such header. The simulator needs it to know where a program ends.
// Throws LoadError on parse errors.
std::vector<uint32_t> loadHexFile(const std::string& path,
                                  size_t* codeWords = nullptr);

// Auto-detects format by file extension (".bin" -> binary, else hex text).
// Loads the words starting at `baseAddr` in `mem`.
// Returns the number of words loaded; `codeWords` is as above.
size_t loadIntoMemory(const std::string& path, Memory& mem,
                      uint32_t baseAddr = 0, size_t* codeWords = nullptr);

} // namespace risc201

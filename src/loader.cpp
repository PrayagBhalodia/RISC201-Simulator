// loader.cpp
// Loads RISC201 programs from binary or hex-text files into Memory.

#include "loader.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace risc201 {

// ── Helpers ───────────────────────────────────────────────────────────────

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// ── Binary loader ─────────────────────────────────────────────────────────

std::vector<uint32_t> loadBinaryFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        throw LoadError("cannot open file: " + path);

    f.seekg(0, std::ios::end);
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);

    if (size % 4 != 0)
        throw LoadError("binary file size is not a multiple of 4 bytes: " + path);

    std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
    for (auto& w : words) {
        uint8_t buf[4];
        if (!f.read(reinterpret_cast<char*>(buf), 4))
            throw LoadError("unexpected end of file: " + path);
        // Big-endian: MSB first (common assembler output convention)
        w = (static_cast<uint32_t>(buf[0]) << 24) |
            (static_cast<uint32_t>(buf[1]) << 16) |
            (static_cast<uint32_t>(buf[2]) <<  8) |
            (static_cast<uint32_t>(buf[3])       );
    }
    return words;
}

// ── Hex-text loader ───────────────────────────────────────────────────────

// The assembler's "; code <n>" header. False for every other comment line.
static bool parseCodeHeader(const std::string& line, size_t& codeWords) {
    static const std::string kPrefix = "; code ";
    if (line.compare(0, kPrefix.size(), kPrefix) != 0) return false;
    try {
        codeWords = static_cast<size_t>(std::stoul(line.substr(kPrefix.size())));
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

std::vector<uint32_t> loadHexFile(const std::string& path, size_t* codeWords) {
    std::ifstream f(path);
    if (!f.is_open())
        throw LoadError("cannot open file: " + path);

    std::vector<uint32_t> words;
    std::string line;
    int lineNo = 0;
    size_t headerWords = 0;
    bool haveHeader = false;

    while (std::getline(f, line)) {
        ++lineNo;
        std::string s = trim(line);

        // Skip blanks and comments
        if (s.empty() || s[0] == '#' || s[0] == ';') {
            if (parseCodeHeader(s, headerWords)) haveHeader = true;
            continue;
        }

        // Strip optional "addr:" prefix  e.g. "0x0000: 0xDEADBEEF"
        size_t colon = s.find(':');
        if (colon != std::string::npos) {
            s = trim(s.substr(colon + 1));
        }

        if (s.empty()) continue;

        // Parse the word (accept "0x" prefix or raw hex)
        try {
            uint32_t word = static_cast<uint32_t>(
                std::stoul(s, nullptr, 16));
            words.push_back(word);
        } catch (const std::exception&) {
            std::ostringstream oss;
            oss << path << ":" << lineNo
                << ": cannot parse hex word \"" << s << "\"";
            throw LoadError(oss.str());
        }
    }

    if (words.empty())
        throw LoadError("no instruction words found in: " + path);

    // A header claiming more code than the file holds is not trusted.
    if (codeWords)
        *codeWords = haveHeader ? std::min(headerWords, words.size()) : words.size();

    return words;
}

// ── Auto-detect and load ──────────────────────────────────────────────────

size_t loadIntoMemory(const std::string& path, Memory& mem, uint32_t baseAddr,
                      size_t* codeWords) {
    // Determine format by extension
    std::string ext;
    size_t dot = path.rfind('.');
    if (dot != std::string::npos) {
        ext = path.substr(dot);
        // to lowercase
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return std::tolower(c); });
    }

    std::vector<uint32_t> words;
    if (ext == ".bin") {
        words = loadBinaryFile(path);
        if (codeWords) *codeWords = words.size();  // raw images carry no header
    } else {
        // Default: hex text (.hex, .txt, or unknown extension)
        words = loadHexFile(path, codeWords);
    }

    // Write words into memory starting at baseAddr
    uint32_t addr = baseAddr;
    for (uint32_t w : words) {
        mem.writeWord(addr, w);
        addr += 4;
    }

    return words.size();
}

} // namespace risc201

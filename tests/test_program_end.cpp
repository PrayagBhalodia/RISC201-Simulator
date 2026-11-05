// test_program_end.cpp
// Checks where a program stops. RISC201 has no halt instruction, so the end of
// the code is the end of the run, and the data the assembler appends after the
// code must never be fetched (ISA Section 2.9).

#include "loader.h"
#include "pipeline4/pipeline4.h"
#include "pipeline6/pipeline6.h"
#include "simulator.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace risc201;

static uint32_t encR(Op op, int rd, int rs1, int rs2) {
    return (static_cast<uint32_t>(op) << 26) | (static_cast<uint32_t>(rd) << 22) |
           (static_cast<uint32_t>(rs1) << 18) | (static_cast<uint32_t>(rs2) << 14);
}

static uint32_t encJ(Op op, int offset) {
    return (static_cast<uint32_t>(op) << 26) | (static_cast<uint32_t>(offset) & 0x3FFFFFFu);
}

static uint32_t encI(Op op, int rd, int rs1, int imm) {
    return (static_cast<uint32_t>(op) << 26) | (static_cast<uint32_t>(rd) << 22) |
           (static_cast<uint32_t>(rs1) << 18) | (static_cast<uint32_t>(imm) & 0x3FFFFu);
}

// R1 = 3, R2 = 4, R3 = R1 + R2, then one word of data that decodes as an
// illegal opcode - so fetching past the code cannot go unnoticed.
static const uint32_t kImage[] = {
    encI(Op::ADDI, 1, 0, 3),
    encI(Op::ADDI, 2, 0, 4),
    encR(Op::ADD, 3, 1, 2),
    0xFFFFFFFFu,
};
static constexpr size_t kCodeWords = 3;

static void writeHex(const std::string& path, size_t words, const char* header) {
    std::ofstream out(path);
    assert(out.is_open());
    if (header) out << header << "\n";
    for (size_t i = 0; i < words; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%08X", kImage[i]);
        out << buf << "\n";
    }
}

// The assembler's header is what tells the loader which words are code.
static void hex_header_gives_the_code_size() {
    const std::string path = "build/test_program_end.hex";
    size_t codeWords = 0;

    writeHex(path, 4, "; code 3");
    assert(loadHexFile(path, &codeWords).size() == 4);
    assert(codeWords == kCodeWords);

    // No header: every word is taken to be code, which is what a hand-written
    // image or a .bin needs.
    writeHex(path, 4, nullptr);
    assert(loadHexFile(path, &codeWords).size() == 4);
    assert(codeWords == 4);

    // A header claiming more code than the file holds is not trusted.
    writeHex(path, 4, "; code 99");
    assert(loadHexFile(path, &codeWords).size() == 4);
    assert(codeWords == 4);

    // Not the header, just a comment.
    writeHex(path, 4, "; code of conduct");
    assert(loadHexFile(path, &codeWords).size() == 4);
    assert(codeWords == 4);
}

static Simulator loaded() {
    Simulator sim;
    sim.loadProgram(std::vector<uint32_t>(kImage, kImage + 4));
    sim.setCodeEnd(static_cast<uint32_t>(4 * kCodeWords));
    return sim;
}

static void functional_run_stops_after_the_last_instruction() {
    Simulator sim = loaded();
    sim.run();
    assert(sim.state().halted);
    assert(sim.lastError().empty());          // an end, not a fault
    assert(sim.state().PC == 4 * kCodeWords); // the data word was never fetched
    assert(sim.state().readGPR(3) == 7);
    assert(sim.cyclesExecuted() == kCodeWords);
}

template <class P>
static void pipeline_stops_fetching_at_the_end_of_the_code() {
    Simulator sim = loaded();
    P pipe(sim);
    while (pipe.tick()) { }
    assert(pipe.drained());
    assert(pipe.retired() == kCodeWords);
    assert(sim.lastError().empty());
    assert(sim.state().readGPR(3) == 7);
}

// A subroutine is normally the last thing in a program image, so the fetcher
// reaches the end of the code while the RET that comes back is still in
// flight. Running off the end only ends the run if nothing brings the PC back.
static const uint32_t kReturnImage[] = {
    encI(Op::ADDI, 1, 0, 3),    // 0x00  R1 = 3
    encJ(Op::CALL, 8),          // 0x04  -> 0x10, LR = 0x08
    encI(Op::ADDI, 2, 1, 1),    // 0x08  R2 = 14, only if the return is followed
    encJ(Op::B, 8),             // 0x0C  -> 0x18, past the code: the end
    encI(Op::ADDI, 1, 1, 10),   // 0x10  the routine: R1 = 13
    encJ(Op::RET, 0),           // 0x14  -> 0x08, below codeEnd again
};

static Simulator withReturnImage() {
    Simulator sim;
    sim.loadProgram(std::vector<uint32_t>(kReturnImage, kReturnImage + 6));
    return sim;
}

static void functional_run_returns_from_a_routine_at_the_end() {
    Simulator sim = withReturnImage();
    sim.run();
    assert(sim.state().halted);
    assert(sim.lastError().empty());
    assert(sim.state().readGPR(1) == 13);
    assert(sim.state().readGPR(2) == 14);
    assert(sim.state().PC == 0x18);
}

template <class P>
static void pipeline_resumes_fetching_after_a_return() {
    Simulator sim = withReturnImage();
    P pipe(sim);
    while (pipe.tick()) { }
    assert(pipe.drained());
    assert(sim.lastError().empty());
    assert(sim.state().readGPR(1) == 13);
    assert(sim.state().readGPR(2) == 14);   // the instruction after CALL ran
}

int main() {
    hex_header_gives_the_code_size();
    functional_run_stops_after_the_last_instruction();
    pipeline_stops_fetching_at_the_end_of_the_code<Pipeline4>();
    pipeline_stops_fetching_at_the_end_of_the_code<Pipeline6>();
    functional_run_returns_from_a_routine_at_the_end();
    pipeline_resumes_fetching_after_a_return<Pipeline4>();
    pipeline_resumes_fetching_after_a_return<Pipeline6>();
    std::printf("test_program_end: all checks passed\n");
    return 0;
}

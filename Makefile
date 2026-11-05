# Makefile for RISC201 - assembler (Person 2) + simulator/debugger (Person 5)

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -O2
INCLUDES := -Iinclude -Isrc

# ── Person 2: Assembler ──────────────────────────────────────────────────────
# A separate deliverable, so its sources may not be in this checkout. The
# targets below engage only when they are, which keeps a plain `make` working
# on a simulator-only clone instead of failing on a missing rule.
HAVE_ASM := $(wildcard src/assembler.cpp)
ASM_HEADERS := include/isa.h
ASM_SRC     := src/assembler.cpp src/isa.cpp

# ── Person 5: Simulator / Debugger ──────────────────────────────────────────
SIM_HEADERS := include/decode.h include/memory.h include/state.h \
               include/simulator.h include/debugger.h \
               include/disasm.h include/loader.h include/control.h \
               include/alu.h include/fpu.h include/mdu.h \
               src/pipeline4/pipeline4.h src/pipeline6/pipeline6.h
SIM_SRC  := src/decode.cpp src/memory.cpp src/state.cpp src/control.cpp \
            src/alu.cpp src/fpu.cpp src/mdu.cpp \
            src/simulator.cpp src/debugger.cpp \
            src/disasm.cpp src/loader.cpp \
            src/pipeline4/pipeline4.cpp src/pipeline6/pipeline6.cpp

ifeq ($(OS),Windows_NT)
    EXE      := .exe
    MKBUILD  := if not exist build mkdir build
    RMBUILD  := if exist build rmdir /s /q build
    RUN       = $(subst /,\\,$(1))
else
    EXE      :=
    MKBUILD  := mkdir -p build
    RMBUILD  := rm -rf build
    RUN       = ./$(1)
endif

ASM  := build/risc201asm$(EXE)
SIM  := build/risc201sim$(EXE)
CTL  := build/test_control$(EXE)
ALU  := build/test_alu$(EXE)
MDU  := build/test_mdu$(EXE)
PIPE := build/test_pipeline$(EXE)
END  := build/test_program_end$(EXE)
FPU  := build/test_fpu$(EXE)
STK  := build/test_stack$(EXE)

.PHONY: all asm sim spec cli guide docs evaluate test-evaluate examples test-examples test test-alu test-mdu test-pipeline test-control test-fpu test-stack test-program-end test-cli test-asm clean

# Prerequisites accumulate, so the assembler block below adds to these.
all: $(SIM)
test: test-alu test-mdu test-pipeline test-control test-fpu test-stack test-program-end test-cli

# ── Assembler targets ────────────────────────────────────────────────────────
# assembler.cpp carries its own main(), so there is no separate entry point.
$(ASM): $(ASM_SRC) $(ASM_HEADERS)
	$(MKBUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(ASM_SRC) -o $@

ifneq ($(HAVE_ASM),)
all: $(ASM)
test: test-asm
asm: $(ASM)

# Assembles every program in tests/asm and compares it with the expected
# image; a program with no expected image is an error case and must be
# rejected. POSIX shell.
test-asm: $(ASM)
	@for s in tests/asm/*.s; do \
	  n=`basename $$s .s`; \
	  if [ -f tests/asm/expected/$$n.hex ]; then \
	    $(call RUN,$(ASM)) $$s >/dev/null || { echo "  FAIL $$n: assembler reported an error"; exit 1; }; \
	    cmp -s tests/asm/$$n.hex tests/asm/expected/$$n.hex || { echo "  FAIL $$n: output differs from expected"; exit 1; }; \
	    echo "  ok   $$n"; \
	  else \
	    if $(call RUN,$(ASM)) $$s >/dev/null 2>tests/asm/$$n.err; then echo "  FAIL $$n: expected a rejection"; exit 1; fi; \
	    if [ -f tests/asm/expected/$$n.err ]; then \
	      while IFS= read -r want; do \
	        [ -z "$$want" ] && continue; \
	        grep -qF "$$want" tests/asm/$$n.err || { echo "  FAIL $$n: no error matching: $$want"; exit 1; }; \
	      done < tests/asm/expected/$$n.err; \
	    fi; \
	    echo "  ok   $$n (rejected as expected)"; \
	  fi; \
	done
	@rm -f tests/asm/*.hex tests/asm/*.err
	@echo "test_asm: all checks passed"
else
asm test-asm:
	@echo "  Assembler sources are not in this checkout; '$@' needs src/assembler.cpp."
endif

# ── Simulator targets ────────────────────────────────────────────────────────
sim: $(SIM)

$(SIM): $(SIM_SRC) src/simulator_main.cpp $(SIM_HEADERS)
	$(MKBUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SIM_SRC) src/simulator_main.cpp -o $@

run: $(SIM)
	$(call RUN,$(SIM)) $(PROG)

$(CTL): $(SIM_SRC) tests/test_control.cpp $(SIM_HEADERS)
	$(MKBUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SIM_SRC) tests/test_control.cpp -o $@

test-control: $(CTL)
	$(call RUN,$(CTL))

$(ALU): $(SIM_SRC) tests/test_alu.cpp $(SIM_HEADERS)
	$(MKBUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SIM_SRC) tests/test_alu.cpp -o $@

test-alu: $(ALU)
	$(call RUN,$(ALU))

$(MDU): $(SIM_SRC) tests/test_mdu.cpp $(SIM_HEADERS)
	$(MKBUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SIM_SRC) tests/test_mdu.cpp -o $@

test-mdu: $(MDU)
	$(call RUN,$(MDU))

$(PIPE): $(SIM_SRC) tests/test_pipeline.cpp $(SIM_HEADERS)
	$(MKBUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SIM_SRC) tests/test_pipeline.cpp -o $@

test-pipeline: $(PIPE)
	$(call RUN,$(PIPE))

$(END): $(SIM_SRC) tests/test_program_end.cpp $(SIM_HEADERS)
	$(MKBUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SIM_SRC) tests/test_program_end.cpp -o $@

test-program-end: $(END)
	$(call RUN,$(END))

$(FPU): $(SIM_SRC) tests/test_fpu.cpp $(SIM_HEADERS)
	$(MKBUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SIM_SRC) tests/test_fpu.cpp -o $@

test-fpu: $(FPU)
	$(call RUN,$(FPU))

$(STK): $(SIM_SRC) tests/test_stack.cpp $(SIM_HEADERS)
	$(MKBUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SIM_SRC) tests/test_stack.cpp -o $@

test-stack: $(STK)
	$(call RUN,$(STK))

# hex, fill and width are sticky on a stream. A command that leaves them set
# makes the next one print a decimal figure in hex, which no unit test sees.
test-cli: $(SIM)
	@printf 'set reg R1 7\ninfo\nquit\n' | $(call RUN,$(SIM)) \
	  | sed 's/\x1b\[[0-9;]*m//g' > build/cli.out
	@grep -q 'Memory size : 64 KB (65536 bytes)' build/cli.out \
	  || { echo "  FAIL test_cli: memory size did not print in decimal"; exit 1; }
# The hex and the binary spelling of one word have to decode to one instruction.
	@printf 'decode 0x48400040\ndecode 01001000010000000000000001000000\nquit\n' \
	  | $(call RUN,$(SIM)) | sed 's/\x1b\[[0-9;]*m//g' > build/cli.out
	@test "`grep -cF '0x48400040   LD' build/cli.out`" = 2 \
	  || { echo "  FAIL test_cli: hex and binary words did not decode alike"; exit 1; }
	@printf 'decode @0x30 0x64000000\nquit\n' | $(call RUN,$(SIM)) \
	  | sed 's/\x1b\[[0-9;]*m//g' | grep -qF '0x00000030:  0x64000000   RET' \
	  || { echo "  FAIL test_cli: decode ignored its base address"; exit 1; }
# The algorithm names `pipe` takes are a fixed vocabulary, so a typo has to be
# refused outright rather than leaving the machine half-reconfigured.
	@printf 'pipe 4 shift-and-add restoring\nstats\nquit\n' | $(call RUN,$(SIM)) \
	  | sed 's/\x1b\[[0-9;]*m//g' > build/cli.out
	@grep -q '^mul_algo=shift-and-add' build/cli.out \
	  && grep -q '^div_algo=restoring' build/cli.out \
	  || { echo "  FAIL test_cli: pipe did not select the named algorithms"; exit 1; }
	@printf 'pipe 4 wallace\npipe 4 booth bogus\nstats\nquit\n' | $(call RUN,$(SIM)) \
	  | sed 's/\x1b\[[0-9;]*m//g' > build/cli.out
	@grep -q 'Multiplier must be' build/cli.out \
	  && grep -q 'Divider must be' build/cli.out \
	  || { echo "  FAIL test_cli: pipe accepted an unknown algorithm name"; exit 1; }
	@grep -q '^pipeline=off' build/cli.out \
	  || { echo "  FAIL test_cli: a refused pipe command still selected a pipeline"; exit 1; }
# `run` names its own timing model. A session that has not chosen one gets the
# 6-stage; an explicit `pipe off` is a choice and has to survive a bare `run`.
	@printf 'run\nstats\nquit\n' | $(call RUN,$(SIM)) \
	  | sed 's/\x1b\[[0-9;]*m//g' | grep -q '^pipeline=6' \
	  || { echo "  FAIL test_cli: a bare run did not default to the 6-stage"; exit 1; }
	@printf 'run 4\nstats\nquit\n' | $(call RUN,$(SIM)) \
	  | sed 's/\x1b\[[0-9;]*m//g' | grep -q '^pipeline=4' \
	  || { echo "  FAIL test_cli: run did not take the model it was given"; exit 1; }
	@printf 'pipe off\nrun\nstats\nquit\n' | $(call RUN,$(SIM)) \
	  | sed 's/\x1b\[[0-9;]*m//g' | grep -q '^pipeline=off' \
	  || { echo "  FAIL test_cli: a bare run overrode an explicit 'pipe off'"; exit 1; }
	@printf 'run 500\nstats\nquit\n' | $(call RUN,$(SIM)) \
	  | sed 's/\x1b\[[0-9;]*m//g' > build/cli.out
	@grep -q 'usage: run' build/cli.out && grep -q '^cycles=0' build/cli.out \
	  || { echo "  FAIL test_cli: run read a cycle cap as a pipeline"; exit 1; }
# A pipeline run never consults the debugger, so a breakpoint set before one
# has to be called out rather than appear to have been honoured.
	@printf 'break 0x8\nrun\nquit\n' | $(call RUN,$(SIM)) \
	  | sed 's/\x1b\[[0-9;]*m//g' | grep -q 'not checked on a pipeline' \
	  || { echo "  FAIL test_cli: a pipeline run ignored a breakpoint silently"; exit 1; }
	@printf 'break 0x8\nrun off\nquit\n' | $(call RUN,$(SIM)) \
	  | sed 's/\x1b\[[0-9;]*m//g' | grep -q 'Stop reason *: Breakpoint' \
	  || { echo "  FAIL test_cli: run off did not stop at a breakpoint"; exit 1; }
# `reset` has to clear the performance counters and the recorded fault, not
# just the registers, or a second run in the same session reports the first
# one's verdict. 0xFFFFFFFF carries a reserved opcode, so one word of memory
# is enough to raise a fault without an assembler.
	@printf 'pipe off\nset mem 0 0xFFFFFFFF\nrun\nreset\nstats\nquit\n' \
	  | $(call RUN,$(SIM)) | sed 's/\x1b\[[0-9;]*m//g' > build/cli.out
	@grep -q '^error=none' build/cli.out && grep -q '^cycles=0' build/cli.out \
	  || { echo "  FAIL test_cli: reset left the previous run's fault or counters behind"; exit 1; }
	@rm -f build/cli.out
	@echo "test_cli: all checks passed"

# ── Requirement 5 evaluation ────────────────────────────────────────────────
# One short spelling for the sweep, and a check that actually runs it: a script
# nothing executes is a script that rots. --quick keeps the gate to one program.
ifneq ($(HAVE_ASM),)
test: test-evaluate

evaluate: $(SIM) $(ASM)
	@sh tools/evaluate.sh

test-evaluate: $(SIM) $(ASM)
	@sh tools/evaluate.sh --quick > build/eval.out
	@grep -q 'program' build/eval.out \
	  || { echo "  FAIL test_evaluate: the table printed no header"; exit 1; }
	@test "`grep -c '  built$$' build/eval.out`" = 2 \
	  || { echo "  FAIL test_evaluate: expected one row per pipeline"; exit 1; }
	@grep -q '^loaduse	6	' build/evaluation.tsv \
	  || { echo "  FAIL test_evaluate: the TSV lost its columns"; exit 1; }
# A faulted run must stop the sweep, not be tabulated as a measurement.
	@if PROGRAMS=faulting sh tools/evaluate.sh --quick > build/eval.out 2>&1; then \
	   echo "  FAIL test_evaluate: a faulting program passed as a clean run"; exit 1; \
	 fi
	@grep -q 'divide-by-zero' build/eval.out \
	  || { echo "  FAIL test_evaluate: the runner did not name the fault"; exit 1; }
	@rm -f build/eval.out
	@echo "test_evaluate: all checks passed"
else
evaluate test-evaluate:
	@echo "  The evaluation needs the assembler; '$@' needs src/assembler.cpp."
endif

# ── Demonstration programs ──────────────────────────────────────────────────
# examples/ holds one readable program per thing the project has to show, with
# both assembled images and a recorded transcript committed beside each. The
# gate regenerates the images, compares them byte for byte, runs the program
# and diffs its output, so a program that stops computing what it claims fails
# the suite. `make examples` re-records them, deliberately.
ifneq ($(HAVE_ASM),)
test: test-examples

examples: $(SIM) $(ASM)
	@sh tools/examples.sh --record

test-examples: $(SIM) $(ASM)
	@sh tools/examples.sh
else
examples test-examples:
	@echo "  The examples need the assembler; '$@' needs src/assembler.cpp."
endif


# ── Specification document ────────────────────────────────────────────────
# The PDF is printed from the HTML source, which is what gets edited.
CHROME    ?= google-chrome
SPEC_HTML := RISC201_ISA_Specification.html
SPEC_PDF  := RISC201_ISA_Specification_1.pdf

CLI_HTML  := RISC201_CLI_Guide.html
CLI_PDF   := RISC201_CLI_Guide.pdf

EVAL_HTML := RISC201_Evaluation_Guide.html
EVAL_PDF  := RISC201_Evaluation_Guide.pdf

spec: $(SPEC_PDF)
cli:  $(CLI_PDF)
guide: $(EVAL_PDF)
docs: spec cli guide

$(SPEC_PDF): $(SPEC_HTML)
	$(CHROME) --headless --no-pdf-header-footer \
	  --print-to-pdf=$(CURDIR)/$@ file://$(CURDIR)/$<

$(CLI_PDF): $(CLI_HTML)
	$(CHROME) --headless --no-pdf-header-footer \
	  --print-to-pdf=$(CURDIR)/$@ file://$(CURDIR)/$<

$(EVAL_PDF): $(EVAL_HTML)
	$(CHROME) --headless --no-pdf-header-footer \
	  --print-to-pdf=$(CURDIR)/$@ file://$(CURDIR)/$<

clean:
	$(RMBUILD)

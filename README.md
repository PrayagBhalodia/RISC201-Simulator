# RISC201 Simulator
CSC-201 Processor Design Project

| Document | Covers |
|---|---|
| `RISC201_Evaluation_Guide.pdf` | **Start here.** Build it, run it, and check every part in turn |
| `RISC201_CLI_Guide.pdf` | Every debugger command, with a real session for each |
| `RISC201_ISA_Specification_1.pdf` | The full ISA and microarchitecture specification |
| `examples/README.md` | The demonstration programs: what each shows and how to run it |

Each PDF is printed from the `.html` beside it: `make guide`, `make cli`,
`make spec`, or `make docs` for all three.

---

## Building

```sh
make          # builds build/risc201asm and build/risc201sim
make test     # runs every gate: see "Testing" below
```

Requires: **g++ (C++17)**.

The assembler is a separate deliverable, so the build detects whether its
sources are present: without them `make` builds the simulator alone and
`make asm` says so instead of failing.

```sh
./build/risc201asm examples/quicksort.asm         # -> examples/quicksort.hex
./build/risc201asm --bin examples/quicksort.asm   # -> examples/quicksort.bin
./build/risc201sim examples/quicksort.hex
```

The assembler writes one image per invocation, beside its input: a hex-text
`.hex` by default, or a raw big-endian `.bin` with `--bin`. Only the `.hex`
carries the `; code <n>` header that marks where code ends, so a program with a
`.data` section loaded from `.bin` will execute its own data.

---

## Running

```sh
./build/risc201sim              # loads built-in demo program
./build/risc201sim prog.hex     # loads a hex-text program
./build/risc201sim prog.bin     # loads a raw binary program
```

A program ends when the PC walks past its last instruction, so the ISA spends no
opcode on halting.
The assembler records the code size in a `; code <n>` header at the top of the
`.hex` image, which is how the data it appends after the code is never fetched.
A `.bin` image carries no header and is taken to be code throughout.

---

## Command Reference

`build/risc201sim` is an interactive debugger. **`RISC201_CLI_Guide.pdf`** is the
full reference: every command with its syntax, what it does, and a transcript of
a real session showing the input and the output.

```
load  reset  run  step  disasm  decode  reg  freg  stack  mem  set
break  watch  blist  bdel  pipe  cycle  ustore  info  help  quit
```

`help` lists them in the simulator, `help <command>` explains one.

---

## Testing

`make test` runs every gate and stops at the first failure, so the last line is
the verdict. Each gate can also be run alone.

| Command | Checks |
|---|---|
| `make test-alu` | every ALU operation |
| `make test-mdu` | all four MDU algorithms, iteration by iteration |
| `make test-pipeline` | cycle counts and hazards on both pipelines |
| `make test-control` | the control store, and that each line is driven by exactly one stage |
| `make test-fpu` | every FP operation against the host's binary32, bit for bit |
| `make test-stack` | the three stack boundary faults |
| `make test-program-end` | the PC walking past the last word of code |
| `make test-cli` | the debugger's commands, driven through piped input |
| `make test-asm` | assembles every program in `tests/asm/`, compares each image |
| `make test-evaluate` | the Section 5 sweep, quick form |
| `make test-examples` | runs every program in `examples/` and diffs its whole output |

Two further targets are not part of `make test` because they rewrite committed
files: `make evaluate` for the full timing sweep, and `make examples` to
re-record the demonstration programs' images and transcripts.

A single demonstration program can be checked on its own:

```sh
PROGRAMS=recursion sh tools/examples.sh
```

The simulator exits 0 even after a fault, since a fault is a result and not a
crash, so nothing here judges success by exit status: the machine-readable
verdict is the `error=` key of the `stats` command, which reads `error=none` on
a clean run.

---

## Source layout

```
include/
  state.h       Architectural state (GPRs, PC, SP, LR)
  memory.h      Byte-addressable flat memory + MemoryAccessError
  decode.h      Opcode table, instruction formats, DecodedInstruction
  simulator.h   Fetch-decode-execute loop
  disasm.h      Human-readable disassembly
  loader.h      Binary / hex-text file loader
  debugger.h    Breakpoints, watchpoints, instrumented run loop
  control.h     Microword fields, the control store (Section 6)
  alu.h         class Alu - the execute stage's arithmetic/logic unit
  mdu.h         class Mdu - the multiply/divide unit and its algorithms
  fpu.h         class Fpu - the single-precision floating-point unit
  isa.h         Opcode table shared by the assembler

src/
  state.cpp
  memory.cpp
  decode.cpp
  simulator.cpp
  disasm.cpp
  loader.cpp
  debugger.cpp
  control.cpp          Control store contents, per-instruction control
  alu.cpp              Per-operation results, carry-lookahead adder (Section 2.3)
  mdu.cpp              Booth, shift-and-add, restoring, non-restoring (Section 2.8)
  fpu.cpp              IEEE-754 binary32 in integer operations (Section 2.10)
  assembler.cpp        Two-pass assembler (its own main)
  isa.cpp              Opcode table, register and immediate parsing
  pipeline4/           The 4-stage pipeline, complete and self-contained
    pipeline4.h        class Pipeline4
    pipeline4.cpp      IF, DE/OF, EX/MEM, WB - hazards, cycles, 4-step routines
  pipeline6/           The 6-stage pipeline, complete and self-contained
    pipeline6.h        class Pipeline6
    pipeline6.cpp      IF, ID, OF, EX, MEM, WB - hazards, cycles, 6-step routines
  simulator_main.cpp   Interactive REPL (this is the CLI)

tests/
  test_alu.cpp         ALU gate: make test-alu
  test_mdu.cpp         MDU gate, every algorithm: make test-mdu
  test_pipeline.cpp    Timing gate, both designs: make test-pipeline
  test_control.cpp     Control unit gate: make test-control
  test_fpu.cpp         FPU gate, against the host's binary32: make test-fpu
  test_stack.cpp       Stack boundary gate: make test-stack
  test_program_end.cpp End-of-program gate: make test-program-end
  asm/*.s              Assembler programs; make test-asm assembles each one
  asm/expected/*.hex   and compares it with the image recorded here
  bench/*.s            Benchmarks for the requirement 5 sweep

examples/
  *.asm                Demonstration programs, one per thing to show
  *.hex, *.bin         Both assembled images, committed beside each source
  expected/*.out       Recorded output; make test-examples diffs against it
  README.md            What each program shows and how to run it

tools/
  evaluate.sh          The sweep itself: make evaluate, gated by make test
  examples.sh          The examples gate: make test-examples, make examples
```

`examples/` is the folder to start from if you want to see the processor
work rather than read about it. Unlike `tests/asm/`, whose programs are only
ever assembled, and `tests/bench/`, whose programs exist to be timed, these
are meant to be run and read: each one is gated on what it computes, not just
on whether it still assembles.

Each pipeline directory is independent: `Pipeline4` and `Pipeline6` are
unrelated classes that share no header and no base class, so either pipeline
can be read, changed or graded on its own. They expose the same surface
(`tick`, `stages`, `stageView`, the stall counters), which is what lets the
CLI drive whichever one is selected from one templated code path.

The cost of that independence is duplication: the ISA dependence helpers
(`readsRes` / `writesRes`, the SP and LR resource ids) appear in both `.cpp`
files, so a change to which registers an instruction touches has to be made
twice.

Specialising each file to its own geometry is the payoff. The 4-stage merges
the ALU and the data access into EX/MEM, so every result is ready at the end
of the same stage, no load-use shadow exists at all, and all control resolves
in the single DE/OF stage. The 6-stage splits them, so a load's value lands a
stage later than an ALU result and stalls its consumer in OF, while `B`, `CALL`
and `RET` redirect from ID but the conditional branches must wait for OF.

---

## ALU

`class Alu` is the arithmetic/logic unit of the execute stage. `ALUOp` from the
control word selects the operation and `apply` returns the result:

```cpp
uint32_t r = alu.apply(AluOp::SUB, a, b);
```

There is no condition-code output and no FLAGS register. `BEQ` and `BGT`
compare their two source registers in the decode stage, so no instruction ever
reads a zero, carry or overflow bit. Arithmetic is 32-bit two's complement and
wraps silently, and a shift amount is taken modulo 32.

`ADD` and `SUB` share a **two-level carry-lookahead adder**, written as the gate
structure rather than as the host's `+` - the same treatment the MDU's four
algorithms get. The per-bit generate and propagate
terms are one pair of word operations, eight 4-bit lookahead units produce the
block terms without any carry, a second level of the same unit gives the carry
into each block, and only the two 16-bit halves hand a carry along. `SUB` is
`a + ~b + 1` through the same adder, as §2.3 defines it. `make test-alu` checks
it against the host operators over a 50,000-pair sweep plus every carry that
has to cross a block, half or word boundary.

The multiply and divide codes are executed by the MDU beside the ALU (§2.8);
they share one result bus, so `ALUOp` selects them here too. A zero divisor
comes back from the MDU as `divideByZero` rather than a value, since §2.7
forbids writing rd in that case.

`make test-alu` checks every operation's result; `make test-mdu` checks the MDU
against the host operators and the worked division examples of §2.7.

---

## MDU

`class Mdu` holds **four algorithms**, each written as the iteration the
hardware performs, and `pipe` chooses which pair runs:

| Name | Unit | Cycles | Adder operations |
| ---- | ---- | ------ | ---------------- |
| `booth` (default) | multiplier | 32 | where two neighbouring bits of rs2 differ |
| `shift-and-add` | multiplier | 32 | one per set bit of `abs(rs2)` |
| `non-restoring` (default) | divider | 33 | 32, plus the correction when needed |
| `restoring` | divider | 32 + restores | 32, plus one per restore |
| `default` | either | 1 | none - the host's own `*` or `/` |

```
pipe 4 shift-and-add restoring     # swap both units
pipe 6 default default             # the one-cycle floor
```

Every pair computes the same value; only the cycles and the adder count move,
which is what makes the Section 5 table a comparison rather than two different
machines. Restoring is the one algorithm whose latency its operands decide - a
trial subtraction succeeds exactly where the quotient takes a 1 bit, so L is
`64 - popcount(quotient)` - so both pipelines ask the unit what each individual
division will cost instead of holding a figure per instruction.

`make evaluate` runs the whole sweep. On the `arith` benchmark Booth and
shift-and-add both take 1652 cycles on the 4-stage design and separate only in
the adder column, 1176 against 984.

---

## FPU

`class Fpu` is the single-precision floating-point unit, built the same way as
`Alu`: stateless, selected by `ALUOp`, and reporting the IEEE exception flags it
raised rather than writing them, so the caller decides what to commit. The
control word's `FpuEn` line is what routes an instruction here (§6.2).

It is **IEEE-754 binary32 implemented in integer operations** - unpack sign,
exponent and mantissa, restore the implicit leading 1, operate, normalize, round
once, pack - not a wrapper around the host's `float`. Subnormals are supported
rather than flushed, rounding is to nearest even, and three guard bits below the
significand keep a sticky bit so the single rounding step at the end sees the
whole exact result (§2.10, §2.11).

Eight instructions use it. `FADD`, `FSUB`, `FMUL` and `FDIV` are the arithmetic;
`FEQ` and `FGT` write an integer 0 or 1 so the existing branch unit can act on
them (`BGT rd, R0, target` when true), and `ITOF` and `FTOI` convert.

There is no `FLT` opcode. Less-than is `FGT` with the operands exchanged, which
the assembler spells `FLT` - exactly as `BLT` is spelled for `BGT`. Writing it
as `NOT FEQ AND NOT FGT` would be wrong: every comparison against a NaN is
false, so both negations are true and the conjunction claims `a < b`. There is
no `FLE`, `FNEG` or `FABS` either: `FLT OR FEQ` gives `<=`, and the integer
`XOR` and `AND` already flip and clear a sign bit.

FP exceptions never stop the machine. The five IEEE conditions accumulate in
`State::fpsr`, a sticky register the `freg` command shows, so `FDIV x, 0` gives
a signed infinity where integer `DIV x, 0` still halts.

`make test-fpu` differential-tests every arithmetic operation against the host's
own binary32, bit for bit, over an edge list and a seeded random corpus. The
host is not a valid oracle for `FTOI` out of range - a C cast there is undefined
behaviour - so those cases are asserted against the §2.10 table directly.

---

## Stack

The stack is **full-descending**: SP addresses the last word pushed and starts
at the top of memory, `PUSH` pre-decrements, `POP` post-increments. There is no
SP-relative addressing mode, so `PUSH` and `POP` are the only instructions that
move SP or reach a stack slot - which is what makes the boundary checks below
decidable rather than heuristic.

`LR` lives outside the register file, so no register field can name it. The
S-format **L bit** (bit 21) is its only path to memory, written `PUSH LR` and
`POP LR`. Without it a nested `CALL` would overwrite its caller's return
address, so recursion depends on it; `tests/asm/recursion.s` is the proof.

Three boundaries are enforced in the memory stage, before the access, so a
faulting instruction writes nothing and leaves SP where it was (§2.12):

| Fault | Raised by |
|---|---|
| `stack overflow` | `PUSH` writing below the program image |
| `stack underflow` | `POP` with SP at the stack base |
| `stack corruption` | `ST` to an address at or above SP, i.e. inside a live frame |

They report through the same path as a misaligned access, and because the
check sits in the shared execute semantics, both pipelines inherit it with no
code of their own.

`stack` renders what is live. RISC201 has no frame pointer, and nothing in
memory tells a saved LR from a saved GPR, so the simulator records the address
as each `PUSH LR` executes and drops it when SP rises past it. Those marks are
where the renderer cuts one frame from the next:

```
risc201> stack
  base 0x00010000   SP 0x0000FFF0   limit 0x00000048  (program image end)
  4 word(s) in use, 2 frame(s), 65448 byte(s) before overflow
                  +-------------------------------------+  base  frame 1
  0x0000FFFC      | 0x00000008  saved LR -> 0x00000008  |
  0x0000FFF8      | 0x00000005  5                       |
                  +-------------------------------------+  frame 2
  0x0000FFF4      | 0x00000030  saved LR -> 0x00000030  |
  0x0000FFF0 SP ->| 0x00000004  4                       |
                  +-------------------------------------+  free space down to the image end
```

`make test-stack` crosses each boundary by exactly one word and checks that the
frame marks unwind with the stack.

---

## Macros

The assembler runs a preprocessor over the source before pass 1. It lifts out
`.macro` definitions and replaces each invocation with the body, so neither
pass learns that macros exist and a body can hold anything the assembler
already accepts, including a pseudo-instruction or another macro.

```asm
.macro ENTER saved              ; a callee's prologue
        PUSHM LR, \saved
.endm

.macro LEAVE saved              ; ... and its epilogue, same list
        POPM  LR, \saved
        RET
.endm

SQUARE: ENTER R4
        MUL   R5, R4, R4
        LEAVE R4
```

`\p` is the operand passed as parameter `p`; `\@` is a number unique to each
expansion, which a body that defines a label needs (`POS\@:`) or the second
invocation would define it twice. Nesting stops at depth 32, which is how a
self-invoking macro is caught - the preprocessor cannot see a base case,
because the branch that would end the recursion is not evaluated until the
program runs.

Errors name both ends of the expansion, because either may hold the mistake:

```
Error, line 19 (ABS, macro line 12): bad register 'R99' (use R0 to R15, or LR with PUSH/POP)
```

`PUSHM` / `POPM` are built in rather than written as macros, because they take
a register list of any length while macro parameters are positional. `POPM`
pops in reverse, so it takes the identical list to the `PUSHM` it undoes and
the full-descending discipline stops being the programmer's problem.

`tests/asm/macros.s` runs all of this; `tests/asm/macro_errors.s` is the
rejection fixture.

---

## Control Unit

Execution is microprogrammed, not hard-coded. A 64-entry control store holds
one 22-bit horizontal microword per opcode, with the 14 unencoded fields of
spec §6.2; the opcode field of IR is the control store address, so there is no
mapping ROM and no micro-sequencer (§6.1). Every architectural effect - which
operand reaches each ALU input, what the ALU or MDU does, whether memory is
read or written, where the write-back value comes from, whether SP moves, how
the PC changes - is driven by that word. `Simulator::execute` contains no
per-opcode switch.

Each opcode also owns a **microroutine**: one microinstruction per pipeline
stage, which is its control word projected onto the lines that stage consumes
(§6.5). The microPC is `opcode:step`, and it advances as the instruction moves
down the pipeline:

```
risc201> ustore LD
  uPC    stage    microinstruction
  12:0   IF       -
  12:1   ID       -
  12:2   OF       -
  12:3   EX       ALUSrcB=imm  ALUOp=ADD
  12:4   MEM      MemRead=1
  12:5   WB       RegWrite=1  WBSrc=mem
  union of all steps = 0x220880  (Section 6.2 control word)
```

The stage field sets partition the microword, so every control line is driven
by exactly one stage - a line assigned to none would never reach the datapath,
and one assigned to two would be driven twice. `make test-control` enforces
that for both pipelines, and checks the worked control words of §6.4
along with the claim that each word truthfully predicts what executing its
instruction does.

Because the 4-stage merges the ALU and the memory access, and the 6-stage
splits PCSel (ID) from CondSel (OF), the same opcode decomposes differently in
each pipeline. That difference is exactly why a conditional branch costs an
extra bubble in the 6-stage.

---

## Scope

A functional single-cycle model plus a timing model for both pipelines.
All 37 instructions execute, including the eight floating-point ones, with the
exceptions of ISA §2.7 and §2.12.

`pipe 4` and `pipe 6` add stage occupancy, hazards and cycle counts on top of
the same execute semantics. Both pipelines are in-order with full forwarding,
so running each instruction at a single point against the architectural
register file gives exactly what the forwarding paths would; the timing model
adds cycles, never a second copy of the ISA. Branch handling is static
predict-not-taken, so only a taken branch costs bubbles.

---

## Specification document

`RISC201_ISA_Specification.pdf` is the generated ISA specification. After changing its source, regenerate it with:

```sh
make spec     # needs google-chrome; override with CHROME=<browser>
```

---

## Demo session

```
./build/risc201sim test_add.hex

risc201[PC=0x00000000]> disasm 0 3
--------------------------------------------------------------
  Disassembly
--------------------------------------------------------------
 > 0x00000000:  0x34400003   ADDI    R1, R0, #3  (0x3)
   0x00000004:  0x34800004   ADDI    R2, R0, #4  (0x4)
   0x00000008:  0x04C48000   ADD     R3, R1, R2
--------------------------------------------------------------

risc201[PC=0x00000000]> step
  > 0x00000000:  0x34400003   ADDI    R1, R0, #3  (0x3)
  R0  (zero) = 0x00000000     R1         = 0x00000003     R2         = 0x00000000     R3         = 0x00000000
  ... (rest of the register table) ...

risc201[PC=0x00000004]> break 0x00000008
  Breakpoint #1 set at 0x00000008

risc201[PC=0x00000004]> run off
  Stop reason : Breakpoint
  Detail      : breakpoint #1 hit at 0x8 (hit count: 1)
```

A bare `run` uses the 6-stage pipeline, whose loop ticks a clock rather than
consulting the debugger, so breakpoints and watchpoints need `run off` - the
functional model. The simulator says so rather than letting one look honoured.

Exceptions report the address of the faulting instruction:

```
  Detail      : divide-by-zero exception at PC=0x00000008
  Detail      : illegal opcode 0x25 at PC=0x00000004
```

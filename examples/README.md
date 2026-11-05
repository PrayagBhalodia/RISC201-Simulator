# RISC201 examples

Ten programs, one per thing the project has to demonstrate.
Each has its source, both assembled images, and a recorded transcript of its own output committed beside it.

`make test-examples` regenerates every image, compares it with the committed one byte for byte, runs the program, and diffs its output against the recorded transcript.
It is part of `make test`, so a program that stops computing what it claims fails the suite rather than failing at the demo.

## Layout

| File | What it is |
| --- | --- |
| `<name>.asm` | the source |
| `<name>.hex` | the hex image, with the `; code <n>` header that marks where code ends |
| `<name>.bin` | the same image as raw big-endian bytes, with no header |
| `<name>.cmds` | the commands its transcript was recorded from, when the common set is not enough |
| `expected/<name>.out` | the recorded transcript |

The common command set is `pipe 4`, `run`, `stats`, `reg`, `stack`, `quit`.

## Running one

```sh
./build/risc201asm examples/quicksort.asm        # -> examples/quicksort.hex
./build/risc201asm --bin examples/quicksort.asm  # -> examples/quicksort.bin
./build/risc201sim examples/quicksort.hex        # interactive
```

Non-interactively, which is how the transcripts are made:

```sh
printf 'pipe 4\nrun\nstats\nreg\nquit\n' | ./build/risc201sim examples/quicksort.hex
```

## Re-recording, after a deliberate change

```sh
make examples        # rewrites every image and transcript
git diff examples/   # read this before committing
```

Images are checked before transcripts, so a change to a source needs the image re-recorded before the transcript check is even reached.
That order is deliberate: image drift is the cheaper mistake and the one worth reporting first.

## The programs

Measured figures below are from the committed transcripts, on the 4-stage pipeline.

| Program | Cycles | Instructions | Outcome |
| --- | --- | --- | --- |
| `addition` | 10 | 6 | clean |
| `function_call` | 65 | 22 | clean |
| `recursion` | 2799 | 2088 | clean |
| `multiply_divide` | 207 | 14 | clean |
| `bit_reverse` | 10860 | 8550 | clean |
| `quicksort` | 611 | 451 | clean |
| `div_by_zero` | 41 | 5 | `divide-by-zero exception at PC=0x00000010` |
| `stack_overflow` | 65526 | 49142 | `stack overflow at PC=0x00000004` |
| `stack_underflow` | 8 | 4 | `stack underflow at PC=0x0000000C` |
| `misaligned` | 8 | 4 | `unaligned word read at address=0x2` |

### addition

The shortest complete program, and the only one with no `.data` section.

```sh
printf 'disasm 0 6\nrun\nreg\nquit\n' | ./build/risc201sim examples/addition.hex
```

Look at `R3 = 0x2A` (42) and `R6 = 0x54` (84).
`R6` is computed as `ADD R6, R3, R3`, not with a multiply: doubling costs one cycle where `MULI` would cost 32 in the MDU.

Because there is no data section, this is the one program whose `.bin` behaves exactly like its `.hex`, and the gate runs both and requires identical output.
Every other program has data, and a raw image has nowhere to record where code ends, so loading one of those from `.bin` would execute its data.
That is what the `; code <n>` header in a `.hex` exists to prevent (spec Section 2.9).

```sh
./build/risc201sim examples/addition.bin    # identical behaviour
```

### function_call

Leaf against non-leaf.
`SUM2` calls nothing, so its return address is safe where it is.
`AVG3` calls twice, so the second `CALL` would overwrite the first one's return address - and `LR` sits outside the register file, so no instruction can copy it to a register and the stack is its only hiding place.

Look at `R3 = 0x11` (17, the mean of 12, 30 and 9) and `R4 = 9`: `R4` comes back as the caller left it, because `ENTER`/`LEAVE` saved and restored it.
`PUSH` copies a register to the stack without disturbing it, which is why the body can still read `R4` freely between the two.

### recursion

Recursive Fibonacci, `fib(10) = 55`.
Two recursive calls per frame, which is what makes a frame's bookkeeping visible: `fib(n-1)` has to stay alive across the second call, and with no SP-relative addressing the only way to read a saved word back is to `POP` it.

```sh
printf 'pipe off\nbreak 0x40\nrun\nstack all\nquit\n' | ./build/risc201sim examples/recursion.hex
```

Breakpoints are only checked when no pipeline is selected, so the session starts with `pipe off`; a pipeline run says so rather than appearing to honour a breakpoint.
`stack all` then draws one frame per live call, each with its `saved LR ->` mark - the simulator records those as each `PUSH LR` executes, because nothing in memory distinguishes a saved return address from a saved register.

### multiply_divide

The requirement-5 arithmetic comparison, in one readable program. Six operations, each chosen so that changing the MDU's algorithm moves a counter.

| `pipe 4 <mul> <div>` | cycles | mdu_stalls | mdu_adder_ops |
| --- | --- | --- | --- |
| `booth non-restoring` | 207 | 189 | 133 |
| `shift-and-add non-restoring` | 207 | 189 | 146 |
| `booth restoring` | 282 | 264 | 209 |
| `shift-and-add restoring` | 282 | 264 | 222 |

Measured per multiply, with the pattern in `rs2` - the only operand Booth recodes:

| `rs2` | Booth | shift-and-add |
| --- | --- | --- |
| `0x7FFFFFFF` | 2 | 31 |
| `0x55555555` | 32 | 16 |
| `0xFFFFFFFF` (-1) | 1 | 1 |

Two things worth saying out loud.
The multipliers take the same 32 cycles either way, so they separate only in the adder column - and which one wins is a property of the data, not of the algorithm: Booth leads here 133 to 146, and loses on `tests/bench/arith` 1176 to 984.
The dividers separate in cycles instead, because restoring pays one extra cycle per restore and a trial subtraction succeeds exactly where the quotient takes a 1 bit, so its latency is decided by the operands.

### bit_reverse

Two algorithms for the same answer, and a self-check strong enough to tell them apart.
`REVLOOP` moves one bit per iteration, 32 iterations.
`REVSWAP` halves the problem five times - neighbouring bits, pairs, nibbles, bytes, halves - at six instructions a pass.

Measured in isolation on one input: 271 cycles and 202 instructions for the loop, 62 and 56 for the five-step version.

Look at `R10` and `R11`, both `0x1E6A2C48` (`0x12345678` reversed), and `R12 = 1`.
`R12` is the verdict of a sweep over all 32 single-bit inputs: bit k must come back at bit 31-k.
One input is not enough, and that is not hypothetical - corrupt the nibble mask from `0x0F0F0F0F` to `0x0E0F0F0F` and `0x12345678` still reverses correctly, because the altered bit never reaches a place where it shows. The sweep catches it on the first mismatching probe.

### quicksort

The centrepiece: recursion, pointer partitioning, and macro preprocessing.
Sorts `29, 7, 41, 3, 18, 55, 12, 8, 33, 1` into `1, 3, 7, 8, 12, 18, 29, 33, 41, 55`.

```sh
printf 'pipe 4\nmem 0xF4 10\nrun\nmem 0xF4 10\nreg\nquit\n' | ./build/risc201sim examples/quicksort.hex
```

`0xF4` is where this program's data section starts; the assembler prints it on the `data:` line, and it moves if the code size changes.

Look at the two `mem` dumps either side of the run, and at `R10 = 1` with `R11 = 0xCF` (207).
The program checks its own work: `R10` is 1 only if every neighbour ended in ascending order, and `R11` is the element sum, which a correct sort leaves unchanged.
Asserting on registers keeps the check independent of where the data happened to land.

The preprocessing is visible in the listing:

```sh
printf 'disasm 0x84 4\nquit\n' | ./build/risc201sim examples/quicksort.hex
```

One `SWAPW R5, R6, R8, R9` in the source is four instructions here - two loads and two stores.
Neither assembler pass ever learns that `SWAPW` exists; the preprocessor rewrites the source before pass 1, so by the time addresses are assigned there is nothing left but ordinary instructions.
The same listing shows `BGE R6, R2, PEND` as a `BEQ` followed by a `BGT`, because there is no single greater-or-equal opcode.

Three ISA facts shaped this program, and each is worth a sentence:
pointers rather than indices, because `SHL` takes its shift amount from a register and scaling an index by 4 would otherwise cost a 32-cycle `MULI`;
`PUSH LR` on the recursive path, because `LR` lives outside the register file;
and plain `PUSH`/`POP` rather than the `ENTER`/`LEAVE` pair, because `POPM` pops its list in reverse and that only works when nothing else is pushed in between.

### The four fault programs

Each one halts on a different runtime exception, so each needs its own file: a fault stops the machine.
In every case the check runs before the access, so the faulting instruction writes nothing.

| Program | What it does | `error=` |
| --- | --- | --- |
| `div_by_zero` | divides correctly, then by a register holding 0 | `divide-by-zero exception at PC=0x00000010` |
| `stack_overflow` | pushes until the stack reaches the program image | `stack overflow at PC=0x00000004: PUSH to 0x0000000C is below the program image end 0x00000010` |
| `stack_underflow` | a balanced push and pop, then one pop too many | `stack underflow at PC=0x0000000C: POP with SP=0x00010000 at the stack base 0x00010000` |
| `misaligned` | reads from an address that is not a multiple of 4 | `unaligned word read at address=0x2` |

`div_by_zero` shows the architectural consequence as well as the message: `R5` is still 0, because the faulting instruction wrote no destination, and `R6` is still 0 because the instruction after it never ran.

The simulator exits 0 even after a fault - a fault is a result, not a crash - so the gate reads the `error=` key of the `stats` block rather than the exit status.
That is also why each fault program is listed as faulting in `tools/examples.sh`: the exact text is pinned by its transcript, and the list only says which way round the verdict has to come out.

## Known limits

- `break` and `watch` are only consulted when no pipeline is selected. Start a debugging session with `pipe off`.
- `stats` prints six keys with the pipeline off and twelve with one selected. The per-hazard counters need a `pipe 4` or `pipe 6` first.
- Addresses in the command lines above are literals. They move when a program's code size changes, and the transcript diff is what notices.
- The transcripts record the memory size, since SP starts at the top of it. The gate pins `RISC201_MEM_KB=64`.
- `mem` prints its decimal column unsigned while `stack` prints signed, so a negative word reads as a large number in a `mem` dump.

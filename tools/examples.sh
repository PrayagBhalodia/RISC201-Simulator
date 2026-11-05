#!/bin/sh
# The examples/ gate: assemble every demo program, compare both images with the
# committed ones, run it, and compare the output with the recorded transcript.
#
# Images are rebuilt in a work directory rather than beside their source,
# because the committed .hex and .bin are themselves the expected output and
# assembling over them would destroy what the comparison is against.
#
# The simulator exits 0 even after a fault, so the verdict comes from the
# error= key of the stats block.
set -eu

ASM=${ASM:-build/risc201asm}
SIM=${SIM:-build/risc201sim}
DIR=${DIR:-examples}

PROGRAMS=${PROGRAMS:-"addition function_call recursion multiply_divide
                      bit_reverse quicksort div_by_zero stack_overflow
                      stack_underflow misaligned"}

# The programs whose whole point is to stop on a runtime exception. The exact
# fault text is pinned by each one's recorded transcript; this list only says
# which way round the verdict has to come out.
FAULTING="div_by_zero stack_overflow stack_underflow misaligned"

# Pinned, not inherited: memory size sets SP and therefore every stack address
# the transcripts record.
RISC201_MEM_KB=64
export RISC201_MEM_KB

record=no
[ "${1:-}" = "--record" ] && record=yes || true

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The commands piped into the simulator: a program's own .cmds file when it has
# one, otherwise the common set.
commands() {
    if [ -f "$DIR/$1.cmds" ]; then
        cat "$DIR/$1.cmds"
    else
        printf 'pipe 4\nrun\nstats\nreg\nstack\nquit\n'
    fi
}

faulting() {
    case " $FAULTING " in
        *" $1 "*) return 0 ;;
    esac
    return 1
}

fail() {
    echo "  FAIL $1: $2" >&2
    exit 1
}

for prog in $PROGRAMS; do
    cp "$DIR/$prog.asm" "$work/$prog.asm"
    "$ASM"       "$work/$prog.asm" > /dev/null
    "$ASM" --bin "$work/$prog.asm" > /dev/null

    if [ "$record" = yes ]; then
        cp "$work/$prog.hex" "$DIR/$prog.hex"
        cp "$work/$prog.bin" "$DIR/$prog.bin"
    else
        cmp -s "$work/$prog.hex" "$DIR/$prog.hex" \
            || fail "$prog" "the hex image differs from the committed one"
        cmp -s "$work/$prog.bin" "$DIR/$prog.bin" \
            || fail "$prog" "the binary image differs from the committed one"
    fi

    commands "$prog" | "$SIM" "$DIR/$prog.hex" 2>&1 \
        | sed 's/\x1b\[[0-9;]*m//g' > "$work/$prog.out"

    err=$(sed -n 's/^error=//p' "$work/$prog.out" | head -1)
    [ -n "$err" ] || fail "$prog" "the stats block has no error= key"

    if faulting "$prog"; then
        [ "$err" != "none" ] \
            || fail "$prog" "expected a runtime fault, got error=none"
    else
        [ "$err" = "none" ] \
            || fail "$prog" "expected a clean run, got error=$err"
    fi

    if [ "$record" = yes ]; then
        mkdir -p "$DIR/expected"
        cp "$work/$prog.out" "$DIR/expected/$prog.out"
        echo "  recorded  $prog"
    else
        diff -u "$DIR/expected/$prog.out" "$work/$prog.out" \
            || fail "$prog" "output differs from the recorded transcript"
        echo "  ok   $prog"
    fi
done

# addition.asm has no .data section, which is the one case where a raw image
# says the same thing as a hex one: with nothing after the code, there is no
# code/data split for the missing "; code <n>" header to lose. Any program with
# data would execute its data here, so only this one is run both ways. Guarded
# so that restricting PROGRAMS to one program tests only that program.
case " $PROGRAMS " in *" addition "*)
    hex=$(printf 'pipe 4\nrun\nstats\nquit\n' | "$SIM" "$DIR/addition.hex" 2>&1 \
            | sed 's/\x1b\[[0-9;]*m//g' | sed -n '/^pipeline=/,$p')
    bin=$(printf 'pipe 4\nrun\nstats\nquit\n' | "$SIM" "$DIR/addition.bin" 2>&1 \
            | sed 's/\x1b\[[0-9;]*m//g' | sed -n '/^pipeline=/,$p')
    [ "$hex" = "$bin" ] \
        || fail addition "the .bin image does not run the same as the .hex image"
    echo "  ok   addition (.bin runs identically)"
    ;;
esac

if [ "$record" = yes ]; then
    echo "examples: images and transcripts recorded for $(echo $PROGRAMS | wc -w) program(s)"
else
    echo "test_examples: all checks passed"
fi

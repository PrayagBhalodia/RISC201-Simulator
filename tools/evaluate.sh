#!/bin/sh
# Requirement 5 evaluation: every benchmark on both pipelines as built, then
# the MDU algorithm sweep on the one benchmark whose time the MDU dominates -
# each multiplier and each divider against the host operators that stand in
# for a combinational unit.
#
# Reads the simulator's `stats` key=value block rather than scraping its human
# output, and checks the error= key rather than the exit status, because the
# simulator reports a fault in its output and still exits 0.
set -eu

ASM=${ASM:-build/risc201asm}
SIM=${SIM:-build/risc201sim}
BENCH=${BENCH:-tests/bench}
OUT=${OUT:-build/evaluation.tsv}

# --quick is the CI gate: one program, both pipelines, so the script itself is
# known to run end to end without spending the full sweep. An explicit
# PROGRAMS always wins, which is how the gate points it at a faulting program.
if [ "${1:-}" = "--quick" ]; then
    PROGRAMS=${PROGRAMS:-loaduse}
else
    PROGRAMS=${PROGRAMS:-"branchy loaduse arith calls mixed"}
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for prog in $PROGRAMS; do
    "$ASM" "$BENCH/$prog.s" > /dev/null
done

value() { sed -n "s/^$2=//p" "$1" | head -1; }

# One row of the table: run `prog` on an n-stage pipeline with the given MDU
# algorithms and append the figures to $OUT.
measure() {
    prog=$1
    stages=$2
    mul=$3
    div=$4
    note=$5

    printf 'pipe %s %s %s\nrun\nstats\n' "$stages" "$mul" "$div" \
        | "$SIM" "$BENCH/$prog.hex" > "$work/out" 2>&1

    for key in cycles instructions mdu_stalls load_use_stalls branch_stalls \
               squashed mdu_adder_ops error; do
        grep -q "^$key=" "$work/out" || {
            echo "  FAIL: the simulator's stats block has no '$key' key" >&2
            exit 1
        }
    done

    err=$(value "$work/out" error)
    if [ "$err" != "none" ]; then
        echo "  FAIL $prog on the $stages-stage pipeline: $err" >&2
        exit 1
    fi

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$prog" "$stages" "$mul" "$div" \
        "$(value "$work/out" cycles)" \
        "$(value "$work/out" instructions)" \
        "$(value "$work/out" mdu_stalls)" \
        "$(value "$work/out" load_use_stalls)" \
        "$(value "$work/out" branch_stalls)" \
        "$(value "$work/out" squashed)" \
        "$(value "$work/out" mdu_adder_ops)" \
        "$note" >> "$OUT"
}

mkdir -p "$(dirname "$OUT")"
printf 'program\tstages\tmul\tdiv\tcycles\tinstrs\tmdu_stall\tloaduse\tbranch\tsquash\tadder_ops\tnote\n' > "$OUT"

# The requirement 1a comparison: both designs, every program, as built.
for prog in $PROGRAMS; do
    for stages in 4 6; do
        measure "$prog" "$stages" booth non-restoring built
    done
done

# Section 5's algorithm sweep, on the benchmark the MDU dominates. Booth and
# shift-and-add take the same 32 cycles and separate only in the adder column;
# restoring and non-restoring separate in both. `default` is the host operator
# in one cycle, which is the combinational bound the four are read against.
case " $PROGRAMS " in
    *" arith "*)
        for stages in 4 6; do
            measure arith "$stages" shift-and-add non-restoring 'multiplier sweep'
            measure arith "$stages" booth restoring 'divider sweep'
            measure arith "$stages" default non-restoring 'bound: host multiplier'
            measure arith "$stages" default default 'bound: host multiplier and divider'
        done
        ;;
esac

awk -F'\t' '
NR == 1 {
    printf "  %-9s %-6s %-14s %-14s %8s %7s %7s %9s %8s %7s %10s  %s\n",
           "program", "stages", "mul", "div", "cycles", "instrs", "CPI",
           "mdu_stall", "loaduse", "branch", "adder_ops", "note"
    next
}
{
    printf "  %-9s %-6s %-14s %-14s %8s %7s %7.2f %9s %8s %7s %10s  %s\n",
           $1, $2, $3, $4, $5, $6, ($6 ? $5 / $6 : 0), $7, $8, $9, $11, $12
}
END { printf "\n  %d row(s) written to '"$OUT"'\n", NR - 1 }
' "$OUT"

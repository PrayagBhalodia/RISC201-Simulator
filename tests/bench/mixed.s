; a mix of the four pressures above in rough proportion, so the comparison is
; not read off one pathological shape: a load feeding its consumer, a branch
; either way, a call per iteration and one MDU operation inside it.

.data
ARR:    .word 7, 3, 9, 1, 8, 2, 6, 4
N:      .word 8
OUT:    .space 1

.text
START:  LI   R1, ARR
        LD   R2, N(R0)
        CLR  R7
LOOP:   LD   R3, 0(R1)
        ADD  R7, R7, R3
        BGT  R3, R0, POSITIVE   ; every element here is positive, so taken
        SUBI R7, R7, 1
POSITIVE:
        CALL SCALE
        ADDI R1, R1, 4
        SUBI R2, R2, 1
        BGT  R2, R0, LOOP
        ST   R7, OUT(R0)
        B    DONE

SCALE:  MULI R7, R7, 3
        RET
DONE:

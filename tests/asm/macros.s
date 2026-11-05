; Macro expansion and the full-descending stack (ISA Section 2.13).

.macro ENTER saved              ; a callee's prologue
        PUSHM LR, \saved
.endm

.macro LEAVE saved              ; ... and its epilogue, same list
        POPM  LR, \saved
        RET
.endm

.macro ABS rd, rs               ; \@ keeps the label unique per expansion
        ADD  \rd, \rs, R0
        BGE  \rd, R0, POS\@
        SUB  \rd, R0, \rd
POS\@:
.endm

.macro ABSSUM rd, ra, rb        ; a macro that invokes another one
        ABS  \rd, \ra
        ABS  R12, \rb
        ADD  \rd, \rd, R12
.endm

.data
A:      .word -7
B:      .word 3
SUM:    .space 1
SQ:     .space 1

.text
START:  LD   R1, A(R0)
        LD   R2, B(R0)
HERE:   ABSSUM R3, R1, R2       ; a label on an invocation: |-7| + |3| = 10
        ST   R3, SUM(R0)

        LI   R4, 4
        CALL SQUARE
        ST   R5, SQ(R0)         ; 16
        B    DONE

SQUARE: ENTER R4
        MUL  R5, R4, R4
        LEAVE R4
DONE:

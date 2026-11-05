; Recursive factorial: the reason the S-format L bit exists.
; A nested CALL overwrites LR, so a recursive routine has to save it on the
; stack, and LR is outside the register file - PUSH LR is its only path there.
.data
N:      .word 5
RESULT: .space 1

.text
START:  LD   R1, N(R0)
        CALL FACT
        ST   R2, RESULT(R0)     ; 120
        B    DONE

FACT:   BGT  R1, R0, RECURSE    ; R2 = R1!
        LI   R2, 1              ; 0! = 1
        RET
RECURSE:
        PUSH LR                 ; the CALL below clobbers it
        PUSH R1
        SUBI R1, R1, 1
        CALL FACT
        POP  R1
        POP  LR
        MUL  R2, R2, R1
        RET
DONE:

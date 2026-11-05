; call-heavy: recursive factorial, so the run is dominated by CALL and RET
; bubbles and by the stack traffic of saving a return address. This is the
; frame discipline of Section 2.12 - PUSH LR first, the locals below it.

.data
ARG:    .word 8
RES:    .space 1

.text
START:  LD   R1, ARG(R0)
        CALL FACT
        ST   R2, RES(R0)
        B    DONE

FACT:   BGT  R1, R0, RECURSE    ; base case: 0! = 1
        ADDI R2, R0, 1
        RET
RECURSE:
        PUSH LR                 ; the CALL below overwrites it
        PUSH R1                 ; the local
        SUBI R1, R1, 1
        CALL FACT
        POP  R1
        POP  LR
        MUL  R2, R2, R1
        RET
DONE:

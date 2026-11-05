; Function calls: the calling convention, and why a non-leaf must save LR.
;
;   arguments      R1, R2 (and R4 here, for a third)
;   return value   R3
;   callee-saved   whatever the callee names in ENTER / LEAVE
;
; AVG3 calls SUM2 twice, which makes it a non-leaf: the second CALL would
; overwrite the return address the first one left in LR. LR lives outside the
; register file, so no instruction can copy it to a register and the stack is
; its only hiding place (ISA Section 2.12).

.macro ENTER saved              ; callee prologue: LR plus one register
        PUSHM LR, \saved
.endm

.macro LEAVE saved              ; epilogue: the identical list, popped in reverse
        POPM  LR, \saved
        RET
.endm

.data
A:      .word 12
B:      .word 30
C:      .word 9
AVG:    .space 1

.text
START:  LD   R1, A(R0)
        LD   R2, B(R0)
        LD   R4, C(R0)
        CALL AVG3
        ST   R3, AVG(R0)        ; (12 + 30 + 9) / 3 = 17
        B    DONE

; SUM2 is a leaf: it calls nothing, so LR is safe where it is.
;   in R1, R2   out R3   clobbers nothing else
SUM2:   ADD  R3, R1, R2
        RET

; AVG3 is a non-leaf. PUSH copies R4 to the stack without disturbing the
; register, so the body still reads R4 freely; the saved copy is there to hand
; the caller its value back.
;   in R1, R2, R4   out R3
AVG3:   ENTER R4
        CALL SUM2               ; R3 = R1 + R2
        MOV  R1, R3
        MOV  R2, R4
        CALL SUM2               ; R3 = R1 + R2 + R4
        LI   R5, 3
        DIV  R3, R3, R5         ; the mean, through the MDU
        LEAVE R4
DONE:

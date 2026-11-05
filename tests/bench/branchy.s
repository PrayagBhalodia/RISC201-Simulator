; branch-heavy: a nested countdown, so nearly every conditional branch is
; taken. Isolates the taken-branch penalty, which is one bubble on the 4-stage
; design and two on the 6-stage one (Sections 3.2 and 4.2, Case 4).

.data
OUTER:  .word 20
INNER:  .word 10
ACC:    .space 1

.text
START:  LD   R1, OUTER(R0)
        CLR  R4
OLOOP:  LD   R2, INNER(R0)      ; the inner count is reloaded each pass
ILOOP:  ADDI R4, R4, 1
        SUBI R2, R2, 1
        BGT  R2, R0, ILOOP      ; taken nine times in ten
        SUBI R1, R1, 1
        BGT  R1, R0, OLOOP      ; taken nineteen times in twenty
        ST   R4, ACC(R0)
DONE:

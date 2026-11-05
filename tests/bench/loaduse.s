; load-use-heavy: every loaded word feeds the instruction straight after it.
; The 4-stage design pays nothing, because its merged EX/MEM stage has a
; loaded value ready exactly when an ALU result would be; the 6-stage design
; pays one stall per load (Section 3.2 Case 3 against Section 4.2 Case 3).

.data
ARR:    .word 3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9, 3
N:      .word 16
SUM:    .space 1

.text
START:  LI   R1, ARR
        LD   R2, N(R0)
        CLR  R3
LOOP:   LD   R4, 0(R1)          ; the loaded value ...
        ADD  R3, R3, R4         ; ... consumed by the very next instruction
        ADDI R1, R1, 4
        SUBI R2, R2, 1
        BGT  R2, R0, LOOP
        ST   R3, SUM(R0)
DONE:

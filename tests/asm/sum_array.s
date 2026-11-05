; sum of an array
.data
ARR:   .word 5, 10, 15, 20
N:     .word 4
RES:   .space 1

.text
        LD   R2, N(R0)        ; R2 = count
        LI   R1, ARR          ; R1 = pointer
        CLR  R3               ; R3 = sum
LOOP:   LD   R4, 0(R1)
        ADD  R3, R3, R4
        ADDI R1, R1, 4
        SUBI R2, R2, 1
        BGT  R2, R0, LOOP
        ST   R3, RES(R0)

; pass 2 errors
        ADD  R1, R2
        ADD  R16, R1, R2
        ADDI R1, R1, 200000
        DIVI R1, R1, 0
        LD   R1, R2
        BEQ  R1, R2, nowhere
        B    D
        MOVHI R1, 0x40000
        LI   R1, 0x100000000
.data
D:      .word 1

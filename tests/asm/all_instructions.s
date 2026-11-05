; every real instruction and pseudo-instruction once
start:  nop
        add  r1, r2, r3
        sub  R1,R2 , R3
        MUL  R3, R1, R2        ; spec example: 0x0CC48000
        DIV  R4, R5, R6
        MOD  R4, R5, R6
        AND  R7, R8, R9
        OR   R7, R8, R9
        XOR  R7, R8, R9
        NOT  R10, R11
        SHL  R12, R13, R14
        SRL  R12, R13, R14
        SRA  R12, R13, R14
        ADDI R1, R1, 0x1F
        SUBI R1, R1, -8
        MULI R3, R1, -5        ; spec example: 0x3CC7FFFB
        DIVI R3, R1, 2
        MODI R3, R1, 2
        LD   R3, -8(R1)
        ST   R3, 12(R15)
        MOV  R5, R6
        MOVHI R5, 0x3FFFF
        PUSH R14
        POP  R13
        CALL func
        B    done
func:   RET
done:   BEQ  R1, R2, start
        BGT  R1, R2, next
next:   LI   R1, 0x12345678
        LI   R2, -1
        LI   R3, 0x2000
        CLR  R4
        BLT  R1, R2, start
        BGE  R1, R2, start
        BLE  R1, R2, start
        PUSHM LR, R1, R2
        POPM  LR, R1, R2
        FADD R1, R2, R3
        FSUB R1, R2, R3
        FMUL R1, R2, R3
        FDIV R1, R2, R3
        FEQ  R4, R5, R6
        FLT  R4, R5, R6        ; pseudo: FGT R4, R6, R5
        FGT  R4, R5, R6
        ITOF R7, R8
        FTOI R9, R10
        FLI  R11, 3.14159      ; assembles to 0x40490FD0
        FLI  R12, -0.5
        .word 0xFFFFFFFF, -2
        .float 1.0, -2.5, 0.0

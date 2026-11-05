; Von Neumann test: code and data share one memory.
; The .data section is written first here, but the assembler still
; puts all the code at address 0 and the data straight after it.
.data
VAL:    .word 7, -3
PTR:    .word FUNC          ; address of code stored as data
LOOKS:  .word 0x04000000    ; a valid ADD encoding, but it is data
OUT:    .space 2

.text
START:  LD   R1, VAL(R0)        ; R1 = 7
        LD   R2, START(R0)      ; reads the first instruction as data (same memory)
        LD   R3, PTR(R0)        ; R3 = address of FUNC
        CALL FUNC
        ST   R1, OUT(R0)
        B    END
FUNC:   ADDI R1, R1, 1
        RET
END:

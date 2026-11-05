; Stack register names, which pass 2 rejects.
.text
        PUSH  R16               ; no such register
        POP   SP                ; SP is implied, never an operand
        PUSHM R1, PC            ; and neither is PC

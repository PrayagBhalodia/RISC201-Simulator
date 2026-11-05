; A runtime exception: divide by zero (ISA Section 2.7).
;
; The assembler refuses DIVI with a literal zero immediate, so the zero
; divisor has to arrive in a register. The fault is recorded rather than
; thrown: the machine halts, the instruction after it never runs, neither rd
; nor anything else is written, and the simulator still exits 0 - `stats`
; carries the verdict in its error= key.

.text
START:  ADDI R1, R0, 144
        ADDI R2, R0, 12
        DIV  R3, R1, R2         ; 144 / 12 = 12, a perfectly ordinary divide
        CLR  R4                 ; the divisor the hardware will object to
        DIV  R5, R1, R4         ; divide-by-zero exception here; R5 stays 0
        ADDI R6, R0, 99         ; never reached, so R6 stays 0 too
DONE:

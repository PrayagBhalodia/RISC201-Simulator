; Not a benchmark: a program that raises a runtime exception, used by
; test-evaluate to prove the runner refuses to report figures from a faulted
; run. The assembler rejects a zero immediate for DIVI, so the zero divisor
; has to arrive in a register (Section 2.6).

.text
START:  ADDI R1, R0, 10
        CLR  R2                 ; the divisor the hardware will object to
        DIV  R3, R1, R2         ; divide-by-zero exception, Section 2.7
        ADDI R4, R0, 99         ; squashed: never reached
DONE:

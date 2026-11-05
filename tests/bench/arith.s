; arithmetic-heavy: a multiply, a divide and a remainder every iteration, so
; the MDU holds the execute stage for most of the run. The two multiplies pass
; their pattern as rs2, the operand Booth recodes: one long run of 1s against
; strict alternation, which is where the adder count varies most while the 32
; cycles do not (Section 2.8).

.data
RUNOF1S:  .word -1              ; 0xFFFFFFFF: one run, a single adder operation
ALTBITS:  .word 0x55555555      ; every bit pair differs: 32, the worst case
TRIPS:    .word 12
ACC:      .space 1

.text
START:  LD   R5, RUNOF1S(R0)
        LD   R6, ALTBITS(R0)
        LD   R1, TRIPS(R0)
        CLR  R7
LOOP:   MUL  R2, R1, R5         ; the cheap pattern, as the recoded operand
        MUL  R3, R1, R6         ; and the expensive one
        ADD  R7, R7, R2
        DIV  R4, R3, R1         ; R1 is never 0: the branch below keeps it >= 1
        MOD  R8, R3, R1         ; the same divider, its other output
        ADD  R7, R7, R4
        ADD  R7, R7, R8
        SUBI R1, R1, 1
        BGT  R1, R0, LOOP
        ST   R7, ACC(R0)
DONE:

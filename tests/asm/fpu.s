; Floating point: integers in, a mean out, and back to an integer.
; The ISA has no BNE, so a loop counter is tested with BGT against R0, and a
; comparison result - which FEQ, FLT and FGT make 0 or 1 - is branched on the
; same way.
.data
VALS:   .word 12, 7, 20, 5, 36      ; the integers to average: 80 in total
COUNT:  .word 5
SCALE:  .float 2.5
LIMIT:  .float 20.0
MEAN:   .space 1                    ; the mean, as a float
IMEAN:  .space 1                    ; the same, truncated to an integer
SCALED: .space 1
GEFLAG: .space 1

.text
START:  LI   R1, VALS               ; pointer into the array
        LD   R2, COUNT(R0)          ; how many are left
        CLR  R3                     ; running total; integer 0 is also +0.0

SUM:    LD   R4, 0(R1)              ; one element, as an integer
        ITOF R4, R4                 ; ... and as a float
        FADD R3, R3, R4
        ADDI R1, R1, 4
        SUBI R2, R2, 1
        BGT  R2, R0, SUM            ; the counter never goes negative

        LD   R5, COUNT(R0)
        ITOF R5, R5                 ; the divisor has to be a float too
        FDIV R6, R3, R5             ; 80.0 / 5.0 = 16.0
        ST   R6, MEAN(R0)
        FTOI R7, R6                 ; back to an integer, truncating
        ST   R7, IMEAN(R0)

        LD   R8, SCALE(R0)          ; 2.5, straight from .float
        FMUL R9, R6, R8             ; 16.0 * 2.5 = 40.0
        ST   R9, SCALED(R0)

        ; a >= b has no opcode of its own: FGT or FEQ builds it.
        LD   R10, LIMIT(R0)         ; 20.0
        FGT  R11, R9, R10
        FEQ  R12, R9, R10
        OR   R11, R11, R12          ; 40.0 >= 20.0, so this is 1
        ST   R11, GEFLAG(R0)

        BGT  R11, R0, DONE          ; branch taken when the comparison was true
        FLI  R13, -1.0              ; reached only if it was not
        ST   R13, GEFLAG(R0)
DONE:

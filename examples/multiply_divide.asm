; MDU algorithm analysis (ISA Sections 2.7 and 2.8, spec Section 5).
;
; Six operations, each chosen so that switching the MDU's algorithm moves a
; counter you can read. Run the program under each pair and compare `stats`:
;
;   pipe 4 booth         non-restoring
;   pipe 4 shift-and-add non-restoring
;   pipe 4 booth         restoring
;   pipe 4 shift-and-add restoring
;
; mdu_adder_ops separates the two multipliers; mdu_stalls separates the two
; dividers. Everything stays in registers so that the counters measure the
; arithmetic and not the bookkeeping around it.
;
; Only rs2 is Booth-recoded, so in every MUL below the interesting operand is
; the second one.

.data
RUNPOS: .word 0x7FFFFFFF        ; 31 ones in a positive word
ALTBIT: .word 0x55555555        ; every other bit set
MINUS1: .word 0xFFFFFFFF        ; -1: all ones, but a magnitude of just 1
BIGNUM: .word 0x55555555        ; dividend giving a high-popcount quotient
SMALLQ: .word 400               ; dividend giving a quotient of 4

.text
START:  ADDI R1, R0, 3          ; the multiplicand, kept small and boring

; Booth's best case: a run of 1s is two transitions however long it is, so
; Booth drives the adder twice where shift-and-add drives it once per set bit.
        LD   R2, RUNPOS(R0)
        MUL  R3, R1, R2

; Booth's worst case: alternating bits put a transition at every position, so
; Booth pays 32 where shift-and-add pays 16.
        LD   R4, ALTBIT(R0)
        MUL  R5, R1, R4

; The same cost by two different routes: all ones is a single transition for
; Booth, and shift-and-add takes magnitudes first, so -1 has a magnitude of 1.
; One adder operation either way.
        LD   R6, MINUS1(R0)
        MUL  R7, R1, R6

; Division. Non-restoring is a flat 33 cycles. Restoring instead pays one
; extra cycle per restore, and a trial subtraction succeeds exactly where the
; quotient takes a 1 bit - so its latency is decided by the operands, which is
; why both pipelines ask the MDU for a per-operation cost instead of using a
; constant.
        LD   R8, BIGNUM(R0)
        ADDI R9, R0, 1
        DIV  R10, R8, R9        ; quotient 0x55555555: half its bits set

        LD   R11, SMALLQ(R0)
        ADDI R12, R0, 100
        DIV  R13, R11, R12      ; quotient 4: one bit set

        MOD  R14, R11, R12      ; 400 mod 100 = 0, the remainder path
DONE:

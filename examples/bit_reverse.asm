; Reversing the 32 bits of a register, two ways.
;
; The second routine is the interesting one and it is also the one that is easy
; to get subtly wrong, so this program checks it against the first over all 32
; single-bit inputs: bit k must come back at bit 31-k, and any wrong mask or
; wrong shift amount misroutes at least one of them.
;
; One input would not be enough, and that is not a hypothetical. Corrupt the
; nibble mask below from 0x0F0F0F0F to 0x0E0F0F0F and 0x12345678 still
; reverses correctly - the altered bit never reaches the place where the
; difference shows. The sweep catches it immediately.
;
; Neither the shifts nor AND take an immediate in this ISA - SHL, SRL and SRA
; are register-register (Section 2.3) - so every shift amount and every mask
; has to be loaded into a register first. That is what makes the five steps
; worth wrapping in a macro.

; REVSTEP exchanges the groups of \k bits that mask \m selects. \t1 and \t2 are
; the caller's scratch registers: a macro has no way to invent one.
.macro REVSTEP x, m, k, t1, t2
        AND  \t1, \x, \m        ; the bits that travel left
        SHL  \t1, \t1, \k
        NOT  \t2, \m
        AND  \t2, \x, \t2       ; the bits that travel right
        SRL  \t2, \t2, \k
        OR   \x, \t1, \t2
.endm

.data
IN:     .word 0x12345678        ; reverses to 0x1E6A2C48
OUTLP:  .space 1                ; the loop routine's answer
OUTSW:  .space 1                ; the five-step routine's answer

.text
; One named case first, so the transcript carries a readable answer.
START:  LD   R1, IN(R0)
        CALL REVLOOP
        ST   R2, OUTLP(R0)
        MOV  R10, R2
        LD   R1, IN(R0)
        CALL REVSWAP
        ST   R2, OUTSW(R0)
        MOV  R11, R2

; Then the sweep. R12 ends as 1 only if the two routines agreed on every one
; of the 32 single-bit inputs.
        ADDI R12, R0, 1         ; agreeing, until they do not
        ADDI R13, R0, 1         ; the probe: one bit, walking up
        ADDI R14, R0, 32        ; inputs left to try
TLOOP:  MOV  R1, R13
        CALL REVLOOP
        MOV  R8, R2             ; the oracle's answer
        MOV  R1, R13
        CALL REVSWAP
        BEQ  R8, R2, TNEXT
        CLR  R12                ; a mismatch, and the verdict is final
TNEXT:  ADD  R13, R13, R13      ; the next bit up
        SUBI R14, R14, 1
        BGT  R14, R0, TLOOP
        B    DONE

; REVLOOP: R2 = R1 reversed, one bit per iteration. R1 is consumed.
; The constant 1 serves twice over, as the shift amount and as the mask that
; picks out the source's low bit.
REVLOOP:
        CLR  R2
        LI   R3, 32             ; bits remaining
        LI   R4, 1
RLOOP:  SHL  R2, R2, R4         ; make room in the result
        AND  R5, R1, R4         ; take the source's low bit
        OR   R2, R2, R5         ; and plant it
        SRL  R1, R1, R4         ; on to the next source bit
        SUBI R3, R3, 1
        BGT  R3, R0, RLOOP
        RET

; REVSWAP: R2 = R1 reversed in five passes over the whole word - swap
; neighbouring bits, then pairs, then nibbles, then bytes, then halves.
; log2(32) = 5 passes instead of 32, at six instructions a pass.
REVSWAP:
        MOV  R2, R1
        LI   R3, 0x55555555
        LI   R4, 1
        REVSTEP R2, R3, R4, R6, R7
        LI   R3, 0x33333333
        LI   R4, 2
        REVSTEP R2, R3, R4, R6, R7
        LI   R3, 0x0F0F0F0F
        LI   R4, 4
        REVSTEP R2, R3, R4, R6, R7
        LI   R3, 0x00FF00FF
        LI   R4, 8
        REVSTEP R2, R3, R4, R6, R7
        LI   R3, 0x0000FFFF
        LI   R4, 16
        REVSTEP R2, R3, R4, R6, R7
        RET
DONE:

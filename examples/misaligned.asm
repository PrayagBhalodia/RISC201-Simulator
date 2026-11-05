; A runtime exception: an unaligned word access (ISA Section 2.9).
;
; Every RISC201 access moves a whole 32-bit word and every address must be a
; multiple of 4. There is no byte load, so the bytes straddling a boundary are
; not reachable at all: the simulator raises a memory exception rather than
; wrapping the address or returning the bytes that happen to be there.

.data
VAL:    .word 0x11223344

.text
START:  LD   R1, VAL(R0)        ; aligned, and R0 is the hardwired zero base
        LI   R2, 2              ; not a multiple of 4
        LD   R3, 0(R2)          ; unaligned word read exception here
        ADDI R4, R0, 99         ; never reached
DONE:

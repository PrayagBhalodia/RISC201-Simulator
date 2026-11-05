; A runtime exception: stack underflow (ISA Section 2.12).
;
; A balanced push and pop first, to show that the stack itself is fine; then
; one pop too many. SP is back at the stack base, so there is nothing to take
; and the check refuses the access before SP can move.

.text
START:  ADDI R1, R0, 7
        PUSH R1
        POP  R2                 ; balanced: R2 = 7, SP back at the base
        POP  R3                 ; stack underflow exception here
        ADDI R4, R0, 99         ; never reached
DONE:

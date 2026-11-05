; A runtime exception: stack overflow (ISA Section 2.12).
;
; SP starts one word past the top of memory and PUSH pre-decrements, so a loop
; that never pops walks the stack down until it reaches the end of the program
; image. The boundary check runs before the write, so the faulting PUSH stores
; nothing and leaves SP where it was - R1 is therefore exactly the number of
; words that did fit.

.text
START:  CLR  R1
LOOP:   PUSH R1
        ADDI R1, R1, 1
        B    LOOP               ; no exit: the boundary is the exit
DONE:

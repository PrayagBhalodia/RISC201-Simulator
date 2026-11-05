; Recursion and the stack: Fibonacci, two recursive calls per frame.
;
; Each frame saves LR plus whatever has to survive a CALL, so the stack grows
; to the depth of the recursion and `stack` shows one frame per live call with
; its saved return address marked. Factorial demonstrates the same mechanism
; one call at a time; two calls per frame is what makes a frame's bookkeeping
; visible, because fib(n-1) has to be kept alive across the second call.

.data
N:      .word 10
RESULT: .space 1

.text
START:  LD   R1, N(R0)
        CALL FIB
        ST   R2, RESULT(R0)     ; fib(10) = 55
        B    DONE

; FIB: in R1 = n, out R2 = fib(n). Clobbers R3 and R4.
FIB:    LI   R3, 2
        BGE  R1, R3, FREC       ; n >= 2 recurses
        MOV  R2, R1             ; fib(0) = 0, fib(1) = 1
        RET

FREC:   PUSH LR                 ; both CALLs below overwrite it
        PUSH R1                 ; n, needed again for the second call
        SUBI R1, R1, 1
        CALL FIB                ; R2 = fib(n-1)
        POP  R1
        PUSH R2                 ; fib(n-1), needed after the second call
        PUSH R1
        SUBI R1, R1, 2
        CALL FIB                ; R2 = fib(n-2)
        POP  R1
        POP  R4                 ; fib(n-1), back off the stack
        ADD  R2, R2, R4
        POP  LR
        RET
DONE:

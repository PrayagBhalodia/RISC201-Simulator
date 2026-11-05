; Quicksort: recursion, pointer arithmetic, and macro preprocessing.
;
; This is the program to read if you want to see how RISC201's constraints
; shape real code. Three of them decide almost every line below.
;
; 1. There is no shift-immediate. SHL, SRL and SRA are register-register
;    (Section 2.3), so `SHL rd, ri, 2` does not assemble and scaling an index
;    by four costs either a register holding the constant or a MULI, and MULI
;    goes through the MDU for 32 cycles. So this sort walks POINTERS, not
;    indices: `lo` and `hi` are addresses, and stepping to the next element is
;    ADDI by 4.
; 2. LR lives outside the register file and only PUSH and POP can reach it
;    (Section 2.12). A routine that calls must therefore save LR on the stack,
;    and QSORT calls twice.
; 3. POPM pops its list in reverse, which is what lets ENTER/LEAVE take the
;    identical operand list. That only works when nothing else is pushed in
;    between - and QSORT pushes the partition pointer mid-frame - so QSORT
;    uses plain PUSH and POP rather than the macro pair that
;    examples/function_call.asm uses.
;
; The preprocessor runs over this file before pass 1, so neither assembler
; pass ever learns that SWAPW exists; by the time addresses are assigned, each
; invocation is already four ordinary instructions. `disasm` on the assembled
; image is where you can see that.

; SWAPW exchanges the words that two pointers address. \t1 and \t2 are the
; caller's scratch registers: a macro has no way to invent one.
.macro SWAPW pa, pb, t1, t2
        LD   \t1, 0(\pa)
        LD   \t2, 0(\pb)
        ST   \t2, 0(\pa)
        ST   \t1, 0(\pb)
.endm

.data
ARR:    .word 29, 7, 41, 3, 18, 55, 12, 8, 33, 1
N:      .word 10

.text
START:  LI   R1, ARR            ; lo = &A[0]
        LD   R2, N(R0)
        SUBI R2, R2, 1
        ADD  R2, R2, R2         ; (n-1) x 2
        ADD  R2, R2, R2         ; (n-1) x 4: doubling twice, for the reason above
        LI   R3, ARR
        ADD  R2, R3, R2         ; hi = &A[n-1]
        CALL QSORT
        B    CHECK

; QSORT sorts A[lo..hi] in place.
;   in  R1 = lo pointer, R2 = hi pointer
;   clobbers R1-R9
;
; One frame is three words: LR, hi, and the partition pointer. R1 is
; deliberately not saved - the right half's lo comes from the returned
; partition pointer, so the left recursion is free to clobber it.
QSORT:  BGE  R1, R2, QRET       ; one element or none is already sorted
        PUSH LR                 ; both CALLs below overwrite it
        PUSH R2                 ; hi has to outlive the first recursion
        CALL PART               ; R3 = where the pivot came to rest
        PUSH R3
        SUBI R2, R3, 4          ; sort A[lo .. p-1]
        CALL QSORT
        POP  R3
        ADDI R1, R3, 4          ; sort A[p+1 .. hi]
        POP  R2
        CALL QSORT
        POP  LR
QRET:   RET

; PART: Lomuto partition of A[lo..hi] about the pivot A[hi].
;   in  R1 = lo, R2 = hi (both pointers, and neither is written)
;   out R3 = the pivot's resting place
;   clobbers R4-R9
;
; Lomuto rather than Hoare because its inner loop needs one comparison
; against one pivot value held in a register, which is all this ISA offers:
; there are no condition flags and no BNE, so every test is a register-to-
; register BEQ or BGT. PART calls nothing, so LR is safe where it is.
PART:   LD   R4, 0(R2)          ; the pivot value
        SUBI R5, R1, 4          ; i starts one word before the window
        MOV  R6, R1             ; j = lo
PLOOP:  BGE  R6, R2, PEND       ; stop when j reaches the pivot's own slot
        LD   R7, 0(R6)
        BGT  R7, R4, PNEXT      ; only values <= pivot belong on the left
        ADDI R5, R5, 4
        SWAPW R5, R6, R8, R9    ; one source line, four instructions.
                                ; When i = j this swaps a slot with itself,
                                ; which is a no-op rather than a corruption.
PNEXT:  ADDI R6, R6, 4
        B    PLOOP
PEND:   ADDI R5, R5, 4          ; the pivot's home
        SWAPW R5, R2, R8, R9
        MOV  R3, R5
        RET

; The program checks its own work, so the result is readable off the register
; table and does not depend on where the data section happened to land.
;   R10 = 1 if every neighbour is in ascending order
;   R11 = the sum of the elements, unchanged by a correct sort
CHECK:  LI   R1, ARR
        LD   R2, N(R0)
        SUBI R2, R2, 1          ; one comparison per adjacent pair
        ADDI R10, R0, 1         ; sorted, until proven otherwise
        LD   R11, 0(R1)         ; the running sum starts with A[0]
CLOOP:  LD   R5, 0(R1)
        LD   R6, 4(R1)
        ADD  R11, R11, R6
        BGT  R5, R6, CBAD
        B    CNEXT
CBAD:   CLR  R10
CNEXT:  ADDI R1, R1, 4
        SUBI R2, R2, 1
        BGT  R2, R0, CLOOP
DONE:

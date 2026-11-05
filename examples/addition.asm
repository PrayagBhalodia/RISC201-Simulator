; Addition: the shortest complete RISC201 program.
;
; No .data section, deliberately. A .bin image has nowhere to record the
; code/data split, so a program with data executes its data when loaded that
; way. With code only, this program's .hex and .bin images behave identically,
; which is what makes it the one to load both ways.

.text
START:  ADDI R1, R0, 17         ; R1 = 17, built from the hardwired zero
        ADDI R2, R0, 25         ; R2 = 25
        ADD  R3, R1, R2         ; R3 = 42
        SUB  R4, R2, R1         ; R4 = 8
        ADDI R5, R3, -42        ; R5 = 0: the immediate is sign-extended
        ADD  R6, R3, R3         ; R6 = 84. Doubling is how this ISA multiplies
                                ; by two without paying the MDU's 32 cycles.
DONE:

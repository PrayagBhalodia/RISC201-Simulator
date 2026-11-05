; Every macro error the preprocessor has to catch, so none of them assembles.
.macro ADD a                    ; would hide a real instruction
        NOP
.endm

.macro GOOD a
        MOV R1, \b              ; \b is not a parameter
.endm

.macro GOOD a                   ; defined twice
        NOP
.endm

.macro LOOPS
        LOOPS                   ; no base case
.endm

.text
        GOOD R1, R2             ; one parameter, two operands
        GOOD R1                 ; right count, but the body names \b
        LOOPS
.endm                           ; no matching .macro

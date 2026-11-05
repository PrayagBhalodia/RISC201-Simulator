; every line below is an error (pass 1 errors only; pass 2 is skipped)
        FOO  R1, R2
        .bogus 3
.data
        ADD  R1, R2, R3
        .space -1
.text
X:      NOP
X:      NOP
1bad:   NOP
        ADD  R1,, R2

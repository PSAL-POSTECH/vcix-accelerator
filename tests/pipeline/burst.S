# Six instructions the pipelined model owns, back to back: more than its depth of four.
    .text
    .globl main
main:
    li      t0, 8
    vsetvli t0, t0, e32, m1, ta, ma
    .rept 16
    nop
    .endr
    sf.vc.x 0x1, 0x2, 0x3, a0
    sf.vc.x 0x1, 0x2, 0x3, a0
    sf.vc.x 0x1, 0x2, 0x3, a0
    sf.vc.x 0x1, 0x2, 0x3, a0
    sf.vc.x 0x1, 0x2, 0x3, a0
    sf.vc.x 0x1, 0x2, 0x3, a0
    li      a0, 0
    ret

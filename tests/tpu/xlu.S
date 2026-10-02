# Tiles through the cross-lane unit: depth and lane exchanged, an all-gather, a push during a pass.
    .option norvc
    .text
    .globl main
main:
    li      t0, 4
    vsetvli t0, t0, e32, m1, ta, ma
    .rept 16
    nop
    .endr
    .balign 64
    sf.vc.iv  0x3, 0, v1, 4
    sf.vc.iv  0x3, 0, v1, 4
    sf.vc.v.i 0x1, 0, v2, 4
    sf.vc.v.i 0x1, 0, v2, 4
    .rept 128
    nop
    .endr
    .balign 64
    sf.vc.iv  0x3, 0, v1, 5
    sf.vc.v.i 0x1, 0, v2, 5
    sf.vc.v.i 0x1, 0, v2, 5
    .rept 128
    nop
    .endr
    .balign 64
    sf.vc.iv  0x3, 0, v1, 5
    sf.vc.iv  0x3, 0, v1, 5
    sf.vc.v.i 0x1, 0, v2, 5
    sf.vc.iv  0x3, 0, v1, 5
    sf.vc.v.i 0x1, 0, v2, 5
    li      a0, 0
    ret

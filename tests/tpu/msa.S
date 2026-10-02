# A weight push, a push of four inputs and its pop: the field at 19:15 of the first push has [4] set.
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
    sf.vc.iv  0x2, 0x0, v1, -16
    sf.vc.iv  0x2, 0x0, v1, 0
    sf.vc.v.i 0x3, 0x0, v2, 0
    li      a0, 0
    ret

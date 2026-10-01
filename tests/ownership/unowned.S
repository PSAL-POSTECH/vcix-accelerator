# One instruction the owns_one model owns, then one in custom-2 that it does not.
    .text
    .globl main
main:
    li      t0, 8
    vsetvli t0, t0, e32, m1, ta, ma
    sf.vc.x 0x1, 0x2, 0x3, a0
    sf.vc.x 0x2, 0x2, 0x3, a0
    li      a0, 0
    ret

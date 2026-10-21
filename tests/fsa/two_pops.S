# A second pop of the one normalized O: never ends.
    .option norvc
    .text
    .globl main
main:
    li      t0, 4
    vsetvli t0, t0, e16, m1, ta, ma
    sf.vc.iv    0x0, 0, v1, 0
    sf.vc.iv    0x1, 0, v2, 0
    sf.vc.iv    0x2, 0, v3, 0
    sf.vc.i     0x0, 0, 0, 0
    sf.vc.i     0x1, 0, 0, 0
    vsetvli t0, t0, e32, m1, ta, ma
    sf.vc.v.i   0x0, 0, v8, 0
    sf.vc.v.i   0x0, 0, v9, 0
    li      a0, 0
    ret

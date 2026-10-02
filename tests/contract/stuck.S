# A pop and nothing to feed it.
    .text
    .globl main
main:
    li      t0, 8
    vsetvli t0, t0, e32, m1, ta, ma
    sf.vc.v.x 0x1, 0x2, v3, a0
    li      a0, 0
    ret

# Returns vstart as read right after a model-owned instruction that began with vstart = 3.
    .text
    .globl main
main:
    li      t0, 8
    vsetvli t0, t0, e32, m1, ta, ma
    li      t1, 3
    csrw    vstart, t1
    sf.vc.x 0x1, 0x2, 0x3, a0
    csrr    a0, vstart
    ret

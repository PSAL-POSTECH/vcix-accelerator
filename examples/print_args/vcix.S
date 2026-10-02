# Seven sf.vc.* forms, v.vv again under LMUL=2, and one custom-1 instruction; checks nothing.
    .text
    .globl main
main:
    li      t0, 8
    vsetvli t0, t0, e32, m1, ta, ma
    la      t1, vec_a
    vle32.v v1, (t1)
    la      t1, vec_b
    vle32.v v2, (t1)
    li      a0, 0x1234
    li      a1, 42
    la      t1, scalar
    flw     fa0, 0(t1)

    sf.vc.x     0x1, 0x2, 0x3, a0
    sf.vc.i     0x3, 0x2, 0x3, 5
    sf.vc.vv    0x1, 0x4, v2, v1
    sf.vc.v.vv  0x3, v3, v2, v1
    sf.vc.v.xv  0x2, v3, v2, a1
    sf.vc.v.fv  0x1, v5, v2, fa0
    sf.vc.v.ivv 0x3, v4, v2, 7

    li      t0, 8
    vsetvli t0, t0, e32, m2, ta, ma
    sf.vc.v.vv  0x3, v4, v2, v6

    .insn r 0x2b, 0, 0, x0, a0, a1

    li      a0, 0
    ret

    .data
    .p2align 2
vec_a:  .float 10, 11, 12, 13, 14, 15, 16, 17
vec_b:  .float 1, 2, 3, 4, 5, 6, 7, 8
scalar: .float 2.5

# A vexp and a weight push that reads it, so that nothing older is in flight; then the one-cycle instructions.
    .option norvc
    .text
    .globl main
main:
    li      t0, 8
    vsetvli t0, t0, e32, m1, ta, ma
    li      a0, 0
    li      a1, 0
    .rept 16
    nop
    .endr
    .balign 64
    sf.vc.v.iv  0x3, v4, v3, 0
    sf.vc.iv    0x1, 0, v4, 0
    sf.vc.v.i   0x0, 0, v1, 0
    sf.vc.i     0x1, 0, 0, 4
    .insn r 0x2b, 3, 7, x0, a0, a1
    .insn r 0x2b, 3, 2, x0, a0, a1
    .insn r 0x2b, 3, 1, x0, a0, a1
    .insn r 0x2b, 3, 14, x0, a0, a1
    .insn r 0x2b, 3, 3, x0, a0, a1
    li      a0, 0
    ret

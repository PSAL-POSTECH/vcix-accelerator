# A pop with nothing to pop, the push that feeds it, and a read of what the pop writes. Then a pop
# behind a jump the core mispredicts, never fed, and at the jump's target a read of the same register.
    .option norvc
    .text
    .globl main
main:
    li      t0, 8
    vsetvli t0, t0, e32, m1, ta, ma
    la      t3, right
    li      t1, 100
    li      t2, 7
    .rept 16
    nop
    .endr
    .balign 64
    sf.vc.v.x 0x1, 0x2, v3, a0
    sf.vc.x   0x1, 0x2, 0x3, a0
    sf.vc.xv  0x1, 0x4, v3, a0
    .rept 16
    nop
    .endr
    .balign 64
    div     t1, t1, t2
    jr      t3
    sf.vc.v.x 0x1, 0x2, v3, a0
right:
    .rept 16
    nop
    .endr
    sf.vc.xv  0x1, 0x4, v3, a0
    li      a0, 0
    ret

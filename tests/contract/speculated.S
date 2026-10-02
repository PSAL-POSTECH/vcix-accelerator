# One wait that commits, two behind a jump the core has not seen before and one at its target; the divide keeps the jump from committing, and all share a fetch line so the two are there to issue.
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
    sf.vc.x 0x2, 0x2, 0x3, a0
    div     t1, t1, t2
    jr      t3
    sf.vc.x 0x2, 0x2, 0x3, a0
    sf.vc.x 0x2, 0x2, 0x3, a0
right:
    .rept 16
    nop
    .endr
    sf.vc.x 0x2, 0x2, 0x3, a0
    li      a0, 0
    ret

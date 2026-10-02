# A load, a pop and a use of what the pop writes: the load waits for memory at the head of the queue longer than the pop takes, and nothing else in the core moves.
    .option norvc
    .text
    .globl main
main:
    li      t0, 8
    vsetvli t0, t0, e32, m1, ta, ma
    la      t4, far
    .rept 16
    nop
    .endr
    .balign 64
    ld      t5, 0(t4)
    sf.vc.v.x 0x1, 0x2, v3, a0
    sf.vc.xv  0x1, 0x4, v3, a0
    .rept 16
    nop
    .endr
    li      a0, 0
    ret

    .data
    .balign 64
far:
    .space 64

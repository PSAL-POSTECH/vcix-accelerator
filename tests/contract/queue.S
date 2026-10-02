# Four commands back to back and a wait; then two more at the end of a fetch line, so the core runs out of instructions while they are worked on, and a wait.
    .option norvc
    .text
    .globl main
main:
    li      t0, 8
    vsetvli t0, t0, e32, m1, ta, ma
    .rept 16
    nop
    .endr
    .rept 4
    sf.vc.x 0x1, 0x2, 0x3, a0
    .endr
    sf.vc.x 0x2, 0x2, 0x3, a0
    .balign 64
    .rept 14
    nop
    .endr
    sf.vc.x 0x1, 0x2, 0x3, a0
    sf.vc.x 0x1, 0x2, 0x3, a0
    .rept 16
    nop
    .endr
    sf.vc.x 0x2, 0x2, 0x3, a0
    li      a0, 0
    ret

# A push of four and its pop; then sixteen elements and no pop until the output queue is full.
    .option norvc
    .text
    .globl main
main:
    li      t0, 4
    vsetvli t0, t0, e32, m1, ta, ma
    .rept 16
    nop
    .endr
    .balign 64
    sf.vc.iv  0x1, 0x0, v1, 0
    sf.vc.iv  0x0, 0x0, v1, 0
    sf.vc.v.i 0x2, 0x0, v2, 0
    .rept 128
    nop
    .endr
    .balign 64
    .rept 4
    sf.vc.iv  0x0, 0x0, v1, 0
    .endr
    .rept 128
    nop
    .endr
    .balign 64
    sf.vc.iv  0x1, 0x0, v1, 0
    sf.vc.iv  0x1, 0x0, v1, 0
    sf.vc.v.i 0x2, 0x0, v2, 0
    sf.vc.iv  0x1, 0x0, v1, 0
    li      a0, 0
    ret

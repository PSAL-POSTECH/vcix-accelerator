# Five vexp back to back, then the other six special functions, each reading the one before it.
    .option norvc
    .text
    .globl main
main:
    li      t0, 8
    vsetvli t0, t0, e32, m1, ta, ma
    .rept 16
    nop
    .endr
    .balign 64
    sf.vc.v.iv 0x3, v8, v1, 0
    sf.vc.v.iv 0x3, v9, v2, 0
    sf.vc.v.iv 0x3, v10, v3, 0
    sf.vc.v.iv 0x3, v11, v4, 0
    sf.vc.v.iv 0x3, v12, v5, 0
    sf.vc.v.iv 0x1, v13, v12, 0
    sf.vc.v.iv 0x0, v14, v13, 0
    sf.vc.v.iv 0x2, v15, v14, 0
    sf.vc.v.iv 0x2, v16, v15, 1
    sf.vc.v.iv 0x2, v17, v16, 2
    sf.vc.v.iv 0x2, v18, v17, 3
    li      a0, 0
    ret

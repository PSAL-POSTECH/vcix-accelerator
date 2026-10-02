# Forty owned instructions back to back: more than the in-order queue of gem5's default
# pool holds. The nops keep them clear of the refetch that follows vsetvli.
    .text
    .globl main
main:
    li      t0, 8
    vsetvli t0, t0, e32, m1, ta, ma
    .rept 16
    nop
    .endr
    .rept 40
    sf.vc.x 0x1, 0x2, 0x3, a0
    .endr
    li      a0, 0
    ret

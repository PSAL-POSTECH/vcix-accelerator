# attn_score with no load_stationary before it: never accepted.
    .option norvc
    .text
    .globl main
main:
    li      t0, 4
    vsetvli t0, t0, e16, m1, ta, ma
    sf.vc.iv    0x1, 0, v2, 0
    li      a0, 0
    ret

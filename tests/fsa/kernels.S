# FlashAttention on FSA, at run.sh's KERNEL_BASE, where Spike runs a vector instruction in every lane.
# k_attention(a0 = Q in lane 0's scratchpad, a1 = O, a2 = N, a3 = tiles): K and V follow Q, a tile of N rows of halves
# each, K_j at Q + 2N(1 + j), V_j at Q + 2N(1 + tiles + j); O is N rows of singles. Returns N.
    .option norvc
    .section .kernel, "ax"
    .globl k_attention
k_attention:
    vsetvli t0, a2, e16, m1, ta, ma
    slli    t1, a2, 1
    add     t3, a0, t1
    mul     t4, t1, a3
    add     t4, t3, t4
    vle16.v v1, (a0)
    vle16.v v2, (t3)
    vle16.v v3, (t4)
    sf.vc.iv    0x0, 0, v1, 0
    sf.vc.iv    0x1, 0, v2, 0
    sf.vc.iv    0x2, 0, v3, 0
1:  addi    a3, a3, -1
    beqz    a3, 2f
    add     t3, t3, t1
    add     t4, t4, t1
    vle16.v v2, (t3)
    vle16.v v3, (t4)
    sf.vc.iv    0x0, 0, v1, 0
    sf.vc.iv    0x1, 0, v2, 1
    sf.vc.iv    0x2, 0, v3, 1
    j       1b
2:  sf.vc.i     0x0, 0, 0, 0
    sf.vc.i     0x1, 0, 0, 0
    vsetvli t0, a2, e32, m1, ta, ma
    sf.vc.v.i   0x0, 0, v8, 0
    vse32.v v8, (a1)
    mv      a0, t0
    ret

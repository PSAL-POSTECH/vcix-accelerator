# Bare metal, in M mode: ends with m5_exit, or with m5_fail and a bit per failed check: 1 VS not dirty after vd is written, 2 f[rs1] read with FS off, 4 any other trap.
    .text
    .globl _start
_start:
    la      t0, trap
    csrw    mtvec, t0
    li      s1, 0
    li      t0, 0x6600
    csrs    mstatus, t0
    li      t0, 8
    vsetvli t0, t0, e32, m1, ta, ma

    li      t0, 0x200
    csrc    mstatus, t0
    li      s0, 0
    sf.vc.v.vv  0x3, v3, v2, v1
    csrr    t1, mstatus
    li      t0, 0x600
    and     t1, t1, t0
    beq     t1, t0, 1f
    ori     s1, s1, 1
1:
    li      t0, 0x6000
    csrc    mstatus, t0
    la      s0, 2f
    sf.vc.v.fv  0x1, v5, v2, fa0
    ori     s1, s1, 2
2:
    li      s0, 0
    sf.vc.v.xv  0x2, v3, v2, a1

done:
    li      a0, 0
    mv      a1, s1
    bnez    s1, 3f
    .long   0x4200007b
3:  .long   0x4400007b

    .p2align 2
trap:
    csrr    t0, mcause
    li      t1, 2
    bne     t0, t1, 4f
    beqz    s0, 4f
    jr      s0
4:  ori     s1, s1, 4
    j       done

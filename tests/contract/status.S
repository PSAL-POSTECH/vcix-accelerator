# Bare metal, in M mode: what mstatus does to a custom-2 instruction and what the
# instruction does to mstatus. Ends with m5_exit, or with m5_fail and one bit per
# failed check: 1 VS not dirty after vd is written, 2 f[rs1] read with FS off, 4 any other trap.
    .text
    .globl _start
_start:
    la      t0, trap
    csrw    mtvec, t0
    li      s1, 0                       # the failed checks
    li      t0, 0x6600                  # FS and VS on
    csrs    mstatus, t0
    li      t0, 8
    vsetvli t0, t0, e32, m1, ta, ma

    li      t0, 0x200                   # VS: dirty -> clean
    csrc    mstatus, t0
    li      s0, 0                       # no trap expected
    sf.vc.v.vv  0x3, v3, v2, v1         # writes vd
    csrr    t1, mstatus
    li      t0, 0x600
    and     t1, t1, t0
    beq     t1, t0, 1f
    ori     s1, s1, 1
1:
    li      t0, 0x6000                  # FS off
    csrc    mstatus, t0
    la      s0, 2f                      # an illegal instruction is expected; go on at 2
    sf.vc.v.fv  0x1, v5, v2, fa0        # reads f[rs1]
    ori     s1, s1, 2
2:
    li      s0, 0
    sf.vc.v.xv  0x2, v3, v2, a1         # reads no f register: legal with FS off

done:
    li      a0, 0
    mv      a1, s1
    bnez    s1, 3f
    .long   0x4200007b                  # m5_exit(delay = a0)
3:  .long   0x4400007b                  # m5_fail(delay = a0, code = a1)

    .p2align 2
trap:
    csrr    t0, mcause
    li      t1, 2                       # illegal instruction
    bne     t0, t1, 4f
    beqz    s0, 4f
    jr      s0
4:  ori     s1, s1, 4
    j       done

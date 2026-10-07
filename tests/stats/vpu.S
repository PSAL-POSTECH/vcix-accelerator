# Statistics windows cut by m5ops, on the tpu machine's vector unit: eight vector adds 40 dependent scalar adds apart,
# each alone in the unit; 40 scalar adds; the eight adds back to back. No branch, and 40 scalar adds after each reset:
# the CPU issues what follows a branch or an m5op before it resolves, and a vector add issued so occupies the unit too.
    .option norvc
    .text
    .globl main
main:
    li      t0, 8
    vsetvli t0, t0, e32, m1, ta, ma
    .insn r 0x7b, 0, 0x40, x0, x0, x0
    .rept 8
    vadd.vv v1, v2, v3
    .rept 40
    addi    t1, t1, 1
    .endr
    .endr
    .insn r 0x7b, 0, 0x41, x0, x0, x0
    .insn r 0x7b, 0, 0x40, x0, x0, x0
    .rept 40
    addi    t1, t1, 1
    .endr
    .insn r 0x7b, 0, 0x41, x0, x0, x0
    .insn r 0x7b, 0, 0x40, x0, x0, x0
    .rept 40
    addi    t1, t1, 1
    .endr
    vadd.vv v1, v9, v10
    vadd.vv v2, v9, v10
    vadd.vv v3, v9, v10
    vadd.vv v4, v9, v10
    vadd.vv v5, v9, v10
    vadd.vv v6, v9, v10
    vadd.vv v7, v9, v10
    vadd.vv v8, v9, v10
    .rept 40
    addi    t1, t1, 1
    .endr
    .insn r 0x7b, 0, 0x41, x0, x0, x0
    li      a0, 0
    ret

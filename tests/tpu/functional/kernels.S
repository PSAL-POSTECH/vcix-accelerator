# The vector code of the programs in this directory, linked at KERNEL_BASE of run.sh. Spike runs a vector
# instruction in every lane only inside --kernel-addr; there a load or store at a scratchpad address is each
# lane's own copy of it. No instruction here has 2 in its rd field, and the first is not an addi: Spike
# reads both as the kernel moving its stack.
#
# Every function: a0 = source in lane 0's scratchpad, a1 = destination there, a2 = elements asked.
# Returns the elements done, at most one register group.
    .option norvc
    .section .kernel, "ax"

# vd = f(vs2): sf.vc.v.iv \p, vd, vs2, \simm on a group of eight registers.
.macro special name, sew, p, simm
    .globl \name
\name:
    vsetvli t0, a2, e\sew, m8, ta, ma
    vle\sew\().v v8, (a0)
    vle\sew\().v v16, (a1)
    sf.vc.v.iv \p, v16, v8, \simm
    vse\sew\().v v16, (a1)
    mv      a0, t0
    ret
.endm
.irp sew, 16, 32, 64
    special k_verf_\sew, \sew, 0x0, 0
    special k_vtanh_\sew, \sew, 0x1, 0
    special k_vsin_\sew, \sew, 0x2, 0
    special k_vcos_\sew, \sew, 0x2, 1
    special k_vlog_\sew, \sew, 0x2, 2
    special k_vatan_\sew, \sew, 0x2, 3
    special k_vexp_\sew, \sew, 0x3, 0
.endr

# The systolic array: a push of the source, and a pop into the destination, which keeps what the pop leaves.
.macro systolic_push name, sew, p
    .globl \name
\name:
    vsetvli t0, a2, e\sew, m2, ta, ma
    vle\sew\().v v8, (a0)
    sf.vc.iv \p, 0x0, v8, 0
    mv      a0, t0
    ret
.endm
.macro systolic_pop name, sew
    .globl \name
\name:
    vsetvli t0, a2, e\sew, m2, ta, ma
    vle\sew\().v v16, (a1)
    sf.vc.v.i 0x2, 0x0, v16, 0
    vse\sew\().v v16, (a1)
    mv      a0, t0
    ret
.endm
.irp sew, 8, 16, 32, 64
    systolic_push k_input_\sew, \sew, 0x0
    systolic_push k_weight_\sew, \sew, 0x1
    systolic_pop k_pop_\sew, \sew
.endr
    .globl k_compute
k_compute:
    sf.vc.i 0x1, 0, 0, 4
    ret

# The cross-lane unit. A group holds 64 elements at every width, so a pattern of 64 words fits its own group.
# \simm is the operation as the five signed bits of the field.
.macro cross_push name, sew, lmul, simm
    .globl \name
\name:
    vsetvli t0, a2, e\sew, \lmul, ta, ma
    vle\sew\().v v8, (a0)
    sf.vc.iv 0x3, 0, v8, \simm
    mv      a0, t0
    ret
.endm
# a3 = the pattern, a word per element.
.macro cross_push_pattern name, sew, lmul, simm
    .globl \name
\name:
    vsetvli t0, a2, e32, m8, ta, ma
    vle32.v v24, (a3)
    vsetvli t0, a2, e\sew, \lmul, ta, ma
    vle\sew\().v v8, (a0)
    sf.vc.ivv 0x3, v24, v8, \simm
    mv      a0, t0
    ret
.endm
.macro cross_pop name, sew, lmul
    .globl \name
\name:
    vsetvli t0, a2, e\sew, \lmul, ta, ma
    vle\sew\().v v16, (a1)
    sf.vc.v.i 0x1, 0, v16, 4
    vse\sew\().v v16, (a1)
    mv      a0, t0
    ret
.endm
    cross_pop k_cross_pop_8, 8, m2
    cross_pop k_cross_pop_16, 16, m4
    cross_pop k_cross_pop_32, 32, m8
# Not an operation: the unit is not entered without crossing or shuffling.
    cross_push k_cross_push_0_32, 32, m8, 0
.irp operation, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14
    cross_push k_cross_push_\operation\()_8, 8, m2, \operation
    cross_push k_cross_push_\operation\()_16, 16, m4, \operation
    cross_push k_cross_push_\operation\()_32, 32, m8, \operation
    cross_push_pattern k_cross_pattern_\operation\()_8, 8, m2, \operation
    cross_push_pattern k_cross_pattern_\operation\()_16, 16, m4, \operation
    cross_push_pattern k_cross_pattern_\operation\()_32, 32, m8, \operation
.endr
.irp operation, 16, 17, 18, 20, 21, 22
    cross_push k_cross_push_\operation\()_8, 8, m2, \operation-32
    cross_push k_cross_push_\operation\()_16, 16, m4, \operation-32
    cross_push k_cross_push_\operation\()_32, 32, m8, \operation-32
    cross_push_pattern k_cross_pattern_\operation\()_8, 8, m2, \operation-32
    cross_push_pattern k_cross_pattern_\operation\()_16, 16, m4, \operation-32
    cross_push_pattern k_cross_pattern_\operation\()_32, 32, m8, \operation-32
.endr

# vlane_idx at an element width: a1 receives a3 doublewords of the group it wrote into, which was 0.
.macro lane_index name, sew
    .globl \name
\name:
    vsetvli t0, a3, e64, m8, ta, ma
    vmv.v.i v16, 0
    vsetvli t1, a2, e\sew, m1, ta, ma
    sf.vc.v.i 0x0, 0, v16, 0
    vsetvli t0, a3, e64, m8, ta, ma
    vse64.v v16, (a1)
    mv      a0, t1
    ret
.endm
.irp sew, 8, 16, 32, 64
    lane_index k_lane_index_\sew, \sew
.endr

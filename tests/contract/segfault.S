# Dies, and not as an illegal instruction: a load from address 0.
    .text
    .globl main
main:
    lw      t0, 0(zero)
    li      a0, 0
    ret

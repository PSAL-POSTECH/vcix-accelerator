# An owned instruction after frm is set to 2: the model reads it through the host.
    .text
    .globl main
main:
    li      t0, 8
    vsetvli t0, t0, e32, m1, ta, ma
    csrwi   frm, 2
    sf.vc.x 0x1, 0x2, 0x3, a0
    li      a0, 0
    ret

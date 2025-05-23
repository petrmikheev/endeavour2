.section .text.startup
.global _start
_start:
    addi sp, sp, -8
    sw gp, 0(sp)
    sw ra, 4(sp)
.option push
.option norelax
    la gp, __global_pointer$
.option pop
    jal dhrystone
    lw ra, 4(sp)
    lw gp, 0(sp)
    addi sp, sp, 8
    ret

.globl strcpy
strcpy:
    lb   t0, 0(a1)
    lb   t1, 1(a1)
    lb   t2, 2(a1)
    lb   t3, 3(a1)
    sb   t0, 0(a0)
    beq  t0, zero, strcpy_ret
    sb   t1, 1(a0)
    beq  t1, zero, strcpy_ret
    sb   t2, 2(a0)
    beq  t2, zero, strcpy_ret
    sb   t3, 3(a0)
    beq  t3, zero, strcpy_ret
    addi a1, a1, 4
    addi a0, a0, 4
    j strcpy
strcpy_ret:
    ret

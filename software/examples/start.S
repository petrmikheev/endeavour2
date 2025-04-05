.section .text.startup
.global _start
_start:
.option push
.option norelax
    la gp, __global_pointer$
.option pop
    la sp, __stack_pointer$
    j main

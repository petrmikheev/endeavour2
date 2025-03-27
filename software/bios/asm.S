#include <endeavour2/defs.h>

#define STACK_ADDR (RAM_BASE + BIOS_SIZE)

.section .data.biosmagic
    .word BIOS_MAGIC

.section .text.startup
.global _start
_start:
    j start
.align 2
    j putchar_impl
.align 2
    j printf_impl
.align 2
    j sscanf_impl
.align 2
    j crc32_impl
.align 2
    j beep_impl
.align 2
    j readline_impl
.align 2
    j read_uart_impl

start:
.option push
.option norelax
    la gp, __global_pointer$
.option pop
    li sp, STACK_ADDR
    la t0, trap
    csrw mtvec, t0
    jal main
    j infloop

trap:
    # save sp, t0, t1
    csrw mscratch, sp
    li sp, STACK_ADDR - 56
    sw t0, 0(sp)
    sw t1, 4(sp)

    # check timer interrupt
    li t0, (1<<31) | 7
    csrr t1, mcause
    beq t0, t1, timer_interrupt

    li t0, 9
    bne t0, t1, fatal_trap

    # ecall
    csrr t0, mepc
    addi t0, t0, 4
    csrw mepc, t0  # set mepc to next command to prevent loop

    li t0, 0x54494D45 # timer extension
    beq t0, a7, set_timer

    # save the rest of the context before calling C code
    sw t2, 8(sp)
    sw t3, 12(sp)
    sw t4, 16(sp)
    sw t5, 20(sp)
    sw t6, 24(sp)
    sw a2, 28(sp)
    sw a3, 32(sp)
    sw a4, 36(sp)
    sw a5, 40(sp)
    sw a6, 44(sp)
    sw a7, 48(sp)
    sw ra, 52(sp)

    mv a2, a6
    mv a3, a7
    jal sbi_handler

    # restore context
    lw t2, 8(sp)
    lw t3, 12(sp)
    lw t4, 16(sp)
    lw t5, 20(sp)
    lw t6, 24(sp)
    lw a2, 28(sp)
    lw a3, 32(sp)
    lw a4, 36(sp)
    lw a5, 40(sp)
    lw a6, 44(sp)
    lw a7, 48(sp)
    lw ra, 52(sp)
    j trap_ret

set_timer:
    la t0, CLINT_TIMECMP(0)
    li t1, -1
    sw t1, 4(t0)
    sw a0, 0(t0)
    sw a1, 4(t0)
    li t0, 1<<5
    csrc mip, t0  # clear S timer interrupt
    li a0, 0
    li a1, 0
    j trap_ret

timer_interrupt:
    la t0, CLINT_TIMECMPH(0)
    li t1, -1
    sw t1, 0(t0)  # disable timer to prevent loop
    li t0, 1<<5
    csrs mip, t0  # trigger S timer interrupt
    # continue to trap_ret

trap_ret:
    lw t0, 0(sp)
    lw t1, 4(sp)
    csrr sp, mscratch
    mret

fatal_trap:
    mv   a0, t1       # mcause
    csrr a1, mtval    # mtval
    csrr a2, mepc     # mepc
    csrr a3, mscratch # sp
    mv   a4, ra       # ra
    jal fatal_trap_handler
infloop:
    j infloop

.section .text.dhrystone_bin
.global dhrystone
dhrystone:
    .incbin "dhrystone/dhrystone.bin"

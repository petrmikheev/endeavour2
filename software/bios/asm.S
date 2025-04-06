#include <endeavour2/defs.h>

#define STACK_ADDR (RAM_BASE + BIOS_SIZE)

.section .data.data_start
.align 2
tmp_gp:
    .word 0
tmp_ra:
    .word 0
tmp_sp:
    .word 0

.global hart_cfg
hart_cfg:
    .zero 32*3

.section .text.startup
.global _start
_start:
    j start

. = _start + 4

.global sdcard_sector_count
sdcard_sector_count: .word 0
text_style:          .word 0
cursor_ptr:          .word 0

.macro API_FN id fn
  . = _start + 64 + 36 * \id
  # store gp and ra to global variables rather than to stack
  # because stack may contain variadic arguments for printf/sscanf
    la t1, __global_pointer$
    sw gp, -0x800(t1)  # tmp_gp
    sw ra, -0x7fc(t1)  # tmp_ra
    mv gp, t1
    call \fn
    lw ra, -0x7fc(gp)
    lw gp, -0x800(gp)
    ret
.endm

.option push
.option norelax
    API_FN  0, putchar
    API_FN  1, printf
    API_FN  2, sscanf
    API_FN  3, crc32
    API_FN  4, beep
    API_FN  5, readline
    API_FN  6, read_uart
    API_FN  7, sdread
    API_FN  8, sdwrite
    API_FN  9, get_keyboard_report
    API_FN 10, hart_run
.option pop

.global printf
printf:
    addi sp, sp, -32
    sw ra, 0(sp)
    sw a1, 4(sp)
    sw a2, 8(sp)
    sw a3, 12(sp)
    sw a4, 16(sp)
    sw a5, 20(sp)
    sw a6, 24(sp)
    sw a7, 28(sp)
    addi a1, sp, 4
    call printf_impl
    lw ra, 0(sp)
    addi sp, sp, 32
    ret

.global sscanf
sscanf:
    addi sp, sp, -32
    sw ra, 0(sp)
    sw a2, 8(sp)
    sw a3, 12(sp)
    sw a4, 16(sp)
    sw a5, 20(sp)
    sw a6, 24(sp)
    sw a7, 28(sp)
    addi a2, sp, 8
    call sscanf_impl
    lw ra, 0(sp)
    addi sp, sp, 32
    ret

start:
.option push
.option norelax
    la gp, __global_pointer$
.option pop
    csrr tp, mhartid
    bne tp, zero, not_main_thread
    la t0, trap
    csrw mtvec, t0
    li sp, STACK_ADDR
    jal main
    j infloop

not_main_thread:
    slli t0, tp, 2
    li t1, CLINT_BASE
    add t0, t0, t1
    sw zero, 0(t0)   # clear pending IPI
    addi t0, tp, -1
    slli t0, t0, 5
    la t1, hart_cfg
    add s0, t0, t1   # struct HartCfg of this hart
    csrr t0, misa
    sw t0,  0x8(s0)

    # store debug info
    csrr t1, mcause
    csrr t2, mtval
    csrr t3, mepc
    sw t1,  0xC(s0)
    sw t2, 0x10(s0)
    sw t3, 0x14(s0)
    sw sp, 0x18(s0)
    sw ra, 0x1C(s0)

    lw t0, 4(s0) # jump_to
    seqz t1, t0
    sw t1, 0(s0) # set 'ready' if jump_to == 0
    csrsi mstatus, 1<<3  # allow interrupts in M mode
    beq t0, zero, not_main_thread_wait
    sw zero, 4(s0)
    la sp, worker_stack
    la ra, start
    fence.i
    jr t0

not_main_thread_wait:
    wfi
    j not_main_thread_wait

.global run_binary
run_binary:
    add sp, sp, -16
    sw ra, 12(sp)
    sw sp, -0x7f8(gp)
    mv t0, a0
    mv a0, a1
    mv a1, a2
    fence.i
    jalr ra, 0(t0)
.option push
.option norelax
    la gp, __global_pointer$
.option pop
    lw sp, -0x7f8(gp)
    lw ra, 12(sp)
    add sp, sp, -16
    ret

.align 2
trap:
    # save sp, t0, t1
    csrw mscratch, sp
    li sp, STACK_ADDR - 64
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
    sw gp, 56(sp)

.option push
.option norelax
    la gp, __global_pointer$
.option pop

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
    lw gp, 56(sp)
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
.option push
.option norelax
    la gp, __global_pointer$
.option pop
    jal fatal_trap_handler
infloop:
    j infloop

.section .bss.worker_stack
    .zero 0x300
worker_stack:

.section .bss.ohci_hcca
.global ohci_hcca
.align 8
ohci_hcca:
    .zero 256

.section .text.dhrystone_bin
.global dhrystone
dhrystone:
    .incbin "dhrystone/dhrystone.bin"

.section .data.biosmagic
    .word BIOS_MAGIC

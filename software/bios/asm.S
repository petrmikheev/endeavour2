#include <endeavour2/raw/defs.h>

#define CORE0_STACK_ADDR (RAM_BASE + BIOS_SIZE)
#define CORE1_STACK_ADDR (RAM_BASE + BIOS_SIZE - 0xD00)

.section .data.data_start
.align 2
tmp_gp:              .word 0
tmp_ra:              .word 0
tmp_sp:              .word 0

.align 6
.global hart_cfg
hart_cfg:
    .zero 32*2

.section .text.start
.global _start
_start:
    j start

.align 2
# used in bios.h by absolute address
.global sdcard_sector_count, text_style, cursor_ptr
sdcard_sector_count: .word 0
text_style:          .word 0
cursor_ptr:          .word 0

start:
    .option push
.option norelax
    la gp, __global_pointer$
.option pop
    li t0, 1<<13
    csrs mstatus, t0  # enable FPU
    la t0, hart_cfg
    csrr t1, misa
    csrr tp, mhartid
    beq tp, zero, start_core0
    j start_core1

.section .text.api
_api_start:

.macro API_FN id fn
  . = _api_start + 36 * \id
  # store gp and ra to global variables rather than on stack
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
    API_FN 10, get_hart_cfg
    API_FN 11, set_video_mode
.option pop

.section .text

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

start_core0:
    li sp, CORE0_STACK_ADDR
    sw sp, 4(t0)
    sw t1, 8(t0)
    la t0, trap_core0
    csrw mtvec, t0
    jal main
    j stop_core

start_core1:
    sw t1, 40(t0)
    la t0, trap_core1
    csrw mtvec, t0
    # j stop_core   continue to stop_core

.global stop_core
stop_core:
.option push
.option norelax
    la t0, hart_cfg
.option pop
    csrr t1, mhartid
    slli t1, t1, 5
    add t0, t0, t1
    sw zero, 0(t0)
    sw zero, 4(t0)
    csrsi mstatus, 1<<3  # allow interrupts in M mode
    j infloop

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
trap_core1:
    csrw mscratch, sp
    li sp, CORE1_STACK_ADDR - 64
    j trap

.align 2
trap_core0:
    csrw mscratch, sp
    li sp, CORE0_STACK_ADDR - 64

trap:
    sw t0, 0(sp)
    sw t1, 4(sp)

    csrr t1, mcause

    li t0, (1<<31) | 3
    beq t0, t1, software_interrupt
    addi t0, t0, (7 - 3)
    beq t0, t1, timer_interrupt
    li t0, 9
    bltu t1, t0, trap_delegate_to_supervisor
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

    mv a3, a6
    mv a4, a7
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

trap_delegate_to_supervisor:
    csrr t0, mstatus
    li t1, 1<<12
    and t1, t1, t0
    bne t1, zero, fatal_trap  # can't delegate if was already in M mode

    andi t1, t0, 2
    ori t1, t1, 1<<(11-4) # mpp=01
    slli t1, t1, 4     # new spie
    srli t0, t0, 3
    andi t0, t0, 1<<8  # new spp
    or t0, t0, t1
    li t1, (1<<8) | (1<<5) | (1<<1)
    csrc mstatus, t1   # clear old spp, old spie, sie
    csrs mstatus, t0   # set mpp, new spp, new spie

    csrr t0, mcause
    csrw scause, t0
    csrr t0, mepc
    csrw sepc, t0
    csrr t0, mtval
    csrw stval, t0
    csrr t0, stvec
    andi t0, t0, ~3
    csrw mepc, t0
    j trap_ret

#define HART_ACTION_IPI    1
#define HART_ACTION_FENCEI 2
#define HART_ACTION_JUMP   4

software_interrupt:
    sw t2, 8(sp)
    csrr t1, mhartid
    slli t1, t1, 2
    li t0, CLINT_BASE
    add t0, t0, t1
    sw zero, 0(t0)   # clear pending IPI
    slli t1, t1, 3
.option push
.option norelax
    la t0, hart_cfg
.option pop
    add t0, t0, t1
    lw t1, 0(t0)  # action
    sw zero, 0(t0)
    andi t2, t1, HART_ACTION_FENCEI
    beq t2, zero, skip_fencei
    fence.i
skip_fencei:
    andi t2, t1, HART_ACTION_IPI
    beq t2, zero, skip_ipi
    li t2, 1<<1
    csrs mip, t2  # trigger S software interrupt
skip_ipi:
    andi t2, t1, HART_ACTION_JUMP
    bne t2, zero, hart_jump

    # store debug info
    csrr t1, mepc
    csrr t2, mscratch
    sw t1, 0x14(t0)   # epc
    sw t2, 0x18(t0)   # sp
    sw ra, 0x1C(t0)   # ra

    lw t2, 8(sp)
    j trap_ret

hart_jump:
    lw t1, 4(t0)  # jump_to
    beq t1, zero, stop_core
    # args for run_in_supervisor_mode
    lw a0, 0x14(t0)
    lw a1, 0x10(t0)
    jalr t1
    j stop_core

set_timer:
    csrr t1, mhartid
    slli t1, t1, 3
    li t0, CLINT_TIMECMP(0)
    add t0, t0, t1
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
    csrr t1, mhartid
    slli t1, t1, 3
    li t0, CLINT_TIMECMPH(0)
    add t0, t0, t1
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
    csrr a0, mcause   # mcause
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
    fence iorw, iorw
    wfi
    j infloop

.global run_in_supervisor_mode  # void run_in_supervisor_mode(void* addr, unsigned a1)
run_in_supervisor_mode:
    csrw mepc, a0
    csrr a0, mhartid

    # mstatus.mpp = 01 (S mode), unset mie/mpie
    li t0, 0x1000 | (1<<3) | (1<<7)
    li t1, 0x0800
    csrc mstatus, t0
    csrs mstatus, t1

    csrci sstatus, (1<<1) # clear sie, so interrupt will not happen right after switching to S mode

    # delegate interrupts to S mode
    li t0, ~(1<<11) # ~((1<<3) | (1<<7) | (1<<11))
    csrw mie, t0
    li t0, -1
    csrw mideleg, t0

    # delegate exceptions to S mode
    li t0, 0xb1ff
    csrw medeleg, t0

    csrwi satp, 0  # turn off virtual memory in S mode
    fence.i
    mret           # switch to S mode and jump to addr

.section .bss.core1_stack
    .zero 0x300

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

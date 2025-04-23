#include <endeavour2/defs.h>

/* Microloader is placed in ROM.
- Loads 32 KB to RAM_BASE from either UART (if boot_en=0) or SPI flash (boot_en=1 or key0=1).
- Simple memory check: reads these 32KB again and checks that XOR is the same.
- Checks that there is magic number BIOS_MAGIC at the end.
- Jumps to RAM_BASE.

LEDS (b - slow blinking, B - fast blinking)
0001 start
00B1 calibration error
0011 calibration ok
0b11 listen uart
0B11 uart error
01B1 memory error
B111 no BIOS_MAGIC
1111 jump to RAM_BASE
*/

#define BBOARD s0
#define BUART s1
#define PTR s2
#define PTR_END s3
#define HASH s4

# Fake data for memory simulation
# #define FAKE_UART
# #define BIOS_SIZE 10240

.section .text.startup
.global _start
_start:
    csrr t0, mhartid
    bne t0, zero, not_main_thread
    li BBOARD, BOARD_BASE
    li BUART, UART_BASE
    li t0, 1
    sw t0, REG_BOARD_LEDS(BBOARD)  # leds 0001 -> cpu started
    li t0, UART_BAUD_RATE(115200) | UART_PARITY_EVEN | UART_CSTOPB
    sw t0, REG_UART_CFG(BUART)
    sw zero, REG_UART_RX(BUART)

wait_ddr_calibration:
    lw t0, REG_BOARD_RAM_STAT(BBOARD)
    andi t0, t0, 3
    beq t0, zero, wait_ddr_calibration

    li a0, 2
    blt t0, a0, error  # leds 00B1 -> ddr calibration error
    li t0, 3
    sw t0, REG_BOARD_LEDS(BBOARD)  # leds 0011 -> ddr calibration OK

    li PTR, RAM_BASE
    li t0, BIOS_SIZE
    add PTR_END, PTR, t0
    li HASH, 0
    li a0, 4
uart_loop:
    lw t0, REG_BOARD_KEYS(BBOARD)
    li t1, (BOARD_KEY_BOOT_EN | 1)
    and t0, t0, t1
    bne t0, zero, spi_boot
    csrr t0, time
    srli t0, t0, 21
    andi t0, t0, 4
    ori t0, t0, 3
    sw t0, REG_BOARD_LEDS(BBOARD)  # leds 0b11 -> listen UART

#ifndef FAKE_UART
    lw t0, REG_UART_RX(BUART)
    blt t0, zero, uart_loop
    li t1, 255
    bgt t0, t1, error  # leds 0B11 -> UART error
#else
    andi t0, PTR, 255
#endif

    andi t1, PTR, 3
    slli t1, t1, 3
    sll t1, t0, t1
    xor HASH, HASH, t1

    sb t0, 0(PTR)
    addi PTR, PTR, 1
    bne PTR, PTR_END, uart_loop
    j check

spi_boot:
    li HASH, 0
    li PTR, RAM_BASE
    li t0, SPI_FLASH_BASE
    li t1, 0xAB000000  # power up
    sw t1, REG_SPI_FLASH_DATA(t0)
    li t1, (1<<16)
    sw t1, REG_SPI_FLASH_CNT(t0)
    li t1, 2048
spi_pa_wait:
    addi t1, t1, -1
    bne t1, zero, spi_pa_wait
    li t1, 0x03000000 | ((16<<20) - BIOS_SIZE)
    sw t1, REG_SPI_FLASH_DATA(t0)
    li t1, (4<<16) | BIOS_SIZE
    sw t1, REG_SPI_FLASH_CNT(t0)
spi_loop:
    lw t1, REG_SPI_FLASH_RHAS(t0)
    beq t1, zero, spi_loop
    lw t1, REG_SPI_FLASH_DATA(t0)
    sw t1, 0(PTR)
    xor HASH, HASH, t1
    addi PTR, PTR, 4
    bne PTR, PTR_END, spi_loop
    li t1, 0xB9000000  # power down
    sw t1, REG_SPI_FLASH_DATA(t0)
    li t1, (1<<16)
    sw t1, REG_SPI_FLASH_CNT(t0)

check:
    li t0, 7
    sw t0, REG_BOARD_LEDS(BBOARD)  # leds 0111 -> memcheck started
    li PTR, RAM_BASE
checkloop:
    lw t0, 0(PTR)
    xor HASH, HASH, t0
    addi PTR, PTR, 4
    bne PTR, PTR_END, checkloop
    li a0, 2
    bne HASH, zero, error  # leds 01B1 -> memcheck failed
    li t1, BIOS_MAGIC
    li a0, 8
    bne t0, t1, error # leds B111 -> no BIOS_MAGIC
    fence.i
    li t0, 15
    sw t0, REG_BOARD_LEDS(BBOARD)  # leds 1111 -> jump to RAM_BASE
    li t0, RAM_BASE
    jr t0

error:  # a0 - mask
    lw t0, REG_BOARD_LEDS(BBOARD)
    xori t1, a0, -1
    and t0, t0, t1
    csrr t1, time
    srli t1, t1, 21
    andi t1, t1, 1
    mul t1, t1, a0
    or t0, t0, t1
    sw t0, REG_BOARD_LEDS(BBOARD)
    lw t0, REG_BOARD_KEYS(BBOARD)
    andi t0, t0, 2
    beq t0, zero, error

    # dump memory
    li PTR, RAM_BASE
    li t0, BIOS_SIZE
    add PTR_END, PTR, t0
dump_loop:
    lw t0, REG_UART_TX(BUART)
    blt t0, zero, dump_loop
    lb t0, 0(PTR)
    sw t0, REG_UART_TX(BUART)
    addi PTR, PTR, 1
    bne PTR, PTR_END, dump_loop
    j error

not_main_thread:
    la t0, not_main_thread_go
    csrw mtvec, t0
    csrsi mie, 1<<3      # allow machine software interrupt
    csrsi mstatus, 1<<3  # allow interrupts in M mode
    wfi
    j not_main_thread

.align 2
not_main_thread_go:
    fence.i
    li t0, RAM_BASE
    jr t0

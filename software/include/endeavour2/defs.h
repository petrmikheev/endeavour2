#ifndef ENDEAVOUR2_DEFS_H
#define ENDEAVOUR2_DEFS_H

// *** Address map

#define UART_BASE      0x100
#define     REG_UART_RX      0  // negative value - buffer empty; write to RX clears framing error flag
#define     REG_UART_TX      4  // negative value - buffer full
#define     REG_UART_CFG     8

#define AUDIO_BASE     0x200
#define     REG_AUDIO_CFG    0
#define     REG_AUDIO_STREAM 4  // write - add to stream, read - remaining buf size

#define I2C_BASE       0x300

#define BOARD_BASE    0x1000
#define     REG_BOARD_RESET      0x0  // write triggers soft reset
#define     REG_BOARD_HART_COUNT 0x4
#define     REG_BOARD_CPU_FREQ   0x8  // CPU freq in kHz
#define     REG_BOARD_DVI_FREQ   0xC  // DVI pixel freq in kHz
#define     REG_BOARD_LEDS      0x10
#define     REG_BOARD_KEYS      0x14
#define     REG_BOARD_RAM_STAT  0x18

#define VIDEO_BASE    0x2000
#define SDCARD_BASE   0x3000
#define USB_OHCI_BASE 0x4000

#define CLINT_BASE   0x10000
#define PLIC_BASE  0x4000000

#define ROM_BASE  0x40000000  // reset addr
#define RAM_BASE  0x80000000
#define     RAM_SIZE  0x40000000  // 1 GB

// ***

// UART_CFG flags
#define UART_BAUD_RATE(X) (60000000 / (X) - 1)
#define UART_PARITY_NONE  0
#define UART_PARITY_EVEN  (1<<16)
#define UART_PARITY_ODD   (3<<16)
#define UART_CSTOPB       (4<<16)

// UART_RX flags
#define UART_PARITY_ERROR  0x100
#define UART_FRAMING_ERROR 0x200

// AUDIO_CFG flags
#define AUDIO_SAMPLE_RATE(X) (48000000 / (X) - 1)
#define AUDIO_VOLUME(X) (((unsigned)(X)&15) << 16)
#define AUDIO_MAX_VOLUME 15
#define AUDIO_NO_SLEEP 0x00100000
#define AUDIO_EMPTY    0x80000000

#endif  // ENDEAVOUR2_DEFS_H

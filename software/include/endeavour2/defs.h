#ifndef ENDEAVOUR2_DEFS_H
#define ENDEAVOUR2_DEFS_H

// *** Address map

#define UART_BASE       0x100
#define AUDIO_BASE      0x200
#define I2C_BASE        0x300
#define ESP32_UART_BASE 0x400
#define BOARD_BASE     0x1000
#define VIDEO_BASE     0x2000
#define SDCARD_BASE    0x3000
#define USB_OHCI_BASE  0x4000

#define CLINT_BASE   0x10000
#define CLINT_IPI(hartid)      (CLINT_BASE + ((hartid)<<2))
#define CLINT_TIMECMP(hartid)  (CLINT_BASE + 0x4000 + ((hartid)<<3))
#define CLINT_TIMECMPH(hartid) (CLINT_BASE + 0x4004 + ((hartid)<<3))
#define CLINT_TIME             (CLINT_BASE + 0xBFF8)
#define CLINT_TIMEH            (CLINT_BASE + 0xBFFC)

#define PLIC_BASE  0x4000000

#define ROM_BASE  0x40000000  // reset addr
#define RAM_BASE  0x80000000

// *** UART

#ifdef __ASSEMBLER__
  #define REG_UART_RX      0  // negative value - buffer empty; write to RX clears framing error flag
  #define REG_UART_TX      4  // negative value - buffer full
  #define REG_UART_CFG     8
#else
struct EndeavourUart {
  int rx;
  int tx;
  unsigned cfg;
};
#define UART_REGS ((volatile struct EndeavourUart*)(UART_BASE))
#define ESP32_UART_REGS ((volatile struct EndeavourUart*)(ESP32_UART_BASE))
#endif

// UART_CFG flags
#define UART_BAUD_RATE(X) (60000000 / (X) - 1)
#define UART_PARITY_NONE  0
#define UART_PARITY_EVEN  (1<<16)
#define UART_PARITY_ODD   (3<<16)
#define UART_CSTOPB       (4<<16)

// UART_RX flags
#define UART_PARITY_ERROR  0x100
#define UART_FRAMING_ERROR 0x200

// *** Audio

#ifdef __ASSEMBLER__
  #define REG_AUDIO_CFG    0
  #define REG_AUDIO_STREAM 4  // write - add to stream, read - remaining buf size
#else
struct EndeavourAudio {
  unsigned cfg;
  unsigned stream;
};
#define AUDIO_REGS ((volatile struct EndeavourAudio*)(AUDIO_BASE))
#endif

// AUDIO_CFG flags
#define AUDIO_SAMPLE_RATE(X) (60000000 / (X) - 1)
#define AUDIO_VOLUME(X) (((unsigned)(X)&15) << 16)
#define AUDIO_MAX_VOLUME 15
#define AUDIO_NO_SLEEP 0x00100000
#define AUDIO_EMPTY    0x80000000

// *** I2C

#ifdef __ASSEMBLER__
  #define REG_I2C_CMD      0
  #define REG_I2C_COUNTER  4
  #define REG_I2C_DATA     8
#else
struct EndeavourI2C {
  int cmd;
  unsigned counter;
  unsigned data;
};
#define I2C_REGS ((volatile struct EndeavourI2C*)(I2C_BASE))
#endif

// I2C_CMD
#define I2C_ADDR_STUSB4500  0x28  // USB PD controller
#define I2C_ADDR_DDC        0x37  // Display data channel
#define I2C_ADDR_TFP410     0x38  // DVI transmitter
#define I2C_ADDR_PCF85063A  0x51  // Real-time clock
#define I2C_ADDR_SI5351A    0x60  // PLL
#define I2C_CMD_READ        0x80
#define I2C_CMD_WRITE       0x00
#define I2C_CMD_HIGH_SPEED 0x100
#define I2C_CMD_DATA_ERR   0x200
#define I2C_CMD_ADDR_ERR   0x400
#define I2C_CMD_BUSY  0x80000000

// *** Board

#ifdef __ASSEMBLER__
  #define REG_BOARD_RESET         0x0  // write triggers soft reset
  #define REG_BOARD_HART_COUNT    0x4
  #define REG_BOARD_CPU_FREQ      0x8  // CPU frequency
  #define REG_BOARD_DVI_FREQ      0xC  // DVI pixel frequency
  #define REG_BOARD_LEDS         0x10
  #define REG_BOARD_KEYS         0x14
  #define REG_BOARD_RAM_STAT     0x18
  #define REG_BOARD_CPU_FEATURES 0x1C
  #define REG_BOARD_RAM_SIZE     0x20
  #define REG_BOARD_ESP32_CFG    0x24
#else
struct EndeavourBoard {
  unsigned reset;
  unsigned hart_count;
  unsigned cpu_frequency;
  unsigned dvi_pixel_frequency;
  unsigned leds;
  unsigned keys;
  unsigned ram_stat;
  unsigned cpu_features;
  unsigned ram_size;
  unsigned esp32_cfg;
};
#define BOARD_REGS ((volatile struct EndeavourBoard*)(BOARD_BASE))
#endif

// BOARD_KEYS flags
#define BOARD_KEY_BOOT_EN 4

// BOARD_CPU_FEATURES flags
#define CPU_FEATURES_ZBA        1  // Address calculation extension.
#define CPU_FEATURES_ZBB        2  // Basic bit manipulation extension.
#define CPU_FEATURES_ZBC        4  // Carry-less multiplication extension.
#define CPU_FEATURES_ZBS        8  // Single-bit operation extension.
#define CPU_FEATURES_ZICBOP  0x10  // Cache-block prefetch extension.
#define CPU_FEATURES_ZICBOM  0x20  // Cache-block management extension.

// BOARD_ESP32_CFG flags
#define BOARD_ESP32_EN          1
#define BOARD_ESP32_SPI_BOOT    2

#ifndef __ASSEMBLER__
// *** SD card, see https://github.com/ZipCPU/sdspi
struct EndeavourSDCard {
  unsigned cmd;
  unsigned data;
  unsigned fifo0;
  unsigned fifo1;
  unsigned phy;
};
#define SDCARD_REGS ((volatile struct EndeavourSDCard*)(SDCARD_BASE))

// *** USB OHCI
struct OHCICtrl {
  unsigned HcRevision;         // 0x00
  unsigned HcControl;          // 0x04
  unsigned HcCommandStatus;    // 0x08
  unsigned HcInterruptStatus;  // 0x0c
  unsigned HcInterruptEnable;  // 0x10
  unsigned HcInterruptDisable; // 0x14
  unsigned HcHCCA;             // 0x18
  unsigned HcPeriodCurrentED;  // 0x1c
  unsigned HcControlHeadED;    // 0x20
  unsigned HcControlCurrentED; // 0x24
  unsigned HcBulkHeadED;       // 0x28
  unsigned HcBulkCurrentED;    // 0x2c
  unsigned HcDoneHead;         // 0x30
  unsigned HcFmInterval;       // 0x34
  unsigned HcFmRemaining;      // 0x38
  unsigned HcFmNumber;         // 0x3c
  unsigned HcPeriodicStart;    // 0x40
  unsigned HcLSThreshold;      // 0x44
  unsigned HcRhDescriptorA;    // 0x48
  unsigned HcRhDescriptorB;    // 0x4c
  unsigned HcRhStatus;         // 0x50
  unsigned HcRhPortStatus[2];  // 0x54, 0x58
};
#define USB_OHCI_REGS ((volatile struct OHCICtrl*)(USB_OHCI_BASE))
#endif

// *** misc utils

// Initial loader in ROM loads BIOS_SIZE bytes from SPI Flash or UART to RAM_BASE
// and jumps there if the last 4 bytes are BIOS_MAGIC.
#define BIOS_SIZE 32768
#define BIOS_MAGIC 0xa5ef1234

#ifndef __ASSEMBLER__
// A counter that increments every 100ns
static inline unsigned time_100nsec() {
  unsigned t;
  asm volatile("csrr %0, time" : "=r" (t));
  return t;
}
#endif

#endif  // ENDEAVOUR2_DEFS_H

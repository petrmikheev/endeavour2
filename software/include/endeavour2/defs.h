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
  #define REG_BOARD_CPU_FREQ      0x4  // CPU frequency
  #define REG_BOARD_DVI_FREQ      0x8  // DVI pixel frequency
  #define REG_BOARD_HART_COUNT    0xC
  #define REG_BOARD_CPU_FEATURES(hartid) (0x10 + 4 * (hartid))
  #define REG_BOARD_LEDS         0x20
  #define REG_BOARD_KEYS         0x24
  #define REG_BOARD_RAM_STAT     0x28
  #define REG_BOARD_RAM_SIZE     0x2C
  #define REG_BOARD_ESP32_CFG    0x30
#else
struct EndeavourBoard {
  unsigned reset;
  unsigned cpu_frequency;
  unsigned dvi_pixel_frequency;
  unsigned hart_count;
  unsigned cpu_features[4];
  unsigned leds;
  unsigned keys;
  unsigned ram_stat;
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
struct VideoMode {
// clock flags
#define HSYNC_INV (1<<31)
#define VSYNC_INV (1<<30)
  unsigned clock;        //  0
  unsigned hResolution;  //  4
  unsigned hSyncStart;   //  8
  unsigned hSyncEnd;     //  C
  unsigned hTotal;       // 10
  unsigned vResolution;  // 14
  unsigned vSyncStart;   // 18
  unsigned vSyncEnd;     // 1C
  unsigned vTotal;       // 20
};

struct EndeavourVideo {
  struct VideoMode mode;
// cfg flags
#define VIDEO_TEXT_ON     1
#define VIDEO_GRAPHIC_ON  2
#define VIDEO_RGB565      0
#define VIDEO_RGAB5515    4
#define VIDEO_FONT_HEIGHT(X) ((((X)-1)&15) << 4) // allowed range [6, 16]
  unsigned cfg;          // 24
  unsigned regIndex;     // 28
  unsigned regValue;     // 2C
  void*    textAddr;     // 30
  void*    graphicAddr;  // 34
  unsigned textOffset;   // 38
};
#define VIDEO_REGS ((volatile struct EndeavourVideo*)(VIDEO_BASE))

#define TEXT_BUFFER_SIZE 0x40000  // 256KB
#define TEXT_LINE_SIZE     1024   // 1KB

#define GRAPHIC_BUFFER_SIZE 0x800000 // 8MB
#define GRAPHIC_LINE_SIZE     4096   // 4KB

#define TEXT_BG(X) ((X)<<24)
#define TEXT_FG(X) ((X)<<16)

// VIDEO_REG_INDEX
#define VIDEO_COLORMAP(X) (X)  // RGBA (8, 8, 8, 7); bit 7 unused; X in range [0, 127]
#define VIDEO_CHARMAP(CHAR, WORD) ((CHAR) << 2 | (WORD))  // WORD range is [0, 3]; CHAR range is [0, 511], but [0, 31] intersects with colormap

// COLORMAP values
#define COLORMAP_TEXT_COLOR(R, G, B) ((R)<<24 | (G)<<16 | (B)<<8)
#define COLORMAP_TEXT_ALPHA(A) (A)     // 0 - 64

// VIDEO_TEXT_OFFSET
#define VIDEO_TEXT_OFFSET_X(X) (X)
#define VIDEO_TEXT_OFFSET_Y(Y) ((Y)<<8)

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

static inline unsigned get_hartid() {
  unsigned i;
  asm volatile("csrr %0, mhartid" : "=r" (i));
  return i;
}

static inline void software_interrupt(int hartid) {
  *(volatile unsigned*)(long)CLINT_IPI(hartid) = 1;
}
#endif

#endif  // ENDEAVOUR2_DEFS_H

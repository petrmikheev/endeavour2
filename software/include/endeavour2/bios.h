#ifndef ENDEAVOUR2_BIOS_H
#define ENDEAVOUR2_BIOS_H

#include <endeavour2/defs.h>

#define bios_putchar ((void (*)(char))                               (RAM_BASE + 0x4))
#define bios_printf  ((void (*)(const char* /*fmt*/, ...))           (RAM_BASE + 0x8))
#define bios_sscanf  ((int  (*)(const char* /*fmt*/, ...))           (RAM_BASE + 0xC))
#define bios_crc32   ((unsigned (*)(const void*, int))               (RAM_BASE + 0x10))
#define bios_beep    ((void (*)(int /*volume 0-256*/, int /*cnt*/))  (RAM_BASE + 0x14))

// bios_sdread(dst, sector, sector_count) -> sector_count
//#define bios_sdread  ((int  (*)(void*, unsigned, unsigned))          (BIOS_ROM_ADDR + 0x10))
// bios_sdwrite(src, sector, sector_count) -> sector_count
//#define bios_sdwrite ((int  (*)(const void*, unsigned, unsigned))    (BIOS_ROM_ADDR + 0x14))
// 0x18 used for BIOS_CHARMAP_ADDR
//#define bios_gets    ((int (*)(char*, int /*max_size*/)) /*-> size*/ (BIOS_ROM_ADDR + 0x1C))
// bios_read_uart(dst, size, uart_divisor_override /* -1 - don't override */) -> ok
//#define bios_read_uart    ((int (*)(char*, int, int))                (BIOS_ROM_ADDR + 0x20))
//#define bios_uart_console ((void (*)())                              (BIOS_ROM_ADDR + 0x28))

#define putchar bios_putchar
#define printf bios_printf
#define sscanf bios_sscanf

#endif  // ENDEAVOUR2_BIOS_H

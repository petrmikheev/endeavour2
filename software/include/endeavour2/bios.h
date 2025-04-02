#ifndef ENDEAVOUR2_BIOS_H
#define ENDEAVOUR2_BIOS_H

#include <endeavour2/defs.h>

#define bios_putchar    ((void (*)(char))                                           (RAM_BASE + 0x4))
#define bios_printf     ((void (*)(const char* /*fmt*/, ...))                       (RAM_BASE + 0x8))
#define bios_sscanf     ((int  (*)(const char* /*str*/, const char* /*fmt*/, ...))  (RAM_BASE + 0xC))
#define bios_crc32      ((unsigned (*)(const void*, int))                           (RAM_BASE + 0x10))
// bios_beep(duration_ms, frequency, volume) // volume range [-1:15]; -1 = silent
#define bios_beep       ((void (*)(unsigned, unsigned, int))                        (RAM_BASE + 0x14))
// bios_readline(prompt, buffer, max_size)
#define bios_readline   ((void (*)(const char*, char*, unsigned))                   (RAM_BASE + 0x18))
// bios_read_uart(dst, size, uart_divisor_override /* -1 - don't override */) -> err
#define bios_read_uart  ((int (*)(char*, int, int))                                 (RAM_BASE + 0x1C))
// bios_sdread(dst, sector, sector_count) -> sector_count
#define bios_sdread     ((unsigned  (*)(void*, unsigned, unsigned))                 (RAM_BASE + 0x20))
// bios_sdwrite(src, sector, sector_count) -> sector_count
#define bios_sdwrite    ((unsigned  (*)(const void*, unsigned, unsigned))           (RAM_BASE + 0x24))
#define bios_sdcard_sector_count ((unsigned (*)())                                  (RAM_BASE + 0x28))

#define putchar bios_putchar
#define printf bios_printf
#define sscanf bios_sscanf

#endif  // ENDEAVOUR2_BIOS_H

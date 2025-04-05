#ifndef ENDEAVOUR2_BIOS_H
#define ENDEAVOUR2_BIOS_H

#include <endeavour2/defs.h>

#define API_FN(ID, RET, ...) ((RET (*)(__VA_ARGS__))(RAM_BASE + 64 + ID * 36))

// ***  NAME                      RETURN    ARGS

#define bios_putchar    API_FN(0, void,     char)

#define bios_printf     API_FN(1, void,     const char* /*fmt*/, ...)

#define bios_sscanf     API_FN(2, int,      const char* /*str*/, const char* /*fmt*/, ...)

#define bios_crc32      API_FN(3, unsigned, const void* /*addr*/, int /*size*/)

// bios_beep(duration_ms, frequency, volume) // volume range [-1:15]; -1 = silent
#define bios_beep       API_FN(4, void,     unsigned, unsigned, int)

// bios_readline(prompt, buffer, max_size)
#define bios_readline   API_FN(5, void,     const char*, char*, unsigned)

// bios_read_uart(dst, size, uart_divisor_override /* -1 - don't override */) -> err
#define bios_read_uart  API_FN(6, int,      char*, int, int)

// bios_sdread(dst, sector, sector_count) -> sector_count
#define bios_sdread     API_FN(7, unsigned, void*, unsigned, unsigned)

// bios_sdwrite(src, sector, sector_count) -> sector_count
#define bios_sdwrite    API_FN(8, unsigned, const void*, unsigned, unsigned)

#define bios_get_keyboard_report API_FN(9, int, struct KeyboardReport*)) /* -> err */

inline unsigned bios_get_sdcard_sector_count() { return *(unsigned*)(RAM_BASE + 4); }

inline char bios_get_text_style() { return *(unsigned*)(RAM_BASE + 8); }
inline void bios_set_text_style(char v) { *(unsigned*)(RAM_BASE + 8) = v; }

inline unsigned short* bios_get_cursor_ptr() { return *(unsigned short**)(RAM_BASE + 12); }
inline void bios_set_cursor_ptr(unsigned short* v) { *(unsigned short**)(RAM_BASE + 12) = v; }

#define putchar bios_putchar
#define printf bios_printf
#define sscanf bios_sscanf

#endif  // ENDEAVOUR2_BIOS_H

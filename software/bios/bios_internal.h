#ifndef BIOS_INTERNAL_H
#define BIOS_INTERNAL_H

#include <endeavour2/defs.h>
#include <endeavour2/bios_defs.h>

void display_putchar(unsigned c);
void putchar(char c);
void printf(const char* fmt, ...);
int sscanf(const char* str, const char* fmt, ...);
void readline(const char* prompt, char* buffer, unsigned max_size);

unsigned crc32(const char* data, int size);
void wait(unsigned t);  // waits 100ns * t

void uart_flush();
int read_uart(char* dst, int size, int divisor);

int strcmp(const char* s1, const char* s2);
unsigned long strlen(const char* s);

// memtest returns 0 on success
int fast_memtest();
int full_memtest(unsigned iter_count, unsigned modifier);

int run_binary(void* addr, int argc, void** argv);
void run_benchmarks();

void beep(unsigned duration_ms, unsigned frequency, int volume);
void playWav(void* filePtr, int volume);

void run_console();

int i2c_write(int addr, int size, const char* data);
int i2c_read(int addr, int size, char* data);

void init_display();
int set_video_mode(enum VideoModeId modeid, const struct VideoMode* mode);

void init_sdcard();
unsigned get_sdcard_rca();
unsigned get_sdcard_sector_count();
unsigned sdread(unsigned* dst, unsigned sector, unsigned sector_count);
unsigned sdwrite(const unsigned* src, unsigned sector, unsigned sector_count);

void init_usb_keyboard();
int get_keyboard_report(volatile struct KeyboardReport* data);

extern struct HartCfg hart_cfg[3];

void hart_run(int hartid, void* addr);

#endif  // BIOS_INTERNAL_H

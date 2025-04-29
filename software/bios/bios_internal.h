#ifndef BIOS_INTERNAL_H
#define BIOS_INTERNAL_H

#include <endeavour2/raw/defs.h>
#include <endeavour2/raw/bios_defs.h>

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
int run_command(const char* cmd_line);
int cmd_eval(const char* args);

int i2c_read(int addr, int size, char* data);
int i2c_write(int addr, int size, const char* data);
int i2c_set_reg(int addr, char reg, char value);

extern unsigned* cursor_ptr;
void init_display();
void set_dvi_frequency(unsigned freq);
int set_video_mode(enum VideoModeId modeid, const struct VideoMode* mode);
void show_logo(unsigned* base_ptr, int line, int column);

void init_sdcard();
unsigned get_sdcard_rca();
unsigned get_sdcard_sector_count();
unsigned sdread(unsigned* dst, unsigned sector, unsigned sector_count);
unsigned sdwrite(const unsigned* src, unsigned sector, unsigned sector_count);

void init_usb_keyboard();
int get_keyboard_report(volatile struct KeyboardReport* data);

extern volatile struct HartCfg hart_cfg[2];
void run_in_supervisor_mode(void* addr, unsigned long arg);

#endif  // BIOS_INTERNAL_H

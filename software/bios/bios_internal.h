#ifndef BIOS_INTERNAL_H
#define BIOS_INTERNAL_H

void wait(unsigned t);
void uart_flush();

int strcmp(const char* s1, const char* s2);
unsigned long strlen(const char* s);

// memtest returns 0 on success
int fast_memtest();
int full_memtest(unsigned iter_count, unsigned modifier);

void run_benchmarks();

void playWav(void* filePtr, int volume);

void init_console();
void run_console();

#endif  // BIOS_INTERNAL_H

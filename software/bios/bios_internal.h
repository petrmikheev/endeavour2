#ifndef BIOS_INTERNAL_H
#define BIOS_INTERNAL_H

void wait(unsigned t);
int strcmp(const char* s1, const char* s2);
void uart_flush();

int fast_memtest();
int full_memtest();
void run_benchmarks();
void playWav(void* filePtr, int volume, int seconds);

#endif  // BIOS_INTERNAL_H

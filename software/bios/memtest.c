#include <endeavour2/defs.h>

#include "bios_internal.h"

static unsigned xorshift_state;

static void xorshift_init(unsigned seed) {
  xorshift_state = seed ^ 0x4698a74f;
  if (xorshift_state == 0) xorshift_state = 0x4698a74f;
}

static unsigned xorshift32() {
  xorshift_state ^= xorshift_state << 13;
  xorshift_state ^= xorshift_state >> 17;
  xorshift_state ^= xorshift_state << 5;
  return xorshift_state;
}

static void clear_1mb(unsigned* dst, unsigned v) {
  for (int i = 0; i < 1024*1024/4; i += 16) {
    asm volatile("prefetch.w 64(%0)" :: "r" (dst+i));
    for (int j = i; j < i + 16; ++j) dst[j] = v;
  }
}

static void fill_1mb(unsigned* dst, unsigned modifier) {
  for (int i = 0; i < 1024*1024/4; i += 16) {
    asm volatile("prefetch.w 64(%0)" :: "r" (dst+i));
    unsigned base = ((unsigned long)(dst + i) << 4) ^ modifier;
    unsigned* line = dst + i;
    for (int j = 0; j < 16; ++j) line[j] = base ^ i;
  }
}

static void check_1mb(const unsigned* data, unsigned modifier, int* err_count) {
  int ec = *err_count;
  for (int i = 0; i < 1024*1024/4; i += 16) {
    asm volatile("prefetch.r 64(%0)" :: "r" (data+i));
    unsigned base = ((unsigned long)(data + i) << 4) ^ modifier;
    const unsigned* line = data + i;
    for (int j = 0; j < 16; ++j) {
      unsigned expected = base ^ i;
      unsigned actual = line[j];
      if (actual != expected && ++ec <= 8) {
        printf("\n\tmem[%08x] = %08x, expected %08x", line + j, actual, expected);
      }
    }
  }
  *err_count = ec;
}

#define MEMTEST_FROM ((void*)RAM_BASE + (1<<20))  // skip first mb
#define MEMTEST_TO   ((void*)RAM_BASE + BOARD_REGS->ram_size)

static int memtest_iteration(unsigned step, unsigned modifier, unsigned random_access_steps) {
  unsigned* from = MEMTEST_FROM;
  unsigned* to = MEMTEST_TO;
  step <<= 18;
  unsigned step_count = (to - from) / step;
  unsigned* middle = from + (step_count / 2) * step;
  int err_count = 0;
  for (unsigned* base = from; base < middle; base += step) fill_1mb(base, modifier);
  putchar('.');
  for (unsigned* base = middle; base < to; base += step) fill_1mb(base, modifier);
  putchar('.');
  for (unsigned* base = from; base < middle; base += step) check_1mb(base, modifier, &err_count);
  if (err_count == 0) putchar('.');
  for (unsigned* base = middle; base < to; base += step) check_1mb(base, modifier, &err_count);
  if (err_count > 0) {
    printf("\nFAILED batch access %u/%u errors, rerunning read stage...", err_count, step_count << 18);
    int err_count2 = 0;
    for (unsigned* base = from; base < to; base += step) check_1mb(base, modifier, &err_count2);
    printf("\nREREAD %u/%u errors\n", err_count2, step_count << 18);
    return err_count;
  }
  putchar('.');
  const unsigned batch_size = 2048;
  for (int j = 0; j < random_access_steps; ++j) {  // random access stage
    if (j == random_access_steps / 2) putchar('.');
    unsigned xstate = xorshift_state;
    unsigned mask = (BOARD_REGS->ram_size - 1) & ~(batch_size - 1);
    for (int i = 0; i < batch_size; ++i) {
      char* addr = (char*)(RAM_BASE + (long)(xorshift32() & mask)) + i;
      if (addr < (char*)from) continue;
      *addr = xorshift32();
    }
    xorshift_state = xstate;
    for (int i = 0; i < batch_size; ++i) {
      char* addr = (char*)(RAM_BASE + (long)(xorshift32() & mask)) + i;
      if (addr < (char*)from) continue;
      unsigned char actual = *addr;
      unsigned char expected = xorshift32();
      if (actual != expected && ++err_count <= 8) {
        printf("\n\tmem[%08x] = %02x, expected %02x", addr, actual, expected);
      }
    }
  }
  if (err_count > 0) {
    printf("\nFAILED random access %u/%u errors\n", err_count, random_access_steps * batch_size);
    return err_count;
  }
  printf(" OK\n");
  return 0;
}

int fast_memtest() {
  xorshift_init(0x3285a83d);
  printf("fast memtest");
  int c = 0;
  for (void* base = MEMTEST_FROM; base < MEMTEST_TO; base += 5<<20) {
    clear_1mb(base, 0);
    if ((++c & 127) == 0) putchar('.');
  }
  putchar('.');
  return memtest_iteration(5, 0, 1024);
}

int full_memtest(unsigned iter_count, unsigned seed) {
  int max_err = 0;
  xorshift_init(seed);
  unsigned clear_val = xorshift32();
  printf("Memtest range %08x - %08x; initializing with %8x...", MEMTEST_FROM, MEMTEST_TO, clear_val);
  for (void* base = MEMTEST_FROM; base < MEMTEST_TO; base += 1<<20) clear_1mb(base, clear_val);
  printf(" DONE\n");
  for (unsigned iter = 0; iter < iter_count; ++iter) {
    unsigned modifier = xorshift32();
    printf("\titer=%-3u\txor=%08x\tmemtest", iter, modifier);
    int err_count = memtest_iteration(1, modifier, 8192);
    if (err_count > max_err) max_err = err_count;
  }
  return max_err;
}

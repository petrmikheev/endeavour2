#include <endeavour2/raw/defs.h>

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

void memset_1mb(unsigned* dst, unsigned v);  // implemented in bench_impl.c

// implemented in memtest_impl.c
void check_clear_1mb(const unsigned* data, unsigned v, unsigned* err_count);
void fill_and_check_sparse_1mb(unsigned* dst, unsigned old, unsigned new, unsigned step, unsigned* err_count);
void fill_1mb(unsigned* dst, unsigned modifier);
unsigned check_1mb(const unsigned* data, unsigned modifier, unsigned* err_count);

#define MEMTEST_FROM ((void*)RAM_BASE + (8<<20))  // skip first 8 mb
#define MEMTEST_TO   ((void*)RAM_BASE + BOARD_REGS->ram_size)
#define RANDOM_ACCESS_BATCH_SIZE 2048

static unsigned memtest_random_access(unsigned random_access_steps) {
  unsigned err_count = 0;
  unsigned* from = MEMTEST_FROM;
  unsigned* to = MEMTEST_TO;
  unsigned mask = (BOARD_REGS->ram_size - 1) & ~(RANDOM_ACCESS_BATCH_SIZE - 1);
  for (int j = 0; j < random_access_steps; ++j) {
    unsigned xstate = xorshift_state;
    for (int i = 0; i < RANDOM_ACCESS_BATCH_SIZE; ++i) {
      char* addr = (char*)(RAM_BASE + (long)(xorshift32() & mask)) + i;
      if (addr < (char*)from) continue;
      *addr = xorshift32();
    }
    xorshift_state = xstate;
    for (int i = 0; i < RANDOM_ACCESS_BATCH_SIZE; ++i) {
      char* addr = (char*)(RAM_BASE + (long)(xorshift32() & mask)) + i;
      if (addr < (char*)from) continue;
      unsigned char actual = *addr;
      unsigned char expected = xorshift32();
      if (__builtin_expect(actual != expected, 0) && ++err_count <= 8) {
        printf("\n\tmem[%08x] = %02x, expected %02x", addr, actual, expected);
      }
    }
  }
  return err_count;
}

static int memtest_iteration(unsigned step, unsigned modifier, unsigned random_access_steps) {
  unsigned* from = MEMTEST_FROM;
  unsigned* to = MEMTEST_TO;
  step <<= 18;
  unsigned step_count = (to - from) / step;
  unsigned err_count_clear = 0;
  unsigned err_count_sparse = 0;
  unsigned err_count_fill = 0;
  for (unsigned* base = from; base < to; base += step) memset_1mb(base, modifier);
  putchar('.');
  for (unsigned* base = from; base < to; base += step) fill_and_check_sparse_1mb(base, modifier, ~modifier, 17, &err_count_sparse);
  for (unsigned* base = from; base < to; base += step) fill_and_check_sparse_1mb(base, ~modifier, modifier, 17, &err_count_sparse);
  putchar('.');
  if (err_count_sparse > 0) {
    printf("\nFAILED sparse stage %u/%u errors\n", err_count_sparse, step_count << 14);
  }
  for (unsigned* base = from; base < to; base += step) check_clear_1mb(base, modifier, &err_count_clear);
  putchar('.');
  if (err_count_clear > 0) {
    printf("\nFAILED clear stage %u/%u errors\n", err_count_clear, step_count << 18);
  }
  for (unsigned* base = from; base < to; base += step) fill_1mb(base, modifier);
  putchar('.');
  for (unsigned* base = from; base < to; base += step) check_1mb(base, modifier, &err_count_fill);
  putchar('.');
  if (err_count_fill > 0) {
    printf("\nFAILED batch access %u/%u errors, rerunning read stage...", err_count_fill, step_count << 18);
    unsigned err_count2 = 0;
    for (unsigned* base = from; base < to; base += step) check_1mb(base, modifier, &err_count2);
    printf("\nREREAD %u/%u errors\n", err_count2, step_count << 18);
  }
  unsigned err_count_random = memtest_random_access(random_access_steps);
  if (err_count_random > 0) {
    printf("\nFAILED random access %u/%u errors\n", err_count_random, random_access_steps * RANDOM_ACCESS_BATCH_SIZE);
  }
  if (err_count_clear | err_count_fill | err_count_random) {
    printf(" FAILED\n");
    return 1;
  }
  printf(" OK\n");
  return 0;
}

int fast_memtest() {
  xorshift_init(0x3285a83d);
  printf("fast memtest");
  return memtest_iteration(5, 0, 512);
}

int full_memtest(unsigned iter_count, unsigned seed) {
  int err = 0;
  xorshift_init(seed);
  for (unsigned iter = 0; iter < iter_count; ++iter) {
    unsigned modifier = xorshift32();
    printf("\titer=%-3u\txor=%08x\tmemtest", iter, modifier);
    err |= memtest_iteration(1, modifier, 2048);
    if ((GPIO_REGS->data_in & (GPIO_KEY0 | GPIO_KEY1)) || UART_REGS->rx >= 0) break;
  }
  return err;
}

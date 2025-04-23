#include "bios_internal.h"

void check_clear_1mb(const unsigned* data, unsigned v, unsigned* err_count) {
  unsigned ec = *err_count;
  for (int i = 0; i < 1024*1024/4; i += 4) {
    for (int j = i; j < i + 4; ++j) ec += (data[j] != v);
  }
  *err_count = ec;
}

void fill_1mb(unsigned* dst, unsigned modifier) {
  for (int i = 0; i < 1024*1024/4; i += 4) {
    unsigned base = ((unsigned long)(dst + i) << 2) ^ modifier;
    unsigned* line = dst + i;
    for (int j = 0; j < 4; ++j) line[j] = base ^ j;
  }
}

void check_1mb(const unsigned* data, unsigned modifier, unsigned* err_count) {
  unsigned ec = *err_count;
  for (int i = 0; i < 1024*1024/4; i += 4) {
    asm volatile("prefetch.r 64(%0)" :: "r" (data+i));
    unsigned base = ((unsigned long)(data + i) << 2) ^ modifier;
    const unsigned* line = data + i;
    for (int j = 0; j < 4; ++j) {
      unsigned expected = base ^ j;
      unsigned actual = line[j];
      if (__builtin_expect(actual != expected, 0) && ++ec <= 8) {
        printf("\n\tmem[%08x] = %08x, expected %08x", line + j, actual, expected);
      }
    }
  }
  *err_count = ec;
}

void fill_and_check_sparse_1mb(unsigned* data, unsigned old, unsigned new, unsigned step, unsigned* err_count) {
  unsigned ec = *err_count;
  for (int i = 0; i < 1024*1024/4; i += step) {
    asm volatile("prefetch.r 64(%0)" :: "r" (data+i));
    if (__builtin_expect(data[i] != old, 0) && ++ec <= 8) {
      printf("\n\tmem[%08x] = %08x, expected %08x", data + i, data[i], old);
    }
    data[i] = new;
  }
  *err_count = ec;
}

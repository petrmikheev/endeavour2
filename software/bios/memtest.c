#include <endeavour2/defs.h>
#include <endeavour2/bios.h>

int fast_memtest() {
  printf("memtest");
  char* ram = (char*)RAM_BASE;
  const int batch_size = BOARD_REGS->ram_size >> 4;
  const int base_step = 16;
  const int start = 256 * 64;  // skip first 64KB
  int i = start, j = start;
  for (int b = 0; b < 4; ++b, j -= batch_size) {
    const int step = base_step + (b & 1);
    for (; j < batch_size; j += step, i += step) {
      char* ptr = ram + (i << 2);
      *(int*)ptr = i ^ (i << 25);  // test 4 byte write
      *(ptr + 1) ^= 0xff;  // test 1 byte write
    }
    bios_putchar('.');
  }
  int check_count = 0;
  int err_count = 0;
  i = start, j = start;
  for (int b = 0; b < 4; ++b, j -= batch_size) {
    const int step = base_step + (b & 1);
    for (; j < batch_size; j += step, i += step, check_count++) {
      char* ptr = ram + (i << 2);
      unsigned expected = (i ^ (i << 25)) ^ 0xff00;
      unsigned actual = *(unsigned*)ptr;
      if (*(ptr + 3) != (expected >> 24)  // test 1 byte read
          || actual != expected) {    // test 4 byte read
        if (++err_count <= 8) {
          printf("\n\tmem[%08x] = %08x, expected %08x", i << 2, actual, expected);
        }
      }
    }
    bios_putchar('.');
  }
  if (err_count) {
    printf("\nFAILED %u/%u errors\n", err_count, check_count);
  } else {
    printf(" OK\n");
  }
  return err_count == 0;
}

int full_memtest() {
  return fast_memtest();  // TODO
}
#include <endeavour2/defs.h>
#include <endeavour2/bios.h>

void wait(unsigned t) {
  unsigned start = time_100nsec();
  while (1) {
    if ((time_100nsec() - start) > t) return;
  }
}

void print_cpu_info() {
  printf("CPU: rv32im");
  unsigned isa;
  asm volatile("csrr %0, misa" : "=r" (isa));
  isa &= ~0x1100;  // exclude 'i', 'm'
  for (int i = 0; i < 26; ++i) {
    if (isa & 1) putchar('a' + i);
    isa = isa >> 1;
  }
  int cores = BOARD_REGS->hart_count;
  printf(", %d core", cores);
  if (cores > 1) putchar('s');
  printf(", %uMhz\nExtensions: zicsr, zifencei", BOARD_REGS->cpu_frequency / 1000000);
  unsigned features = BOARD_REGS->cpu_features;
  if (features & CPU_FEATURES_ZBA) printf(", zba");
  if (features & CPU_FEATURES_ZBB) printf(", zbb");
  if (features & CPU_FEATURES_ZBC) printf(", zbc");
  if (features & CPU_FEATURES_ZBS) printf(", zbs");
  if (features & CPU_FEATURES_ZICBOP) printf(", zicbop");
  if (features & CPU_FEATURES_ZICBOM) printf(", zicbom");
  printf("\n");
}

int memtest() {
  printf("RAM: %uMB\tmemtest", RAM_SIZE >> 20);
  char* ram = (char*)RAM_BASE;
  const int batch_size = RAM_SIZE >> 4;
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
          printf("\n\tmem[%8x] = %8x, expected %8x", i << 2, actual, expected);
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

void bench();

int main() {
  printf(
      "\n\n"
      "\t\t\tEndeavour2\n"
      "\t\t\t==========\n\n"
  );
  print_cpu_info();
  memtest();

  bench();

  AUDIO_REGS->cfg = AUDIO_SAMPLE_RATE(2400) | AUDIO_VOLUME(3); // beep 300Hz
  bios_beep(0, 90);
  bios_beep(256, 90);
  wait(5000000);

  BOARD_REGS->reset = 1;
}

void fatal_trap_handler(unsigned cause, unsigned tval, unsigned epc, unsigned sp, unsigned ra) {
  printf("\nTRAP\n\tcause = %8x\n\ttval  = %8x\n\tepc   = %8x\n\tsp\t= %8x\n\tra\t= %8x\n", cause, tval, epc, sp, ra);
  //bios_uart_console();
}

#define SBI_DEBUG

#define SBI_OK 0
#define SBI_NOT_SUPPORTED -2

#define SBI_EXT_TIMER 0x54494D45
#define SBI_EXT_RESET 0x53525354

struct sbiret {
  int error;
  int value;
};

struct sbiret sbi_handler(int arg, int argh, int fn_id, int ext_id) {
  if (ext_id == 0x10) {
    if (fn_id == 0) return (struct sbiret){SBI_OK, 2}; // SBI spec version -> 0.2
    if (fn_id == 3) {
      if (arg == SBI_EXT_TIMER) return (struct sbiret){SBI_OK, 1};
      //if (arg == SBI_EXT_RESET) return (struct sbiret){SBI_OK, 1};
#ifdef SBI_DEBUG
      printf("[SBI] Probing extension 0x%x\n", arg);
#endif
      return (struct sbiret){SBI_OK, 0};
    }
    return (struct sbiret){SBI_OK, 0};
  }
  // Note: SBI_EXT_TIMER is handled in asm.S
  /*if (ext_id == SBI_EXT_RESET) {
#ifdef SBI_DEBUG
    printf("[SBI] Reset requested. Return to BIOS console.\n");
    bios_uart_console();
#else
    BOARD_REGS->reset = 1;
#endif
  }*/
#ifdef SBI_DEBUG
  printf("[SBI] Function not supported ext=0x%x, fn=0x%x, arg=0x%x\n", ext_id, fn_id, arg);
#endif
  return (struct sbiret){SBI_NOT_SUPPORTED, 0};
}

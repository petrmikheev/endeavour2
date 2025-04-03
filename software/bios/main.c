#include <endeavour2/defs.h>
#include <endeavour2/bios.h>

#include "bios_internal.h"

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
  printf(", %u MHz\nExtensions: zicsr, zifencei", BOARD_REGS->cpu_frequency / 1000000);
  unsigned features = BOARD_REGS->cpu_features;
  if (features & CPU_FEATURES_ZBA) printf(", zba");
  if (features & CPU_FEATURES_ZBB) printf(", zbb");
  if (features & CPU_FEATURES_ZBC) printf(", zbc");
  if (features & CPU_FEATURES_ZBS) printf(", zbs");
  if (features & CPU_FEATURES_ZICBOP) printf(", zicbop");
  if (features & CPU_FEATURES_ZICBOM) printf(", zicbom");
  printf("\n");
}

int main() {
  BOARD_REGS->leds = 0;
  bios_beep(333, 300, 6);
  unsigned ram_size = BOARD_REGS->ram_size;
  if (ram_size >= (1<<20)) {
    unsigned* ram = (unsigned*)RAM_BASE;
    for (int i = BIOS_SIZE/4; i < (1<<20)/4; ++i) ram[i] = 0;
  }

  printf(
      "\n\n"
      "\t\t\tEndeavour2\n"
      "\t\t\t==========\n\n"
  );
  print_cpu_info();

  if (ram_size < (1<<20)) {
    printf("RAM: %u KB\n", ram_size >> 10);
  } else {
    printf("RAM: %u MB\t", ram_size >> 20);
    if (fast_memtest() != 0) {
      bios_beep(1000, 300, 6);
    }
  }

  init_sdcard();
  init_usb_keyboard();

  run_benchmarks();

  run_console();
  while(1);
}

void fatal_trap_handler(unsigned cause, unsigned tval, unsigned epc, unsigned sp, unsigned ra) {
  printf("\nTRAP\n\tcause = %08x\n\ttval  = %08x\n\tepc   = %08x\n\tsp\t= %08x\n\tra\t= %08x\n", cause, tval, epc, sp, ra);
  run_console();
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
    if (fn_id == 0) return (struct sbiret){SBI_OK, 2}; // SBI spec version = 0.2
    if (fn_id == 3) {
      if (arg == SBI_EXT_TIMER) return (struct sbiret){SBI_OK, 1};
      if (arg == SBI_EXT_RESET) return (struct sbiret){SBI_OK, 1};
#ifdef SBI_DEBUG
      printf("[SBI] Probing extension 0x%x\n", arg);
#endif
      return (struct sbiret){SBI_OK, 0};
    }
    return (struct sbiret){SBI_OK, 0};
  }
  // Note: SBI_EXT_TIMER is handled in asm.S
  if (ext_id == SBI_EXT_RESET) {
    printf("[SBI] Reset requested.\n");
    wait(10000000);
    BOARD_REGS->reset = 1;
  }
#ifdef SBI_DEBUG
  printf("[SBI] Function not supported ext=0x%x, fn=0x%x, arg=0x%x\n", ext_id, fn_id, arg);
#endif
  return (struct sbiret){SBI_NOT_SUPPORTED, 0};
}

#include <endeavour2/defs.h>

#include "bios_internal.h"
#include "ext2.h"

void print_cpu_info() {
  for (int hartid = 0; hartid < BOARD_REGS->hart_count; ++hartid) {
    printf(hartid == 0 ? "CPU " : "\t");
    unsigned isa;
    if (hartid == 0) {
      asm volatile("csrr %0, misa" : "=r" (isa));
    } else if (hart_cfg[hartid - 1].ready) {
      isa = hart_cfg[hartid - 1].isa;
    } else {
      printf("core%u: error\n", hartid);
      continue;
    }
    printf("core%u: rv32", hartid);
    const char* seq = "iemafdc";
    while (*seq) {
      unsigned bit = 1u << (*seq - 'a');
      if (isa & bit) {
        putchar(*seq);
        isa &= ~bit;
      }
      seq++;
    }
    for (int i = 0; i < 26; ++i) {
      if (isa & 1) putchar('a' + i);
      isa = isa >> 1;
    }
    printf(", zicsr, zifencei");
    unsigned features = BOARD_REGS->cpu_features[hartid];
    if (features & CPU_FEATURES_ZBA) printf(", zba");
    if (features & CPU_FEATURES_ZBB) printf(", zbb");
    if (features & CPU_FEATURES_ZBC) printf(", zbc");
    if (features & CPU_FEATURES_ZBS) printf(", zbs");
    if (features & CPU_FEATURES_ZICBOP) printf(", zicbop");
    if (features & CPU_FEATURES_ZICBOM) printf(", zicbom");
    printf(", %u MHz\n", BOARD_REGS->cpu_frequency / 1000000);
  }
}

int main() {
  BOARD_REGS->leds = 0;
  for (int hartid = 1; hartid < BOARD_REGS->hart_count; ++hartid)
    software_interrupt(hartid);  // triggers initilization code
  beep(333, 300, 6);

  unsigned ram_size = BOARD_REGS->ram_size;
  if (ram_size >= (2<<20)) {
    init_display();
  }

  printf(
      "\n\n"
      "\t\t\tEndeavour2\n"
      "\t\t\t==========\n\n"
  );
  show_logo(cursor_ptr, -3, 25);
  print_cpu_info();

  if (ram_size < (1<<20)) {
    printf("RAM: %u KB\n", ram_size >> 10);
  } else {
    printf("RAM: %u MB\t", ram_size >> 20);
    if (ram_size >= (8<<20) && fast_memtest() != 0) {
      beep(1000, 300, 6);
    }
  }

  init_sdcard();
  if (get_sdcard_sector_count() > 0) {
    int p = search_and_select_ext2_fs();
    if (p == 0) {
      printf("\tSelected EXT2 filesystem in first sector\n");
    } else if (p > 0) {
      printf("\tSelected EXT2 filesystem on partition %u\n", p);
    }
  }
  init_usb_keyboard();

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

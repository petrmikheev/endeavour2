#include <endeavour2/defs.h>

#include "bios_internal.h"
#include "ext2.h"

void print_cpu_info() {
  for (int hartid = 0; hartid < BOARD_REGS->hart_count; ++hartid) {
    printf(hartid == 0 ? "CPU " : "\t");
    unsigned isa = hartid < 2 ? hart_cfg[hartid].isa : 0;
    if (isa == 0) {
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
  if (BOARD_REGS->hart_count > 1) software_interrupt(1);  // triggers initilization code for second core
  beep(333, 300, 6);

  unsigned ram_size = BOARD_REGS->ram_size;
  if (ram_size >= (8<<20)) {
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
    printf("RAM: %u MB, %u MHz\t", ram_size >> 20, BOARD_REGS->ram_frequency / 1000000);
    if (ram_size >= (8<<20) && fast_memtest() != 0) {
      beep(1000, 300, 6);
    }
  }

  init_sdcard();
  if (get_sdcard_sector_count() > 0 && ram_size >= (2<<20)) {
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
  unsigned hartid = get_hartid();
  printf("\nTRAP (hart %u)\n\tcause = %08x\n\ttval  = %08x\n\tepc   = %08x\n\tsp\t= %08x\n\tra\t= %08x\n", hartid, cause, tval, epc, sp, ra);
  if (hartid == 0) run_console();
}

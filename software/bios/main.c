#include <endeavour2/raw/defs.h>

#include "bios_internal.h"
#include "ext2.h"

// *** Memory map (at 0x80000000 in address space)
//    0    -   32 KB    : code (see bios.lds)
//   32 KB - 1536 KB    : tmp buffer
//  512 KB - 1536 KB    : memory benchmark, page1
// 1536 KB - 1564 KB    : EXT2 buffer
// 1568 KB - 1572 KB    : console script buffer
// 1572 KB - 1604 KB    : DTB buffer
// 1792 KB - 2048 KB    : display buffer, text
// 2048 MB - 6368 MB    : display buffer, graphic
//    7 MB -    8 MB    : memory benchmark, page2
//    8 MB - 1024 MB    : used during memtest

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
    if (features & CPU_FEATURES_SSTC) printf(", sstc");
    printf(", %u MHz\n", BOARD_REGS->cpu_frequency / 1000000);
  }
}

void autoboot() {
  unsigned cb = GPIO_REGS->data_in & (GPIO_CBSEL0 | GPIO_CBSEL1);
  const char* conf_name = 0;
  switch (cb) {
    case 0: conf_name = "boot.0.conf"; break;
    case GPIO_CBSEL0: conf_name = "boot.1.conf"; break;
    case GPIO_CBSEL1: conf_name = "boot.2.conf"; break;
    case GPIO_CBSEL0 | GPIO_CBSEL1: conf_name = "boot.3.conf"; break;
    default:;
  }
  struct Inode* inode = find_inode(conf_name);
  if (!inode) {
    conf_name = "boot.conf";
    inode = find_inode(conf_name);
  }
  if (inode && is_regular_file(inode)) {
    printf("Autostarting /%s\n", conf_name);
    cmd_eval(conf_name);
  }
}

int main() {
  GPIO_REGS->data_clear = 0xf;  // clear leds
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

  if ((GPIO_REGS->data_in & GPIO_BOOT_EN) && is_ext2_reader_initialized()) autoboot();

  run_console();
  while(1);
}

void fatal_trap_handler(unsigned cause, unsigned tval, unsigned epc, unsigned sp, unsigned ra) {
  unsigned hartid = get_hartid();
  printf("\nTRAP (hart %u)\n\tcause = %08x\n\ttval  = %08x\n\tepc   = %08x\n\tsp\t= %08x\n\tra\t= %08x\n", hartid, cause, tval, epc, sp, ra);
  if (hartid == 0) run_console();
}

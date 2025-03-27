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

const char* console_help_msg =
      "\nUART console. Commands:\n"
      "\tW addr val\t\t\t- save 4B to RAM\n"
      "\tR addr\t\t\t\t- load 4B from RAM\n"
      "\tSD addr sector\t\t- load SD card sector to addr\n"
      "\tUART addr size\t\t- receive size(decimal) bytes via UART\n"
      "\tFUART addr size\t   - UART with baud rate 12Mhz\n"
      "\tCRC32 addr size [expected] - calculate crc of data in RAM\n"
      "\tMEMTEST\t\t\t   - rerun memtest\n"
      "\tJ addr\t\t\t\t- run code at addr\n\n";

int main() {
  BOARD_REGS->leds = 0;
  bios_beep(256, 90);

  printf(
      "\n\n"
      "\t\t\tEndeavour2\n"
      "\t\t\t==========\n\n"
  );
  print_cpu_info();

  unsigned ram_size = BOARD_REGS->ram_size;
  if (ram_size < (1<<20)) {
    printf("RAM: %u KB\n", ram_size >> 10);
  } else {
    printf("RAM: %u MB\t", ram_size >> 20);
    if (!fast_memtest()) {
      bios_beep(256, 300);
    }
  }

  run_benchmarks();

  printf("\n\n");
  printf(console_help_msg);
  while (1) {
    #define CMD_BUF_SIZE 120
    char cmd[CMD_BUF_SIZE];
    bios_readline("> ", cmd, CMD_BUF_SIZE);
    if (*cmd == 0) continue;
    unsigned long addr;
    unsigned val;
    int size, c;
    int fast_uart = cmd[0] == 'F';
    switch (cmd[0]) {
      case 'w': {
        int volume = 6;
        int seconds = -1;
        if (bios_sscanf(cmd, "wav %x %d %d", &addr, &volume, &seconds) >= 1) {
          playWav((void*)addr, volume, seconds);
          continue;
        }
        break;
      }
      case 'W':
        if (bios_sscanf(cmd, "W %x %x", &addr, &val) == 2) {
          *(volatile unsigned*)addr = val;
          continue;
        }
        break;
      case 'R':
        if (bios_sscanf(cmd, "R %x", &addr) == 1) {
          bios_printf("%08x\n", *(volatile unsigned*)addr);
          continue;
        }
        break;
      /*case 'S':
        if (bios_sscanf(cmd, "SD %x %x", &addr, &val) == 2) {
          int res = bios_sdread((unsigned*)addr, val, 1);
          if (!res) bios_printf("SD error\n");
          continue;
        }
        break;*/
      case 'U':
      case 'F':
        if (bios_sscanf(cmd + fast_uart, "UART %x %d", &addr, &size) == 2) {
          bios_read_uart((char*)addr, size, fast_uart ? UART_BAUD_RATE(12000000) : -1);
          continue;
        }
        break;
      case 'C':
        val = 0;
        c = bios_sscanf(cmd, "CRC32 %x %d %x", &addr, &size, &val);
        if (c == 2 || c == 3) {
          unsigned crc = bios_crc32((char*)addr, size);
          bios_printf("%08x", crc);
          if (val) {
            bios_printf(crc == val ? " OK" : " ERROR");
          }
          putchar('\n');
          continue;
        }
        break;
      case 'M':
        full_memtest();
        continue;
      case 'J':
        if (bios_sscanf(cmd, "J %x", &addr) == 1) {
          asm("fence.i");
          ((void (*)())addr)();
          continue;
        }
        break;
      case 'h':
      case 'H':
        bios_printf(console_help_msg);
      case '\n': continue;
    }
    bios_printf("Invalid command\n");
    uart_flush();
  }
}

void fatal_trap_handler(unsigned cause, unsigned tval, unsigned epc, unsigned sp, unsigned ra) {
  printf("\nTRAP\n\tcause = %08x\n\ttval  = %08x\n\tepc   = %08x\n\tsp\t= %08x\n\tra\t= %08x\n", cause, tval, epc, sp, ra);
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

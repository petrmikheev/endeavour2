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
  printf(", %uMhz\n", BOARD_REGS->cpu_frequency / 1000000);
}

int main() {
  printf(
      "\n\n"
      "\t\t\tEndeavour2\n"
      "\t\t\t==========\n\n"
  );
  print_cpu_info();

  AUDIO_REGS->cfg = AUDIO_SAMPLE_RATE(2400) | AUDIO_VOLUME(3); // beep 300Hz
  bios_beep(0, 90);
  bios_beep(256, 90);

  for (int i = 0; i < 16; ++i) {
    BOARD_REGS->leds = i;
    wait(5000000);
  }

  BOARD_REGS->reset = 1;
  while (1);
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

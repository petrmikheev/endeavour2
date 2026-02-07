#include "bios_internal.h"

#define SBI_DEBUG

#define SBI_OK                 0
#define SBI_ERR_FAILED        -1
#define SBI_NOT_SUPPORTED     -2
#define SBI_ERR_INVALID_PARAM -3

#define SBI_EXT_HSM       0x48534D    // hart state management
#define SBI_EXT_IPI       0x735049    // inter-processor interrupt
#define SBI_EXT_RFENCE    0x52464E43  // remote fence.i
#define SBI_EXT_TIMER     0x54494D45
#define SBI_EXT_RESET     0x53525354
#define SBI_EXT_ENDEAVOUR 0x0A000000

void sbi_hart_start();
void stop_core();

struct sbiret {
  int error;
  int value;
};

struct sbiret sbi_handler(unsigned arg, unsigned arg2, unsigned arg3, int fn_id, int ext_id) {
  if (ext_id == SBI_EXT_ENDEAVOUR) {
    if (fn_id == 0) {  // print char
      if (arg == '\t')
        printf("\t");
      else if ((arg >= 32 && arg < 128) || arg == '\n')
        putchar(arg);
      return (struct sbiret){SBI_OK, 0};
    }
    if (fn_id == 1) { return (struct sbiret){SBI_OK, get_sdcard_sector_count()}; }
    if (fn_id == 2) { return (struct sbiret){SBI_OK, get_sdcard_rca()}; }
    if (fn_id == 3) {
      set_dvi_frequency(arg);
      return (struct sbiret){SBI_OK, 0};
    }
    if (fn_id == 4) {
      return (struct sbiret){SBI_OK, get_seconds_since_2000()};
    }
    if (fn_id == 5) {
      set_seconds_since_2000(arg);
      return (struct sbiret){SBI_OK, 0};
    }
  }
  if (ext_id == 0x10) {
    if (fn_id == 0) return (struct sbiret){SBI_OK, 3}; // SBI spec version = 0.3
    if (fn_id == 3) {
      if (arg == SBI_EXT_TIMER || arg == SBI_EXT_RESET || arg == SBI_EXT_ENDEAVOUR
        || arg == SBI_EXT_HSM || arg == SBI_EXT_IPI || arg == SBI_EXT_RFENCE) return (struct sbiret){SBI_OK, 1};
#ifdef SBI_DEBUG
      printf("[SBI] Probing extension 0x%x\n", arg);
#endif
      return (struct sbiret){SBI_OK, 0};
    }
    return (struct sbiret){SBI_OK, 0};
  }
  // Note: SBI_EXT_TIMER is handled in asm.S
  if (ext_id == SBI_EXT_RESET) {
    if (arg == 0) {
      printf("[SBI] Shutdown\n");
      set_video_mode(0, 0);
      while (1);
    } else {
      printf("[SBI] Reboot\n");
      wait(10000000);
      BOARD_REGS->reset = 1;  // noreturn
    }
  }
  if (ext_id == SBI_EXT_HSM) {
    if (fn_id == 1) {  // sbi_hart_stop
      stop_core();
    }
    if (arg >= BOARD_REGS->hart_count) return (struct sbiret){SBI_ERR_INVALID_PARAM, 0};
    volatile struct HartCfg* hcfg = &hart_cfg[arg];
    if (fn_id == 0) {  // sbi_hart_start
      hcfg->action = HART_ACTION_FENCEI | HART_ACTION_JUMP;
      hcfg->jump_to = &run_in_supervisor_mode;
      hcfg->epc = arg2;
      hcfg->tval = arg3;
      software_interrupt(arg);
      return (struct sbiret){SBI_OK, 0};
    }
    if (fn_id == 2) {  // sbi_hart_get_status
      return (struct sbiret){SBI_OK, hcfg->jump_to ? 0 /*started*/ : 1 /*stopped*/};
    }
  }
  if (ext_id == SBI_EXT_IPI || ext_id == SBI_EXT_RFENCE) {
    unsigned action = ext_id == SBI_EXT_IPI ? HART_ACTION_IPI : HART_ACTION_FENCEI;
    unsigned mask = arg2 == -1 ? -1 : (arg << arg2);
    int hart_count = BOARD_REGS->hart_count;
    for (int i = 0; i < hart_count; ++i) {
      if (mask & 1) {
        hart_cfg[i].action = action;
        software_interrupt(i);
      }
      mask >>= 1;
    }
    return (struct sbiret){SBI_OK, 0};
  }
#ifdef SBI_DEBUG
  printf("[SBI] Function not supported ext=0x%x, fn=0x%x, arg=0x%x\n", ext_id, fn_id, arg);
#endif
  return (struct sbiret){SBI_NOT_SUPPORTED, 0};
}

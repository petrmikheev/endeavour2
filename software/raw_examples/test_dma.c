#include <endeavour2/raw/bios.h>

/*static int test_memset(const unsigned* data, unsigned v) {
  for (int i = 0; i < 1024*1024/4; ++i) {
    if (data[i] != v) {
      printf("[ERROR] incorrect result: mem[%08x] = %08x, expected %08x\n", data + i, data[i], v);
      return -1;
    }
  }
  return 0;
}

void memset_1mb(unsigned* dst, unsigned v) {
  for (int i = 0; i < 1024*1024/4; ++i) {
    dst[i] = v;
  }
}

void memset_1mb_dma(unsigned* dst, unsigned v) {
  struct DmaCmd { unsigned lo, hi; }* commands = (void*)(RAM_BASE + BIOS_SIZE * 4);

  commands[0].lo = v;  // fill bytes 0-4096 in internal buffer with `v`
  commands[0].hi = DMA_CMD_HI(DMA_SET, 0, 4096);

  for (unsigned i = 0; i < 1024*1024/4096; ++i) {
    commands[i + 1].lo = (unsigned long)dst + i*4096;
    commands[i + 1].hi = DMA_CMD_HI(DMA_WRITE, 0, 4096);
  }

  asm volatile("fence w, w");

  DMA_REGS->cmdAddress = (void*)commands;
  DMA_REGS->cmdCount = 257;

  while (!DMA_REGS->int_stat);
}*/

int main() {
  unsigned* cmds = (unsigned*)(RAM_BASE + BIOS_SIZE * 4);
  unsigned* page = (unsigned*)(RAM_BASE + 0x080000);
  cmds[0] = 0x12345678;
  cmds[1] = DMA_CMD_HI(DMA_SET, 0, 640*2);
  cmds[2] = -1;
  cmds[3] = DMA_CMD_HI(DMA_SET, 2, 6);
  cmds[4] = 0x080000;
  cmds[5] = DMA_CMD_HI(DMA_WRITE, 0, 64);
  DMA_REGS->cmdAddress = cmds;
  asm volatile("fence ow, ow");
  DMA_REGS->cmdCount = 3;
  while (!DMA_REGS->int_stat);
  printf("%08x %08x %08x %08x\n", page[0], page[1], page[2], page[3]);
  /*for (int i = 0; i < 100; ++i) {
    printf("Test DMA\n");
    memset_1mb(cmds, 0x98765432);
    memset_1mb(page, 0x333);
    memset_1mb_dma(page, 0x444);
    if (test_memset(page, 0x444) < 0) break;
  }*/
  return 0;
}

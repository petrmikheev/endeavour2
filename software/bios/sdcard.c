// Minimalistic driver for sdspi (https://github.com/ZipCPU/sdspi) controller.
// Supports only SDHC and SDXC cards.

#include <endeavour2/defs.h>

#include "bios_internal.h"

// Constants are copied from https://github.com/ZipCPU/sdspi/blob/master/sw/sdiodrv.c
static const unsigned
    // Command bit enumerations
    SDIO_CMD      = 0x00000040,
    SDIO_R1       = 0x00000100,
    SDIO_R2       = 0x00000200,
    SDIO_R1b      = 0x00000300,
    SDIO_WRITE    = 0x00000400,
    SDIO_MEM      = 0x00000800,
    SDIO_FIFO     = 0x00001000,
    SDIO_ERR      = 0x00008000,
    SDIO_REMOVED  = 0x00040000,
    SDIO_PRESENTN = 0x00080000,
    SDIO_BUSY     = 0x00104800,
    SDIO_ACK      = 0x04000000,
    // PHY enumerations
    SDIO_W4       = 0x00000400,
    SDIO_PUSHPULL = 0x00003000,
    SDIOCK_12MHZ  = 0x00000004,
    SDIOCK_25MHZ  = 0x00000003,
    SDIOCK_50MHZ  = 0x00000002,
    SDIOCK_100MHZ = 0x00000001,
    SDIOCK_200MHZ = 0x00000000,
    SDIOCK_SHUTDN = 0x00008000,
    SDPHY_1P8V    = 0x00400000,
		SDPHY_1P8VSPT = 0x00800000,
    SECTOR_512B   = 0x09000000;

#define BASE_CLK_IS_200MHZ

static const unsigned
#ifdef BASE_CLK_IS_200MHZ
  SDIOCK_SDR12  = SDIOCK_12MHZ,  // 25 MHz
  SDIOCK_SDR25  = SDIOCK_25MHZ,  // 50 MHz
  SDIOCK_SDR50  = SDIOCK_50MHZ,  // 100 MHz
  SDIOCK_SDR104 = SDIOCK_100MHZ; // 200 MHz
#else
  SDIOCK_SDR12  = SDIOCK_25MHZ,
  SDIOCK_SDR25  = SDIOCK_50MHZ;
  SDIOCK_SDR50  = SDIOCK_100MHZ;
#endif
#define SDIOCK_DEFAULT SDIOCK_SDR12

static unsigned command(unsigned cmd, unsigned arg) {
  SDCARD_REGS->data = arg;
  SDCARD_REGS->cmd = SDIO_CMD | SDIO_ERR | cmd;
  while (SDCARD_REGS->cmd & SDIO_BUSY);
  return SDCARD_REGS->data;
}

static unsigned rca;
extern unsigned sdcard_sector_count;

unsigned get_sdcard_rca() { return rca; }
unsigned get_sdcard_sector_count() { return sdcard_sector_count; }

void init_sdcard() {
  sdcard_sector_count = 0;
  printf("SD card: ");

  if (SDCARD_REGS->cmd & SDIO_PRESENTN) {
    printf("slot empty\n");
    return;
  }
  SDCARD_REGS->cmd = SDIO_REMOVED;

  SDCARD_REGS->phy = SDIOCK_DEFAULT | SECTOR_512B | SDIO_PUSHPULL;
  while (SDIOCK_DEFAULT != (SDCARD_REGS->phy & 0xff));

  // CMD0 - reset
  command(SDIO_REMOVED, 0);

  // CMD8 - check power range
  if ((command(SDIO_R1 | 8, 0x1a5) & 0xff) != 0xa5 || (SDCARD_REGS->cmd & SDIO_ERR)) {
    printf("no response\n");
    return;
  }

  // ACMD41
  const unsigned S18R = (1<<24), XPC = (1<<28), XCS = (1<<30);
  unsigned ocr;
  while(1) {
    unsigned ocr_query = 0xff8000 | XCS | S18R | XPC;
    command(SDIO_R1 | 55, 0);
    ocr = command(SDIO_R1 | 41, ocr_query);
    if ((SDCARD_REGS->cmd & 0x38000) == 0x8000) {
      printf("initialization failed\n");
      return;
    }
    if (ocr & 0x80000000) break;
  }

  const int card_supports_1v8 = 1;  // (ocr & (S18R | XCS)) == (S18R | XCS);
  if (card_supports_1v8) {
    command(SDIO_R1 | 11, 0);  // CMD11 - switch volatage to 1.8v
    SDCARD_REGS->phy |= SDPHY_1P8V | SDIOCK_SHUTDN;
    wait(50000);
    SDCARD_REGS->phy &= ~SDIOCK_SHUTDN;
    wait(10000);
  }

  // CMD2 - read CID
  command(SDIO_R2 | 2, 0);
  unsigned CID[4] = {SDCARD_REGS->fifo0, SDCARD_REGS->fifo0,
                     SDCARD_REGS->fifo0, SDCARD_REGS->fifo0};
  char product[6] = {CID[0], CID[1]>>24, CID[1]>>16, CID[1]>>8, CID[1], 0};
  printf(product);

  // CMD3 - read RCA
  rca = command(SDIO_R1 | 3, 0) & 0xffff0000;

  // CMD9 - read CSD
  command(SDIO_R2 | 9, rca);
  unsigned CSD[4] = {SDCARD_REGS->fifo0, SDCARD_REGS->fifo0,
                     SDCARD_REGS->fifo0, SDCARD_REGS->fifo0};
  int csd_type = (CSD[0] >> 30) & 3;
  if (csd_type != 1) {
    printf(" (CSD type %d, not supported)\n", csd_type);
    return;
  }
  unsigned csize = ((CSD[1] & 0x3f) << 16) | (CSD[2] >> 16);
  sdcard_sector_count = (csize + 1) * 1024;
  printf(" %u MB\n", sdcard_sector_count / 2048);

  // CMD7 - select card
  command(SDIO_R1b | 7, rca);

  // ACMD6 - set 4bit bus mode
  command(SDIO_R1 | 55, rca);
  command(SDIO_R1 | 6, 0x2);
  unsigned phy = SDCARD_REGS->phy | SDIO_W4;

  // CMD6 - check if HS mode (50MHz) is available
  phy = (phy & 0xf0ffffff) | (6<<24);  // 64 byte response
  SDCARD_REGS->phy = phy;

  command(SDIO_R1 | SDIO_MEM | 6, 0x00fffff1);
  (void)SDCARD_REGS->fifo0;
  (void)SDCARD_REGS->fifo0;
  (void)SDCARD_REGS->fifo0;
  unsigned modes = SDCARD_REGS->fifo0;
  if (SDCARD_REGS->cmd & SDIO_ERR) modes = 0;

  const int SDR25 = 0x020000, SDR50 = 0x040000, SDR104 = 0x080000, DDR50 = 0x100000;

  phy = (phy & ~0xf1f00ff) | SECTOR_512B | SDIOCK_SHUTDN | (/*sample shift*/ 16<<16);
  if (modes & SDR104) {
    command(SDIO_R1 | SDIO_MEM | 6, 0x80fffff3); // switch to SDR104
    phy |= SDIOCK_SDR104;
  } else if (modes & SDR50) {
    command(SDIO_R1 | SDIO_MEM | 6, 0x80fffff2); // switch to SDR50
    phy |= SDIOCK_SDR50;
  } else if (modes & SDR25) {
    command(SDIO_R1 | SDIO_MEM | 6, 0x80fffff1); // switch to SDR25
    phy |= SDIOCK_SDR25;
  } else {
    phy |= SDIOCK_SDR12;
  }

  SDCARD_REGS->phy = phy;
  while ((phy & 0xff) != (SDCARD_REGS->phy & 0xff));
}

static unsigned* receive_sector(unsigned* ptr, volatile unsigned* port) {
  unsigned* end = ptr + 128;
  while (ptr < end) {
    ptr[0] = *port;
    ptr[1] = *port;
    ptr[2] = *port;
    ptr[3] = *port;
    ptr += 4;
  }
  return ptr;
}

static const unsigned* send_sector(const unsigned* ptr, volatile unsigned* port) {
  const unsigned* end = ptr + 128;
  while (ptr < end) {
    *port = ptr[0];
    *port = ptr[1];
    *port = ptr[2];
    *port = ptr[3];
    ptr += 4;
  }
  return ptr;
}

static void sd_wait_ready() {
  int counter = 0;
  while (1) {
    unsigned status = command(SDIO_R1 | 13, rca);
    if (status & (1<<8)) return;
    if (++counter > 10000000) {
      printf("[ERROR] SD card timeout\n");
      return;
    }
  }
}

unsigned sdread(unsigned* dst, unsigned sector, unsigned sector_count) {
  if (sector_count == 0) return 0;
  sd_wait_ready();
  command(SDIO_R1 | SDIO_MEM | 18, sector);
  unsigned fifo = SDIO_FIFO;
  for (unsigned b = 0; b < sector_count - 1; ++b) {
    if (SDCARD_REGS->cmd & SDIO_ERR) {
      command(SDIO_R1b | 12, 0);
      return b;
    }
    SDCARD_REGS->cmd = SDIO_MEM | fifo;
    dst = receive_sector(dst, fifo ? &SDCARD_REGS->fifo0_le : &SDCARD_REGS->fifo1_le);
    fifo ^= SDIO_FIFO;
    while (SDCARD_REGS->cmd & SDIO_BUSY);
  }
  int err = SDCARD_REGS->cmd & SDIO_ERR;
  command(SDIO_R1b | 12, 0);
  if (err) return sector_count - 1;
  receive_sector(dst, fifo ? &SDCARD_REGS->fifo0_le : &SDCARD_REGS->fifo1_le);
  return sector_count;
}

unsigned sdwrite(const unsigned* src, unsigned sector, unsigned sector_count) {
  if (sector_count == 0) return 0;
  sd_wait_ready();
  src = send_sector(src, &SDCARD_REGS->fifo0_le);
  SDCARD_REGS->data = sector;
  SDCARD_REGS->cmd = SDIO_CMD | SDIO_ERR | SDIO_R1 | SDIO_WRITE | SDIO_ACK | SDIO_MEM | 25;
  unsigned fifo = 0;
  for (unsigned b = 0; b < sector_count - 1; ++b) {
    fifo ^= SDIO_FIFO;
    src = send_sector(src, fifo ? &SDCARD_REGS->fifo1_le : &SDCARD_REGS->fifo0_le);
    while (SDCARD_REGS->cmd & SDIO_BUSY);
    if (SDCARD_REGS->cmd & SDIO_ERR) {
      command(SDIO_R1b | 12, 0);
      return b;
    }
    SDCARD_REGS->cmd = SDIO_WRITE | SDIO_MEM | fifo;
  }
  while (SDCARD_REGS->cmd & SDIO_BUSY);
  int err = SDCARD_REGS->cmd & SDIO_ERR;
  command(SDIO_R1b | 12, 0);
  return err ? sector_count - 1 : sector_count;
}

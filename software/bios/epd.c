// Code for black-white E2417 E-Ink display.
// Docs: https://docs.pervasivedisplays.com/knowledge/Hardware/epd-usage/Screens/Wide_Small/update-procedure.html

#include <endeavour2/raw/defs.h>

#include "bios_internal.h"

static void epd_write_cmd(unsigned v) {
  while (EPD_REGS->stat & (EPD_BUSY|EPD_SPI_BUSY));
  EPD_REGS->data = v;
  EPD_REGS->cmd = 0x203;
  while (EPD_REGS->stat & EPD_SPI_BUSY);
}

static void epd_write_data(unsigned v) {
  EPD_REGS->data = v;
  EPD_REGS->cmd = 0x303;
  while (EPD_REGS->stat & EPD_SPI_BUSY);
}

static unsigned epd_read_data() {
  EPD_REGS->cmd = 0x13f;   // read data, slow
  while (EPD_REGS->stat & EPD_SPI_BUSY);
  return EPD_REGS->data;
}

static int epd_reset() {
  EPD_REGS->stat = EPD_NRESET;
  wait(10000 * 5);  // 5ms
  EPD_REGS->stat = 0;
  wait(10000 * 10);  // 10ms
  EPD_REGS->stat = EPD_NRESET;
  wait(10000 * 20);  // 20ms
  return EPD_REGS->stat & EPD_SPI_BUSY;
}

static int epd_psr = -1;
char* epd_image = (void*)(RAM_BASE + 0x1b8000);
static char* epd_old_image = (void*)(RAM_BASE + 0x1bc000);

int epd_is_initialized() { return epd_psr >= 0; }

int epd_init() {
  if (epd_reset() != 0) {
    epd_psr = -1;
    return -1;
  }
  epd_write_cmd(0xa2);
  epd_read_data();
  unsigned bank = epd_read_data();
  unsigned skip_count = bank == 0xa5 ? 0xB1E : 0x171E;
  for (int i = 0; i < skip_count; ++i) epd_read_data();
  unsigned psr0 = epd_read_data();
  unsigned psr1 = epd_read_data();
  epd_psr = (psr1 << 8) | psr0;
  while (EPD_REGS->stat & (EPD_BUSY|EPD_SPI_BUSY));
  EPD_REGS->stat = 0;
  return 0;
}

int epd_clear() {
  unsigned* data = (unsigned*)epd_image;
  for (int i = 0; i < EPD_IMAGE_SIZE / 4; ++i) data[i] = 0;
  return epd_full_update();
}

int epd_full_update() {
  if (epd_psr < 0) epd_init();
  if (epd_psr < 0 || epd_reset() != 0) {
    epd_psr = -1;
    return -1;
  }

  // Initialize COG
  epd_write_cmd(0xe5);
  epd_write_data(25); // tsset
  epd_write_cmd(0xe0);
  epd_write_data(2);
  epd_write_cmd(0x0);
  epd_write_data(epd_psr & 0xff);
  epd_write_data(epd_psr >> 8);

  // Send image
  epd_write_cmd(0x10);
  for (int i = 0; i < 300 * 50; ++i) epd_write_data(epd_old_image[i] = epd_image[i]);
  epd_write_cmd(0x13);
  for (int i = 0; i < 300 * 50; ++i) epd_write_data(0x0);

  // Update COG
  epd_write_cmd(0x4);
  epd_write_cmd(0x12);

  // Stop DC/DC and reset
  epd_write_cmd(0x2);
  while (EPD_REGS->stat & (EPD_BUSY|EPD_SPI_BUSY));
  EPD_REGS->stat = 0;
  return 0;
}

int epd_fast_update() {
  if (epd_psr < 0) {
    return epd_full_update();
  }
  if (epd_reset() != 0) {
    epd_psr = -1;
    return -1;
  }

  // Initialize COG
  epd_write_cmd(0xe5);
  epd_write_data(25 + 0x40); // tsset
  epd_write_cmd(0xe0);
  epd_write_data(2);
  epd_write_cmd(0x0);
  epd_write_data((epd_psr & 0xff) | 0x10);
  epd_write_data((epd_psr >> 8) | 0x2);
  epd_write_cmd(0x50);
  epd_write_data(0x7);

  // Send image
  epd_write_cmd(0x50);
  epd_write_data(0x27);
  epd_write_cmd(0x10);
  for (int i = 0; i < 300 * 50; ++i) epd_write_data(epd_old_image[i]);
  epd_write_cmd(0x13);
  for (int i = 0; i < 300 * 50; ++i) {
    epd_write_data(epd_old_image[i] = epd_image[i]);
  }
  epd_write_cmd(0x50);
  epd_write_data(0x7);

  // Update COG
  epd_write_cmd(0x4);
  epd_write_cmd(0x12);

  // Stop DC/DC and reset
  epd_write_cmd(0x2);
  while (EPD_REGS->stat & (EPD_BUSY|EPD_SPI_BUSY));
  EPD_REGS->stat = 0;
  return 0;
}

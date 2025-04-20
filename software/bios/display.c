#include <endeavour2/defs.h>

#include "bios_internal.h"

#define DEFAULT_TEXT_STYLE (TEXT_BG(0) | TEXT_FG(15))

static const unsigned logo[] =  {
  0x000e0000, 0x1f1f0e1f, 0x181f191f, 0x181f181f,
  0x00000000, 0x00c00080, 0xdfe099e1, 0x7fe0cff0,
  0x00000000, 0x03000000, 0xff00e7e0, 0xff03ff01,
  0x30080000, 0xc03cf008, 0x00fc807c, 0x00f800fc,
  0x1c1f181f, 0x0f0f0e0f, 0x07040700, 0x0f080704,
  0x7fc07fe0, 0xff00ff00, 0xff7cff00, 0xff00ff40,
  0xfe01fe03, 0xff00ff00, 0xff78ff0c, 0xff00ff40,
  0x18e818e0, 0xf000f800, 0xf010f010, 0xf818f818,
  0x1f101f18, 0x3f3e7f70, 0x1f0f3f1f, 0x07000f03,
  0xc33cc33c, 0xff00e31c, 0xfff8ffc0, 0xfffcfff8,
  0xe31ce11e, 0xff01c738, 0xff1fff03, 0x3fc0ff1f,
  0xff07fc0c, 0xffc7fe02, 0xfec0fee0, 0xe000f800,
  0x00000100, 0x00000000, 0x00000000, 0x00000000,
  0x3e0ffe0d, 0x03000700, 0x00000000, 0x00000000,
  0x18e01ee0, 0xe000f000, 0x00000000, 0x00000000,
  0x00000000, 0x00000000, 0x00000000, 0x00000000};

static void register_logo() {
  VIDEO_REGS->regIndex = VIDEO_COLORMAP(124);
  VIDEO_REGS->regValue = 0;
  VIDEO_REGS->regIndex = VIDEO_COLORMAP(125);
  VIDEO_REGS->regValue = COLORMAP_TEXT_COLOR(242, 82, 0) | COLORMAP_TEXT_ALPHA(64);
  VIDEO_REGS->regIndex = VIDEO_COLORMAP(126);
  VIDEO_REGS->regValue = COLORMAP_TEXT_COLOR(55, 22, 11) | COLORMAP_TEXT_ALPHA(64);
  VIDEO_REGS->regIndex = VIDEO_COLORMAP(127);
  VIDEO_REGS->regValue = COLORMAP_TEXT_COLOR(255, 255, 255) | COLORMAP_TEXT_ALPHA(64);

  for (int i = 0; i < 64; ++i) {
    VIDEO_REGS->regIndex = (0x1f0 << 2) + i;
    VIDEO_REGS->regValue = logo[i];
  }
}

static unsigned* cursor_offset(unsigned* ptr, int line, int column) {
  unsigned long c = (unsigned long)ptr;
  unsigned long frame = c & ~(TEXT_BUFFER_SIZE-1);
  c = frame | ((c + line * TEXT_LINE_SIZE + column * 4) & (TEXT_BUFFER_SIZE-1));
  return (unsigned*)c;
}

void show_logo(unsigned* base_ptr, int line, int column) {
  if (!cursor_ptr) return;
  const int base = 0xf0;
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 4; ++x) {
      unsigned* ptr = cursor_offset(base_ptr, line + y, column + x);
      *ptr = (1<<31) | TEXT_BG(124) | TEXT_FG(126) | ((base + y*8 + 4 + x) << 8) | (base + y*8 + x);
    }
  }
}

extern const unsigned charmap[94*4];
extern unsigned text_style;

void init_display() {
  set_video_mode(VIDEO_MODE_640x480, 0);

  text_style = DEFAULT_TEXT_STYLE;
  VIDEO_REGS->regIndex = VIDEO_COLORMAP(0);
  VIDEO_REGS->regValue = COLORMAP_TEXT_COLOR(0, 0, 0) | COLORMAP_TEXT_ALPHA(0);  // black background
  VIDEO_REGS->regIndex = VIDEO_COLORMAP(15);
  VIDEO_REGS->regValue = COLORMAP_TEXT_COLOR(255, 255, 255) | COLORMAP_TEXT_ALPHA(64); // white text
  const unsigned* charmap_ptr = charmap;
  for (int i = 32 * 4; i < 127 * 4; ++i) {
    VIDEO_REGS->regIndex = i;
    VIDEO_REGS->regValue = *charmap_ptr++;
  }
  unsigned* text_buf = (unsigned*)(RAM_BASE + 0x1C0000);
  cursor_ptr = text_buf;
  for (int i = 0; i < TEXT_BUFFER_SIZE / 4; ++i) {
    text_buf[i] = DEFAULT_TEXT_STYLE | ' ';
  }
  VIDEO_REGS->graphicAddr = (void*)(RAM_BASE + 0x200000);
  VIDEO_REGS->textAddr = text_buf;
  VIDEO_REGS->textOffset = 0;
  register_logo();
  VIDEO_REGS->cfg = VIDEO_TEXT_ON | VIDEO_FONT_HEIGHT(16);
}

void display_putchar(unsigned c) {
  if (!cursor_ptr) return;
  unsigned long frame = (unsigned long)cursor_ptr & ~(TEXT_BUFFER_SIZE-1);
  if (c == '\n') {
    cursor_ptr = (void*)(((unsigned long)cursor_ptr & ~(TEXT_LINE_SIZE-1)) + TEXT_LINE_SIZE);
  } else if (c == '\b') {
    if (((unsigned long)cursor_ptr & (TEXT_LINE_SIZE-1)) != 0) *--cursor_ptr = text_style | ' ';
    return;
  } else {
    *cursor_ptr++ = text_style | c;
  }
  cursor_ptr = (void*)(frame | ((unsigned long)cursor_ptr & (TEXT_BUFFER_SIZE-1)));
  if (((unsigned long)cursor_ptr & (TEXT_LINE_SIZE-1)) == 0) {
    unsigned* taddr = VIDEO_REGS->textAddr;
    unsigned required_size = ((unsigned long)cursor_ptr - (unsigned long)taddr + TEXT_LINE_SIZE) & (TEXT_BUFFER_SIZE-1);
    unsigned available_size = (VIDEO_REGS->mode.vResolution >> 4) * TEXT_LINE_SIZE;
    if (required_size > available_size) {
      VIDEO_REGS->textAddr = (void*)(frame + (((unsigned long)taddr + required_size - available_size) & (TEXT_BUFFER_SIZE - 1)));
    }
    for (int i = 0; i < TEXT_LINE_SIZE/4; ++i) cursor_ptr[i] = DEFAULT_TEXT_STYLE | ' ';
    unsigned* next_line = (void*)(frame + (((unsigned long)cursor_ptr + TEXT_LINE_SIZE) & (TEXT_BUFFER_SIZE - 1)));
    for (int i = 0; i < TEXT_LINE_SIZE/4; ++i) next_line[i] = DEFAULT_TEXT_STYLE | ' ';
  }
}

static const char SI5351A_p10_17[] = {0x10, 0x4c, 0x8c, 0x8c, 0x8c, 0x8c, 0x8c, 0x8c, 0x8c};

void set_dvi_frequency(unsigned freq) {
  if (freq < 1000000 || freq > 200000000) {
    i2c_set_reg(I2C_ADDR_SI5351A, 3, -1);
    i2c_set_reg(I2C_ADDR_TFP410, 8, 0);
    return;
  }

  i2c_set_reg(I2C_ADDR_TFP410, 8, 0x3);

  unsigned ms0_div = 4;
  while (freq * ms0_div < 600000000) ms0_div += 2;
  unsigned ms0_p1 = ms0_div * 128 - 512;

  unsigned fvco = freq * ms0_div;
  unsigned fsource = 25000000;

  unsigned msna_a = fvco / fsource;
  unsigned msna_b = (fvco % fsource);
  unsigned msna_c = fsource;
  for (int i = 2; i <= 19; ++i) {
    while (msna_b % i == 0 && msna_c % i == 0) {
      msna_b /= i; msna_c /= i;
    }
  }
  while (msna_c >= (1<<20)) {
    msna_b /= 2; msna_c /= 2;
  }
  unsigned bc = (msna_b << 7) / msna_c;
  unsigned msna_p3 = msna_c;
  unsigned msna_p2 = (msna_b << 7) - msna_c * bc;
  unsigned msna_p1 = (msna_a << 7) + bc - 512;

  /*printf("target freq = %u\n", freq);
  printf("freq = 25 * (%u + %u / %u) / %u\n", msna_a, msna_b, msna_c, ms0_div);
  printf("msna_p1 = %u\n", msna_p1);
  printf("msna_p2 = %u\n", msna_p2);
  printf("msna_p3 = %u\n", msna_p3);
  printf("ms0_p1  = %u\n", ms0_p1);*/

  char msna_conf[9];
  msna_conf[0] = 0x1a;
  msna_conf[1] /* 1a */ = (msna_p3 >> 8);
  msna_conf[2] /* 1b */ = msna_p3;
  msna_conf[3] /* 1c */ = (msna_p1 >> 16);
  msna_conf[4] /* 1d */ = (msna_p1 >> 8);
  msna_conf[5] /* 1e */ = msna_p1;
  msna_conf[6] /* 1f */ = ((msna_p3 >> 16) << 4) | (msna_p2 >> 16);
  msna_conf[7] /* 20 */ = (msna_p2 >> 8);
  msna_conf[8] /* 21 */ = msna_p2;

  char ms0_conf[9];
  ms0_conf[0] = 0x2a;
  ms0_conf[1] /* 2a */ = 0;
  ms0_conf[2] /* 2b */ = 1;
  ms0_conf[3] /* 2c */ = (ms0_div == 4 ? 0xC : 0) | (ms0_p1 >> 16);
  ms0_conf[4] /* 2d */ = (ms0_p1 >> 8);
  ms0_conf[5] /* 2e */ = ms0_p1;
  ms0_conf[6] /* 2f */ = 0;
  ms0_conf[7] /* 30 */ = 0;
  ms0_conf[8] /* 31 */ = 0;

  i2c_write(I2C_ADDR_SI5351A, sizeof(SI5351A_p10_17), SI5351A_p10_17);
  i2c_write(I2C_ADDR_SI5351A, sizeof(msna_conf), msna_conf);
  i2c_write(I2C_ADDR_SI5351A, sizeof(ms0_conf), ms0_conf);

  char buf[8] = {0x5a, 0, 0, 0, 0, 0, 0, 0};
  i2c_write(I2C_ADDR_SI5351A, 3, buf);
  buf[0] = 0x95;
  i2c_write(I2C_ADDR_SI5351A, 8, buf);
  buf[0] = 0xa2;
  i2c_write(I2C_ADDR_SI5351A, 4, buf);

  i2c_set_reg(I2C_ADDR_SI5351A, 0x03, 0x00);
  i2c_set_reg(I2C_ADDR_SI5351A, 0xb7, 0xd2);
  i2c_set_reg(I2C_ADDR_SI5351A, 0xb1, 0xac);

  /*wait(10000000);
  printf("FREQ %u\n", BOARD_REGS->dvi_pixel_frequency);*/
}

// Command to get current modeline: xrandr --prop | edid-decode

static struct VideoMode mode_640x480_60   = { 25175000,     640,  656,  752,  800,  480,  490,  492,  525};
static struct VideoMode mode_800x600_60   = { 36000000,     800,  824,  896, 1024,  600,  601,  603,  625};
static struct VideoMode mode_1024x768_60  = { 65000000,    1024, 1048, 1184, 1344,  768,  771,  777,  806};
static struct VideoMode mode_1280x720_60  = { 74250000,    1280, 1720, 1760, 1980,  720,  725,  730,  750};
static struct VideoMode mode_1920x1080_25 = { 74250000,    1920, 2448, 2492, 2640, 1080, 1084, 1089, 1125};
static struct VideoMode mode_1920x1080_50 = {148500000,    1920, 2448, 2492, 2640, 1080, 1084, 1089, 1125};

// struct VideoMode mode_1920x1080 = {148500000, 1920, 2008, 2052, 2200, 1080, 1084, 1089, 1125};
// struct VideoMode mode_1920x1080 = {152840000, 1920, 2000, 2054, 2250, 1080, 1086, 1094, 1132};

int set_video_mode(enum VideoModeId modeid, const struct VideoMode* mode) {
  switch (modeid) {
    case VIDEO_MODE_CUSTOM: break;
    case VIDEO_MODE_640x480: mode = &mode_640x480_60; break;
    case VIDEO_MODE_800x600: mode = &mode_800x600_60; break;
    case VIDEO_MODE_1024x768: mode = &mode_1024x768_60; break;
    case VIDEO_MODE_1280x720: mode = &mode_1280x720_60; break;
    case VIDEO_MODE_1920x1080: mode = &mode_1920x1080_50; break;
    case VIDEO_MODE_1920x1080_25: mode = &mode_1920x1080_25; break;
    default: return -1;
  }
  if (mode == 0) {
    set_dvi_frequency(0);
    return 0;
  }
  set_dvi_frequency(mode->clock & ((~0) >> 2));
  const unsigned* src = (unsigned*)mode;
  volatile unsigned* dst = (unsigned*)&VIDEO_REGS->mode;
  for (int i = 0; i < sizeof(struct VideoMode)/4; ++i) dst[i] = src[i];
  return 0;
}

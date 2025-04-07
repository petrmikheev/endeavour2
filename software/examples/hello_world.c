#include <endeavour2/bios.h>

void hello_line(const char* str) {
  printf("\t\t");
  unsigned old_style = bios_get_text_style();
  bios_set_text_style(TEXT_BG(126) | TEXT_FG(127));
  printf("\t");
  printf(str);
  printf("\t\n");
  bios_set_text_style(old_style);
}

void register_char(int code, const char* data) {
  for (int i = 0; i < 4; ++i) {
    VIDEO_REGS->regIndex = VIDEO_CHARMAP(code, i);
    VIDEO_REGS->regValue = ((int*)data)[i];
  }
}

const char c1[] = {0x00, 0x07, 0x1f, 0x3f, 0x3f, 0x7f, 0x7f, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
const char c2[] = {0x00, 0xe0, 0xf8, 0xfc, 0xfc, 0xfe, 0xfe, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
const char c3[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x7f, 0x7f, 0x3f, 0x3f, 0x1f, 0x07, 0x00};
const char c4[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfe, 0xfe, 0xfc, 0xfc, 0xf8, 0xe0, 0x00};

static int i2c_wait() {
  int cmd;
  while ((cmd = I2C_REGS->cmd) < 0);
  return cmd & (I2C_CMD_DATA_ERR|I2C_CMD_ADDR_ERR);
}

int i2c_write(int addr, int size, const char* data) {
  int err = 0;
  I2C_REGS->cmd = I2C_CMD_WRITE | addr;
  I2C_REGS->counter = size;
  for (int i = 0; i < size; ++i) {
    err |= i2c_wait();
    I2C_REGS->data = *data++;
  }
  err |= i2c_wait();
  return err;
}

int i2c_read(int addr, int size, char* data) {
  int err = 0;
  I2C_REGS->cmd = I2C_CMD_READ | addr;
  I2C_REGS->counter = size;
  for (int i = 0; i < size; ++i) {
    err |= i2c_wait();
    *data++ = I2C_REGS->data;
  }
  return err;
}

static void i2c_set_reg(int addr, char reg, char value) {
  char buf[2] = {reg, value};
  i2c_write(addr, 2, buf);
}

int main() {
  VIDEO_REGS->regIndex = VIDEO_COLORMAP(126);
  VIDEO_REGS->regValue = COLORMAP_TEXT_COLOR(40, 40, 20) | COLORMAP_TEXT_ALPHA(48);
  VIDEO_REGS->regIndex = VIDEO_COLORMAP(127);
  VIDEO_REGS->regValue = COLORMAP_TEXT_COLOR(0, 255, 0) | COLORMAP_TEXT_ALPHA(64);

  register_char(0x1fc, c1);
  register_char(0x1fd, c2);
  register_char(0x1fe, c3);
  register_char(0x1ff, c4);
  printf("\n\n\n");
  hello_line("\t\t\t\t\t\t\t\t\t\t\t\t\t\t");
  hello_line("  _   _\t  _ _\t\t __\t\t__\t\t _\t _ _ ");
  hello_line(" | | | | ___| | | ___\t\\ \\\t  / /__  _ __| | __| | |");
  hello_line(" | |_| |/ _ \\ | |/ _ \\\t\\ \\ /\\ / / _ \\| '__| |/ _` | |");
  hello_line(" |  _  |  __/ | | (_) |\t\\ V  V / (_) | |  | | (_| |_|");
  hello_line(" |_| |_|\\___|_|_|\\___( )\t\\_/\\_/ \\___/|_|  |_|\\__,_(_)");
  hello_line("\t\t\t\t\t |/\t\t\t\t\t\t\t\t ");
  hello_line("\t\t\t\t\t\t\t\t\t\t\t\t\t\t");
  unsigned* cursor = bios_get_cursor_ptr();
  unsigned* first_line = cursor - 8 * 256 + 8;
  unsigned* last_line  = cursor - 1 * 256 + 8;
  first_line[0]  = 0x1fc | TEXT_BG(0) | TEXT_FG(126);
  first_line[63] = 0x1fd | TEXT_BG(0) | TEXT_FG(126);
  last_line[0]   = 0x1fe | TEXT_BG(0) | TEXT_FG(126);
  last_line[63]  = 0x1ff | TEXT_BG(0) | TEXT_FG(126);
  printf("\n\n\n");
  return 0;
}

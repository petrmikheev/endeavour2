#include <endeavour2/bios.h>

void hello_line(const char* str) {
  printf("\t\t");
  unsigned old_style = bios_get_text_style();
  bios_set_text_style(TEXT_BG(122) | TEXT_FG(123));
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

int main() {
  VIDEO_REGS->regIndex = VIDEO_COLORMAP(122);
  VIDEO_REGS->regValue = COLORMAP_TEXT_COLOR(40, 40, 20) | COLORMAP_TEXT_ALPHA(48);
  VIDEO_REGS->regIndex = VIDEO_COLORMAP(123);
  VIDEO_REGS->regValue = COLORMAP_TEXT_COLOR(0, 255, 0) | COLORMAP_TEXT_ALPHA(64);

  register_char(0x1ec, c1);
  register_char(0x1ed, c2);
  register_char(0x1ee, c3);
  register_char(0x1ef, c4);
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
  unsigned* first_line = bios_cursor_offset(cursor, -8, 8);
  unsigned* last_line = bios_cursor_offset(cursor, -1, 8);
  first_line[0]  = 0x1ec | TEXT_BG(0) | TEXT_FG(122);
  first_line[63] = 0x1ed | TEXT_BG(0) | TEXT_FG(122);
  last_line[0]   = 0x1ee | TEXT_BG(0) | TEXT_FG(122);
  last_line[63]  = 0x1ef | TEXT_BG(0) | TEXT_FG(122);
  printf("\n\n\n");
  return 0;
}

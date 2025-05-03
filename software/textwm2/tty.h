#ifndef TEXTWM_TTY
#define TEXTWM_TTY

#include "textwm.h"

//#define IN_FIFO_SIZE 128
#define CSI_MAX_LEN 32

struct TTY {
  int fd;
  char* frame;
  int frame_start;
  int line, column, line_copy, column_copy;
  int width, height;
  int window_posx, window_posy, window_width, window_height;
  int workspace;
  unsigned ucode, ucount;
  unsigned style, style_copy;
  unsigned cdata_at_cursor;
  int scroll_from, scroll_to;
  int csi_len;
  char csi[CSI_MAX_LEN];
  char state;
  bool cursor_visible;
  bool cursor_blink;
  bool cursor_hidden;
  bool bold, bold_copy;
  bool vt100_graphics;
  //char in_fifo[IN_FIFO_SIZE];
  //int in_start, in_end;
};

#define TTY_COUNT 7

extern struct TTY ttys[TTY_COUNT];
extern int active_tty;

void tty_handler(int tty_id, unsigned char c);
void tty_set_active(int tty_id);

void hide_cursor(struct TTY *tty);
void show_cursor(struct TTY *tty);

void init_vt100_graphic_table();

#endif // TEXTWM_TTY

#ifndef TEXTWM_TTY
#define TEXTWM_TTY

#include "textwm.h"

//#define IN_FIFO_SIZE 128
#define CSI_MAX_LEN 32

struct TTY {
  int fd;
  char* frame;
  int frame_start;
  int line, column;
  int width, height;
  int window_posx, window_posy, window_width, window_height;
  int workspace;
  unsigned ucode, ucount;
  unsigned style;
  unsigned cdata_at_cursor;
  int csi_len;
  char csi[CSI_MAX_LEN];
  char state;
  bool cursor_visible;
  bool cursor_blink;
  bool bold;
  //char in_fifo[IN_FIFO_SIZE];
  //int in_start, in_end;
};

#define TTY_COUNT 7

extern struct TTY ttys[TTY_COUNT];
extern int active_tty;

void tty_handler(int tty_id, char c);
void tty_set_active(int tty_id);

void hide_cursor(struct TTY *tty);
void show_cursor(struct TTY *tty);

#endif // TEXTWM_TTY

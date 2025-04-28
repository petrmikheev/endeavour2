#include "tty.h"

#include <stdio.h>
#include <string.h>

#include <endeavour2/display.h>

#include "textwm.h"
#include "utf8.h"

struct TTY ttys[TTY_COUNT];

static unsigned* tty_cursor_ptr(struct TTY* tty) {
  return (unsigned*)(tty->frame + ((tty->frame_start + (tty->line * TEXT_LINE_SIZE) | (tty->column << 2)) & (TEXT_BUFFER_SIZE - 1)));
}

static void maybe_scroll(struct TTY *tty) {
  if (tty->column >= tty->width) {
    tty->column -= tty->width;
    tty->line++;
  }
  while (tty->line >= tty->height) {
    tty->line--;
    int v = tty->style | ' ';
    int* line = (int*)(tty->frame + ((tty->frame_start + (tty->height * TEXT_LINE_SIZE)) & (TEXT_BUFFER_SIZE - 1)));
    for (int i = 0; i < TEXT_LINE_SIZE / 4; ++i) line[i] = v;
    line = (int*)(tty->frame + ((tty->frame_start + (tty->height * TEXT_LINE_SIZE) + TEXT_LINE_SIZE) & (TEXT_BUFFER_SIZE - 1)));
    tty->frame_start = (tty->frame_start + TEXT_LINE_SIZE) & (TEXT_BUFFER_SIZE - 1);
    for (int i = 0; i < TEXT_LINE_SIZE / 4; ++i) line[i] = v;
    update_taddr(tty - ttys);
  }
}

static void tty_print(struct TTY *tty, unsigned ucode) {
  maybe_scroll(tty);
  int c = from_utf(ucode, tty->bold);
  if (c < 0) c = '?';
  *tty_cursor_ptr(tty) = tty->style | c;
  tty->column += 1;
}

static void tty_sgr(struct TTY *tty) {
  tty->style = DEFAULT_STYLE;
  tty->bold = false;
  int i = 0;
  const char* s = tty->csi;
  while (*s >= '0' && *s <= '9') {
    int c = 0;
    while (*s >= '0' && *s <= '9') c = c * 10 + *s++ - '0';
    if (*s == ';') s++;
    if (c == 0 || c == 10) { /* ignore */ }
    else if (c == 7) tty->style = TEXT_BG(15) | TEXT_FG(16);
    else if (c == 1 || c == 4) tty->bold = true;
    else if (c >= 30 && c <= 37) tty->style = (tty->style & 0xff00ffff) | TEXT_FG(c - 30 + 16);
    else if (c == 39) tty->style = (tty->style & 0xff00ffff) | TEXT_FG(31);
    else if (c >= 40 && c <= 47) tty->style = (tty->style & 0x00ffffff) | TEXT_BG(c - 40);
    else if (c == 49) tty->style = (tty->style & 0x00ffffff) | WINDOW_BG;
    else if (c >= 90 && c <= 97) tty->style = (tty->style & 0xff00ffff) | TEXT_FG(c - 90 + 24);
    else if (c >= 100 && c <= 107) tty->style = (tty->style & 0x00ffffff) | TEXT_BG(c - 100 + 8);
    else {
      printf("[textwm] SGR %d not implemented\n", c);
    }
  }
}

static void tty_csi(int tty_id) {
  struct TTY *tty = &ttys[tty_id];
  tty->csi[tty->csi_len] = 0;
  if (tty->csi_len == 0) goto err;
  //printf("[textwm] %d:%d CSI: %s\n", tty->line, tty->column, tty->csi);
  if (strcmp(tty->csi, "?1049h") == 0) {
    tty->frame = text_buffers + (tty_id + TTY_COUNT) * TEXT_BUFFER_SIZE;
    update_taddr(tty_id);
    return;
  }
  if (strcmp(tty->csi, "?1049l") == 0) {
    tty->frame = text_buffers + tty_id * TEXT_BUFFER_SIZE;
    update_taddr(tty_id);
    return;
  }
  if (tty->column >= tty->width) {
    tty->column -= tty->width;
    tty->line++;
  }
  int n = 1, m = 1;
  switch (tty->csi[tty->csi_len - 1]) {
    case 'F':
      tty->column = 0;
    case 'A':
      sscanf(tty->csi, "%d", &n);
      tty->line -= n;
      break;
    case 'E':
      tty->column = 0;
    case 'B':
      sscanf(tty->csi, "%d", &n);
      tty->line += n;
      break;
    case 'b': {
      sscanf(tty->csi, "%d", &n);
      m = tty->style | ' ';
      int cpos = (tty->frame_start + (tty->line << 10) | (tty->column << 2)) & (TEXT_BUFFER_SIZE - 1);
      int to = (cpos + (n<<2)) & (TEXT_BUFFER_SIZE - 1);
      for (int i = cpos; i != to; i = (i + 4) & (TEXT_BUFFER_SIZE - 1)) *(unsigned*)(tty->frame + i) = m;
      tty->column += n;
      break;
    }
    case 'P': {
      sscanf(tty->csi, "%d", &n);
      unsigned* cur = tty_cursor_ptr(tty);
      unsigned* line_end = (unsigned*)(((long)cur + TEXT_LINE_SIZE) & ~(TEXT_LINE_SIZE-1));
      if (cur + n > line_end) n = line_end - cur;
      for (unsigned* p = cur; p < line_end - n; ++p) *p = p[n];
      break;
    }
    case 'C':
      sscanf(tty->csi, "%d", &n);
      tty->column += n;
      break;
    case 'D':
      sscanf(tty->csi, "%d", &n);
      tty->column -= n;
      break;
    case 'G':
      sscanf(tty->csi, "%d", &n);
      tty->column = n - 1;
      break;
    case 'd':
      sscanf(tty->csi, "%d", &n);
      tty->line = n - 1;
      break;
    case 'f':
    case 'H':
      if (tty->csi[0] == ';')
        sscanf(tty->csi, "%d", &m);
      else
        sscanf(tty->csi, "%d;%d", &n, &m);
      tty->line = n - 1;
      tty->column = m - 1;
      break;
    case 'm':
      tty_sgr(tty);
      break;
    case 'J':
      n = 0;
      sscanf(tty->csi, "%d", &n);
      m = tty->style | ' ';
      int cpos = (tty->frame_start + (tty->line << 10) | (tty->column << 2)) & (TEXT_BUFFER_SIZE - 1);
      int frame_end = (tty->frame_start + (tty->height << 10)) & (TEXT_BUFFER_SIZE - 1);
      if (n == 0) {
        for (int i = cpos; i != frame_end; i = (i + 4) & (TEXT_BUFFER_SIZE - 1)) *(unsigned*)(tty->frame + i) = m;
      } else if (n == 1) {
        for (int i = tty->frame_start; i != cpos; i = (i + 4) & (TEXT_BUFFER_SIZE - 1)) *(unsigned*)(tty->frame + i) = m;
      } else if (n == 2) {
        for (int i = tty->frame_start; i != frame_end; i = (i + 4) & (TEXT_BUFFER_SIZE - 1)) *(unsigned*)(tty->frame + i) = m;
      }
      break;
    case 'K': {
      n = 0;
      sscanf(tty->csi, "%d", &n);
      int cpos = (tty->frame_start + (tty->line << 10) | (tty->column << 2)) & (TEXT_BUFFER_SIZE - 1);
      int from = cpos, to = cpos;
      if (n == 0) {
        to = (cpos + 0x400) & ~0x3ff;
      } else if (n == 1) {
        from = cpos & ~0x3ff;
      } else if (n == 2) {
        from = cpos & ~0x3ff;
        to = (cpos + 0x400) & ~0x3ff;
      }
      unsigned v = tty->style | ' ';
      for (char* p = tty->frame + from; p != tty->frame + to; p += 4) *(unsigned*)p = v;
      break;
    }
    case 'S':
      sscanf(tty->csi, "%d", &n);
      n = n << 10;
      tty->frame_start = (tty->frame_start - n) & (TEXT_BUFFER_SIZE - 1);
      update_taddr(tty_id);
      break;
    case 'T':
      sscanf(tty->csi, "%d", &n);
      n = n << 10;
      tty->frame_start = (tty->frame_start + n) & (TEXT_BUFFER_SIZE - 1);
      update_taddr(tty_id);
      break;
    default: goto err;
  }
  if (tty->line < 0) tty->line = 0;
  if (tty->column < 0) tty->column = 0;
  while (tty->column >= tty->width) {
    tty->column -= tty->width;
    tty->line++;
  }
  if (tty->line >= tty->height) tty->line = tty->height - 1;
  return;
err:
  printf("[textwm] Can't process CSI: %s\n", tty->csi);
}

void tty_handler(int tty_id, char c) {
  struct TTY *tty = &ttys[tty_id];
  //printf("TTY%d %d frame_start=%d cursor=%d frame_end=%d\n", tty_id, c, tty->frame_start, tty->cursor, tty->frame_end);
  if (tty->state == 0) {
    switch (c) {
      case 7: break; // beep
      case 0xc: tty->line = tty->column = 0; break;
      case 0x1b: tty->state = 'e'; break; // start escape
      case '\r': tty->column = 0; break;
      case '\n':
        tty->line++;
        maybe_scroll(tty);
        break;
      case '\t': tty->column = (tty->column + 7) & ~7; break;
      case '\b':
        if (tty->column > 0) {
          tty->column--;
        } else if (tty->line > 0) {
          tty->line--;
          tty->column = tty->width - 1;
        }
        break;
      default:
        if (c & 128) {
          tty->state = 'u'; // utf8
          if ((c & 32) == 0) {
            tty->ucount = 2;
            tty->ucode = c & 31;
          } else if ((c & 16) == 0) {
            tty->ucount = 3;
            tty->ucode = c & 15;
          } else {
            tty->ucount = 4;
            tty->ucode = c & 7;
          }
        } else {
          tty_print(tty, c);
        }
    }
  } else if (tty->state == 'e') {
    tty->csi_len = 0;
    switch (c) {
      case '[':
        tty->state = '[';  // CSI
        break;
      case '>':
      case '=':
        tty->state = 0;  // keypad mode, ignore
        break;
      case '(':
      case ')':
        tty->state = '('; // ignore next char
        break;
      default:
        printf("[textwm] Escape sequence \\e%c... (%d) not supported\n", c, c);
        tty->state = 0;
    }
  } else if (tty->state == '(') {
    tty->state = 0;
  } else if (tty->state == 'u') {
    tty->ucode = (tty->ucode << 6) | (c&0x3f);
    if (--tty->ucount == 1) {
      tty->state = 0;
      tty_print(tty, tty->ucode);
    }
  } else if (tty->state == '[') {
    tty->csi[tty->csi_len++] = c;
    if ((c >= 0x40 && c <= 0x7e) || tty->csi_len == CSI_MAX_LEN - 1) {
      tty->state = 0;
      tty_csi(tty_id);
    }
  } else {
    printf("[textwm] Invalid state %c\n", tty->state);
  }
}

int active_tty = 0;

void tty_set_active(int tty_id) {
  if (tty_id >= 0 && tty_id < TTY_COUNT && tty_id != active_tty) {
    printf("[textwm] active tty = %d\n", tty_id);
    active_tty = tty_id;
    update_taddr(active_tty);
  }
}

static unsigned swap_fg_bg(unsigned cdata) {
  unsigned bg = cdata >> 24;
  unsigned fg = (cdata >> 16) & 15;
  return TEXT_BG(fg) | TEXT_FG(bg + 16) | (cdata & 511);
}

void hide_cursor(struct TTY *tty) {
  if (!tty->cursor_visible) return;
  *tty_cursor_ptr(tty) = tty->cdata_at_cursor;
  tty->cursor_visible = false;
}

void show_cursor(struct TTY *tty) {
  if (tty->cursor_visible) return;
  unsigned* cursor = tty_cursor_ptr(tty);
  tty->cdata_at_cursor = *cursor;
  *cursor = swap_fg_bg(*cursor);
  tty->cursor_visible = true;
}

#include "tty.h"

#include <stdio.h>
#include <string.h>

#include <endeavour2/display.h>

#include "textwm.h"
#include "utf8.h"

struct TTY ttys[TTY_COUNT];

static unsigned* tty_line(struct TTY* tty, int line) {
  return (unsigned*)(tty->frame + ((tty->frame_start + line * TEXT_LINE_SIZE) & (TEXT_BUFFER_SIZE - 1)));
}

static void line_fill(unsigned* line, unsigned v) {
  for (int i = 0; i < TEXT_LINE_SIZE / 4; ++i) line[i] = v;
}

static void line_copy(unsigned* to, const unsigned* from) {
  for (int i = 0; i < TEXT_LINE_SIZE / 4; ++i) to[i] = from[i];
}

static unsigned* tty_cursor_ptr(struct TTY* tty) {
  return tty_line(tty, tty->line) + tty->column;
}

static void scroll_all(struct TTY *tty, int count) {
  tty->frame_start = (tty->frame_start + count * TEXT_LINE_SIZE) & (TEXT_BUFFER_SIZE - 1);
  update_taddr(tty - ttys);
}

static void scroll_region(struct TTY *tty, int from, int to, int count) {
  //printf("scroll from=%d to=%d count=%d\n", from, to, count);
  unsigned v = tty->style | ' ';
  if (count > 0) {
    if (from == 0 && to == tty->height)
      scroll_all(tty, count);
    else {
      for (int i = to - 1; i >= from + count; i--)
        line_copy(tty_line(tty, i), tty_line(tty, i - count));
    }
    for (int i = from; i < from + count; i++)
      line_fill(tty_line(tty, i), v);
  } else {
    count = -count;
    if (from == 0 && to == tty->height)
      scroll_all(tty, -count);
    else {
      for (int i = from; i < to - count; i++)
        line_copy(tty_line(tty, i), tty_line(tty, i + count));
    }
    for (int i = to - count; i < to; i++)
      line_fill(tty_line(tty, i), v);
  }
}

static void maybe_scroll(struct TTY *tty) {
  if (tty->column >= tty->width) {
    tty->column = 0;
    tty->line++;
    if (tty->line == tty->scroll_to && tty->line < tty->height) {
      scroll_region(tty, tty->scroll_from, tty->scroll_to, -1);
      tty->line--;
    }
  }
  while (tty->line >= tty->height) {
    tty->line--;
    unsigned v = tty->style | ' ';
    line_fill(tty_line(tty, tty->height), v);
    line_fill(tty_line(tty, tty->height + 1), v);
    tty->frame_start = (tty->frame_start + TEXT_LINE_SIZE) & (TEXT_BUFFER_SIZE - 1);
    update_taddr(tty - ttys);
  }
}

static unsigned from_vt100_table[0x78 - 0x61 + 1];

void init_vt100_graphic_table() {
  for (int i = 0; i <= 0x78 - 0x61; ++i) from_vt100_table[i] = i + 0x61;
  from_vt100_table[0x61 - 0x61] = from_utf(0x2592, 0);
  from_vt100_table[0x6a - 0x61] = from_utf(0x2518, 0);
  from_vt100_table[0x6b - 0x61] = from_utf(0x2510, 0);
  from_vt100_table[0x6c - 0x61] = from_utf(0x250c, 0);
  from_vt100_table[0x6d - 0x61] = from_utf(0x2514, 0);
  from_vt100_table[0x6e - 0x61] = from_utf(0x253c, 0);
  from_vt100_table[0x71 - 0x61] = from_utf(0x2500, 0);
  from_vt100_table[0x74 - 0x61] = from_utf(0x251c, 0);
  from_vt100_table[0x75 - 0x61] = from_utf(0x2524, 0);
  from_vt100_table[0x76 - 0x61] = from_utf(0x2534, 0);
  from_vt100_table[0x77 - 0x61] = from_utf(0x252c, 0);
  from_vt100_table[0x78 - 0x61] = from_utf(0x2502, 0);
}

static unsigned from_vt100(unsigned v) {
  if (v < 0x61 || v > 0x78)
    return v;
  else
    return from_vt100_table[v - 0x61];
}

static void tty_print(struct TTY *tty, unsigned ucode) {
  if (ucode < 32) return;
  maybe_scroll(tty);
  int c;
  if (tty->vt100_graphics)
    c = from_vt100(ucode);
  else
    c = from_utf(ucode, tty->bold);
  if (c < 0) c = '?';
  *tty_cursor_ptr(tty) = tty->style | c;
  tty->column += 1;
}

static void tty_sgr(struct TTY *tty) {
  int i = 0;
  const char* s = tty->csi;
  if (!(*s >= '0' && *s <= '9')) {
    tty->style = DEFAULT_STYLE;
    tty->bold = false;
    return;
  }
  do {
    int c = 0;
    while (*s >= '0' && *s <= '9') c = c * 10 + *s++ - '0';
    if (*s == ';') s++;
    if (c == 0) {
      tty->style = DEFAULT_STYLE;
      tty->bold = false;
    }
    else if (c >= 10 && c <= 12) { /*tty->vt100_graphics = c == 11;*/ }
    else if (c == 7) tty->style = TEXT_BG(15) | TEXT_FG(16);
    else if (c == 1 || c == 4) tty->bold = true;
    else if (c >= 30 && c <= 37) tty->style = (tty->style & 0xff00ffff) | TEXT_FG(c - 30 + 16);
    else if (c == 39) tty->style = (tty->style & 0xff00ffff) | TEXT_FG(31);
    else if (c >= 40 && c <= 47) tty->style = (tty->style & 0x00ffffff) | TEXT_BG(c - 40);
    else if (c == 49) tty->style = (tty->style & 0x00ffffff) | TEXT_BG(WINDOW_BG);
    else if (c >= 90 && c <= 97) tty->style = (tty->style & 0xff00ffff) | TEXT_FG(c - 90 + 24);
    else if (c >= 100 && c <= 107) tty->style = (tty->style & 0x00ffffff) | TEXT_BG(c - 100 + 8);
    else {
      printf("[textwm] SGR %d not implemented\n", c);
    }
  } while (*s >= '0' && *s <= '9');
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
  int scroll_from = tty->scroll_from <= tty->line ? tty->scroll_from : 0;
  int scroll_to = tty->scroll_to > tty->line ? tty->scroll_to : tty->height;
  if (scroll_to > tty->height) scroll_to = tty->height;
  int n = 1, m = 1;
  unsigned csi_code = tty->csi[tty->csi_len - 1];
  switch (csi_code) {
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
    case 'X': {
      sscanf(tty->csi, "%d", &n);
      m = tty->style | ' ';
      int cpos = (tty->frame_start + (tty->line << 10) | (tty->column << 2)) & (TEXT_BUFFER_SIZE - 1);
      int to = (cpos + (n<<2)) & (TEXT_BUFFER_SIZE - 1);
      for (int i = cpos; i != to; i = (i + 4) & (TEXT_BUFFER_SIZE - 1)) *(unsigned*)(tty->frame + i) = m;
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
    case '@': {
      sscanf(tty->csi, "%d", &n);
      unsigned* cur = tty_cursor_ptr(tty);
      unsigned* line_end = (unsigned*)(((long)cur + TEXT_LINE_SIZE) & ~(TEXT_LINE_SIZE-1));
      if (cur + n > line_end) n = line_end - cur;
      for (unsigned* p = line_end - n - 1; p >= cur; --p) p[n] = p[0];
      m = tty->style | ' ';
      while (n-- > 0) *cur++ = m;
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
    case 'L':
      sscanf(tty->csi, "%d", &n);
      scroll_region(tty, tty->line, scroll_to, n);
      return;
    case 'M':
      sscanf(tty->csi, "%d", &n);
      scroll_region(tty, tty->line, scroll_to, -n);
      return;
    case 'S':
      sscanf(tty->csi, "%d", &n);
      scroll_all(tty, -n);
      return;
    case 'T':
      sscanf(tty->csi, "%d", &n);
      scroll_all(tty, n);
      return;
    case 'r':
      tty->scroll_from = 1;
      tty->scroll_to = tty->height;
      sscanf(tty->csi, "%u;%u", &tty->scroll_from, &tty->scroll_to);
      tty->scroll_from--;
      if (tty->scroll_to <= 0) tty->scroll_to = tty->height;
      tty->column = 0;
      tty->line = 0;
      return;
    case 's':
      tty->column_copy = tty->column;
      tty->line_copy = tty->line;
      return;
    case 'u':
      tty->column = tty->column_copy;
      tty->line = tty->line_copy;
      return;
    default:
      if (strcmp(tty->csi, "?25l") == 0) tty->cursor_hidden = true;
      else if (strcmp(tty->csi, "?25h") == 0) tty->cursor_hidden = false;
      else if (strcmp(tty->csi, "?0c") == 0 || strcmp(tty->csi, "?1c") == 0) { /* status request; ignore */ }
      else goto err;
  }
  if (tty->line < scroll_from) tty->line = scroll_from;
  if (tty->column < 0) tty->column = 0;
  while (tty->column >= tty->width) {
    tty->column -= tty->width;
    tty->line++;
  }
  if (tty->line >= scroll_to) tty->line = scroll_to - 1;
  return;
err:
  // ?2004h / ?2004l  - bracket paste mode
/*[textwm] Can't process CSI: 4l     reset mode
[textwm] Can't process CSI: ?7h    Set autowrap on (default)
[textwm] Can't process CSI: ?1c
[textwm] Can't process CSI: ?1000h mouse
[textwm] Can't process CSI: ?0c
[textwm] Can't process CSI: ?1000l
*/
  printf("[textwm] Can't process CSI: %s\n", tty->csi);
}

void tty_handler(int tty_id, unsigned char c) {
  struct TTY *tty = &ttys[tty_id];
  //printf("(%d) %c\n", c, c);
  if (tty->state == 0) {
    switch (c) {
      case 7: break; // beep
      case 0x1b: tty->state = 'e'; break; // start escape
      case '\r': tty->column = 0; break;
      case '\n':
      case 0xb:
      case 0xc:
        tty->column = tty->width;
        maybe_scroll(tty);
        break;
      case 0xe: tty->vt100_graphics = true; break;
      case 0xf: tty->vt100_graphics = false; break;
      case '\t': tty->column = (tty->column + 8) & ~7; break;
      case '\b':
        if (tty->column > 0) {
          tty->column--;
        }/* else if (tty->line > 0) {
          tty->line--;
          tty->column = tty->width - 1;
        }*/
        break;
      default:
        if (c & 128) {
          printf("ucode\n");
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
        } else
          tty_print(tty, c);
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
        tty->state = '('; // set G0 character set
        break;
      case ')':
        tty->state = ')'; // set G1 character set
        break;
      case 'M': {
        int scroll_from = tty->scroll_from <= tty->line ? tty->scroll_from : 0;
        if (tty->line > tty->scroll_from)
          tty->line--;
        else
          scroll_region(tty, tty->scroll_from, tty->scroll_to, 1);
        tty->state = 0;
        break;
      }
      case '7':
        tty->column_copy = tty->column;
        tty->line_copy = tty->line;
        tty->style_copy = tty->style;
        tty->bold_copy = tty->bold;
        tty->state = 0;
        break;
      case '8':
        tty->column = tty->column_copy;
        tty->line = tty->line_copy;
        tty->style = tty->style_copy;
        tty->bold = tty->bold_copy;
        tty->state = 0;
        break;
      case 0x18:
      case 0x1a:
        tty->state = 0;
        break;
      default:
        printf("[textwm] Escape sequence \\e%c (%d) not supported\n", c, c);
        tty->state = 0;
    }
  } else if (tty->state == '(') {
    //tty->vt100_graphics = c == '0';
    tty->state = 0;
  } else if (tty->state == ')') {
    tty->state = 0;
  } else if (tty->state == 'u') {
    tty->ucode = (tty->ucode << 6) | (c&0x3f);
    if (--tty->ucount == 1) {
      if (tty->ucode == 0x9b) {
        tty->csi_len = 0;
        tty->state = '[';
        return;
      }
      tty->state = 0;
      tty_print(tty, tty->ucode);
    }
  } else if (tty->state == '[') {
    if (c == 0x18 || c == 0x1a) {
      tty->state = 0;
      return;
    }
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

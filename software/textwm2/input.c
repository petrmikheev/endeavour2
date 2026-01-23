#include "input.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/input.h>

#include "tty.h"
#include "textwm.h"

int open_input() {
  char path[64];
  unsigned long ev_bits, key_bits;
  for (int i = 0; i < 4; ++i) {
    snprintf(path, 64, "/dev/input/event%d", i);
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) continue;
    ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), &ev_bits);
    if ((ev_bits >> EV_KEY) & 1) {
      ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), &key_bits);
      if ((key_bits >> KEY_A) & 1) {
        printf("[textwm] Using keyboard %s\n", path);
        return fd;
      }
    }
    close(fd);
  }
  return -1;
}

// `input_event` from <linux/input.h> has size 24 and doesn't match `input_event` in linux kernel (that has size 16).
// I haven't managed to configure everything correctly, so using a custom struct.
struct InputEvent {
  unsigned t1, t2;
  unsigned short type;
  unsigned short code;
  unsigned value;
};

#define EVENT_BUF_SIZE 32

static struct InputEvent in_events[EVENT_BUF_SIZE];
static int from = 0, to = 0;

static struct InputEvent* next_event(int fd) {
  if (from == to) {
    int rsize = read(fd, in_events, sizeof(struct InputEvent) * EVENT_BUF_SIZE);
    if (rsize == -1) return 0;
    if (rsize < 0 || (rsize&15) != 0) {
      printf("[textwm] Input error. rsize=%d\n", rsize);
      return 0;
    }
    from = 0;
    to = rsize >> 4;
  }
  return from < to ? &in_events[from++] : 0;
}

int shift = 0;
int alt = 0;
int ctrl = 0;
int caps = 0;
int super = 0;

char* with_shift = "\0\e!@#$%^&*()_+\x7f\tQWERTYUIOP{}\r\0ASDFGHJKL:\"~\0|ZXCVBNM<>?";
char* without_shift = "\0\e1234567890-=\x7f\tqwertyuiop[]\r\0asdfghjkl;'`\0\\zxcvbnm,./";

static int escape(char* buf, int p, const char* escape) {
  while (*escape) buf[p++] = *escape++;
  return p;
}

int parse_input_events(int fd, char* buf, int max_size) {
  int res = 0;
  while (res < max_size - 5) {
    struct InputEvent* ev = next_event(fd);
    if (!ev) break;
    if (ev->type != EV_KEY) continue;
    if (ev->code == 42 || ev->code == 54) {
      shift = ev->value;
      continue;
    }
    if (ev->code == 56 || ev->code == 100) {
      alt = ev->value;
      continue;
    }
    if (ev->code == 29 || ev->code == 97) {
      ctrl = ev->value;
      continue;
    }
    if (ev->code == 58) {
      caps = ev->value;
      continue;
    }
    if (ev->code == 125) {
      super = ev->value;
      continue;
    }
    if (super && ev->code == 15 && ev->value == 0) {  // super + tab
      textwm_set_enabled(textwm_disabled);
      continue;
    }
    if (textwm_disabled) continue;
    if (ev->value == 0) continue;
    if (alt) {
      if (ev->code >= 2 && ev->code < 2+TTY_COUNT) {
        tty_set_active(ev->code - 2);
        continue;
      }
      buf[res++] = '\e';
      /*switch (ev->code) {
        case 17: ev->code = 103; break; // alt + w -> up
        case 30: ev->code = 105; break; // alt + a -> left
        case 31: ev->code = 108; break; // alt + s -> down
        case 32: ev->code = 106; break; // alt + d -> right
        default: continue;
      }*/
    }
    /*if (caps) {
      switch (ev->code) {
        case 17: case 103: ev->code = 104; break; // caps + w -> PgUp
        case 30: case 105: ev->code = 102; break; // caps + a -> Home
        case 31: case 108: ev->code = 109; break; // caps + s -> PgDown
        case 32: case 106: ev->code = 107; break; // caps + d -> End
        default: continue;
      }
    }*/
    if (super) {
      struct TTY *tty = &ttys[active_tty];
      switch (ev->code) {
        case 9: tty->workspace = 0; break; // ws0
        case 10: tty->workspace = 1; break; // ws1
        case 11: tty->workspace = ~tty->workspace; break; // tws
        case 17: case 103: if (shift) tty->window_height--; else tty->window_posy--; break; // up
        case 30: case 105: if (shift) tty->window_width--; else tty->window_posx--; break; // left
        case 31: case 108: if (shift) tty->window_height++; else tty->window_posy++; break; // down
        case 32: case 106: if (shift) tty->window_width++; else tty->window_posx++; break; // right
        default: continue;
      }
      if (shift && tty->workspace < 0) tty->workspace = ~tty->workspace;
      if (tty->window_height < 2) tty->window_height = 2;
      if (tty->window_width < 2) tty->window_width = 2;
      if (tty->window_posx < 0) tty->window_posx = 0;
      if (tty->window_posy < 0) tty->window_posy = 0;
      if (tty->window_height > text_height) tty->window_height = text_height;
      if (tty->window_width > text_width) tty->window_width = text_width;
      if (tty->window_posx + tty->window_width > text_width) tty->window_posx = text_width - tty->window_width;
      if (tty->window_posy + tty->window_height > text_height) tty->window_posy = text_height - tty->window_height;
      resize_tty(active_tty);
      continue;
    }
    int c = 0;
    if (ev->code <= 53) {
      c = (shift ? with_shift : without_shift)[ev->code];
    } else {
      switch (ev->code) {
        case  57: c = ' '; break;
        case  59: res = escape(buf, res, "\e[[A");   break; // F1
        case  60: res = escape(buf, res, "\e[[B");   break; // F2
        case  61: res = escape(buf, res, "\e[[C");   break; // F3
        case  62: res = escape(buf, res, "\e[[D");   break; // F4
        case  63: res = escape(buf, res, "\e[[E"); break; // F5
        case  64: res = escape(buf, res, "\e[17~"); break; // F6
        case  65: res = escape(buf, res, "\e[18~"); break; // F7
        case  66: res = escape(buf, res, "\e[19~"); break; // F8
        case  67: res = escape(buf, res, "\e[20~"); break; // F9
        case  68: res = escape(buf, res, "\e[21~"); break; // F10
        case  87: res = escape(buf, res, "\e[23~"); break; // F11
        case  88: res = escape(buf, res, "\e[24~"); break; // F12
        case 102: res = escape(buf, res, "\e[H");    break; // home
        case 103: res = escape(buf, res, "\e[A");    break; // up
        case 104: res = escape(buf, res, "\e[5~");   break; // PgUp
        case 105: res = escape(buf, res, "\e[D");    break; // left
        case 106: res = escape(buf, res, "\e[C");    break; // right
        case 107: res = escape(buf, res, "\e[F");    break; // end
        case 108: res = escape(buf, res, "\e[B");    break; // down
        case 109: res = escape(buf, res, "\e[6~");   break; // PgDown
        case 111: res = escape(buf, res, "\e[C\b");  break; // del
        // menu  127
        default: printf("[textwm] keypress %d\n", ev->code);
      }
    }
    if (ctrl && c >= 'a' && c <= 'z') c -= ('a' - 1);
    if (c) buf[res++] = c;
  }
  return res;
}

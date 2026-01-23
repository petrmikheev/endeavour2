#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <asm/termbits.h>
#include <string.h>
#include <errno.h>

#include <endeavour2/display.h>

#include "utf8.h"
#include "tty.h"
#include "input.h"
#include "textwm.h"

int display_fd;
char* text_buffers;
char* graphic_buffer;

int display_cfg = -1;
int display_width, display_height;
int font_height;
const int font_width = 8;
int text_width, text_height;
bool textwm_disabled = false;

bool read_display_cfg() {
  struct DisplaySize size = display_get_size(display_fd);
  int dcfg = display_get_cfg(display_fd);

  if (dcfg == display_cfg && size.x == display_width && size.y == display_height) return false;

  display_width = size.x;
  display_height = size.y;
  display_cfg = dcfg;
  font_height = ((dcfg >> 4) & 15) + 1;
  if (font_height < 6) font_height = 6;
  text_width = display_width / font_width;
  text_height = display_height / font_height;
  return true;
}

struct pollfd pfds[TTY_COUNT + 2];

void init_ttys() {
  struct winsize ws;
  ws.ws_col = text_width;
  ws.ws_row = text_height;
  for (int i = 0; i < TTY_COUNT; ++i) {
    struct TTY *tty = &ttys[i];
    char path[64];
    snprintf(path, 64, "/dev/ptyp%d", i);
    tty->fd = open(path, O_RDWR);
    ioctl(tty->fd, TIOCSWINSZ, &ws);
    tty->frame = text_buffers + i * TEXT_BUFFER_SIZE;
    tty->width = tty->window_width = text_width;
    tty->height = tty->window_height = text_height;
    tty->state = 0;
    if (i == 7) {
      tty->frame_start = display_get_text_addr(display_fd) & (TEXT_BUFFER_SIZE - 1);
      tty->line = tty->height - 1;
    } else {
      tty->frame_start = 0;
      tty->line = 0;
    }
    tty->column = 0;
    tty->window_posx = tty->window_posy = 0;
    tty->workspace = -1;
    tty->style = tty->cdata_at_cursor = DEFAULT_STYLE;
    tty->bold = false;
    tty->cursor_visible = false;
    tty->cursor_blink = false;
    tty->cursor_hidden = false;
    tty->vt100_graphics = false;
    tty->scroll_from = 0;
    tty->scroll_to = tty->height;
    //tty->in_start = tty->in_end = 0;
    pfds[i + 2].fd = tty->fd;
    pfds[i + 2].events = POLLIN;
  }
}

void set_wallpaper(const char* arg) {
  int dcfg = display_get_cfg(display_fd);
  if (arg == 0 || *arg == 0 || strcmp(arg, "off") == 0) {
    printf("[textwm] Wallpaper off\n");
    dcfg &= ~DISPLAY_CFG_GRAPHIC_ON;
  } else {
    printf("[textwm] Wallpaper \"%s\"\n", arg);
    FILE* f = fopen(arg, "rb");
    if (!f) {
      printf("[textwm] Can't open file\n");
      return;
    }
    struct {
      unsigned short unused1[5];
      unsigned short bitmap_offset;
      unsigned short unused2[3];
      unsigned short width;
      unsigned short unused3;
      unsigned short height;
      unsigned short unused4[2];
      unsigned short bits_per_pixel;
    } header;
    fread(&header, sizeof(header), 1, f);
    fseek(f, 0L, SEEK_END);
    unsigned size = ftell(f);

    unsigned bytes_per_line = (header.width * 2 + 3) & ~3;
    if ((size != header.bitmap_offset + bytes_per_line * header.height) || header.bits_per_pixel != 16) {
      printf("[textwm] Unsupported image. Expected RGB565 BMP without color table.\n");
      return;
    }

    for (int j = 0; j < header.height; ++j) {
      fseek(f, header.bitmap_offset + (header.height-j-1) * (header.width * 2), SEEK_SET);
      char* dst = graphic_buffer + j * GRAPHIC_LINE_SIZE;
      fread(dst, 2, header.width, f);
    }
    fclose(f);
    display_set_graphic_addr(display_fd, GRAPHIC_BUFFER(0));
    dcfg |= DISPLAY_CFG_GRAPHIC_ON;
  }
  display_set_cfg(display_fd, dcfg);
  display_cfg = dcfg;
}

void update_taddr(int tty_id) {
  struct TTY *tty = &ttys[tty_id];
  if (tty_id == active_tty && tty->workspace < 0) {
    display_set_text_addr(display_fd, TEXT_BUFFER(0) + (tty->frame - text_buffers) + tty->frame_start, 0, 0);
  }
}

void resize_tty(int tty_id) {
  struct TTY *tty = &ttys[tty_id];
  struct winsize new_size;
  new_size.ws_row = tty->workspace < 0 ? text_height : tty->window_height;
  new_size.ws_col = tty->workspace < 0 ? text_width : tty->window_width;
  int prev_height = tty->height;
  int fill = DEFAULT_STYLE | ' ';
  if (tty->line >= prev_height - 1 || tty->line >= new_size.ws_row - 1) {
    tty->line = new_size.ws_row - 1;
    tty->frame_start = (tty->frame_start + ((prev_height - new_size.ws_row) << 10)) & (TEXT_BUFFER_SIZE - 1);
    for (int i = 0; i < new_size.ws_row - prev_height; ++i) {
      int* lp = (int*)(tty->frame + ((tty->frame_start + (i<<10)) & (TEXT_BUFFER_SIZE - 1)));
      for (int j = 0; j < TEXT_LINE_SIZE / 4; ++j) lp[j] = fill;
    }
  } else {
    for (int i = prev_height; i < new_size.ws_row + 1; ++i) {
      int* lp = (int*)(tty->frame + ((tty->frame_start + (i<<10)) & (TEXT_BUFFER_SIZE - 1)));
      for (int j = 0; j < TEXT_LINE_SIZE / 4; ++j) lp[j] = fill;
    }
  }
  tty->height = new_size.ws_row;
  tty->width = new_size.ws_col;
  tty->scroll_from = 0;
  tty->scroll_to = tty->height;
  ioctl(tty->fd, TIOCSWINSZ, &new_size);
  update_taddr(tty_id);
}

void display_cfg_changed() {
  read_display_cfg();
  for (int i = 0; i < TTY_COUNT; ++i) {
    struct TTY *tty = &ttys[i];
    if (tty->workspace < 0) resize_tty(i);
  }
}

void set_font(const char* arg, bool bold) {
  if (bold)
    printf("[textwm] Loading font-bold \"%s\"\n", arg);
  else
    printf("[textwm] Loading font \"%s\"\n", arg);
  FILE* f = fopen(arg, "r");
  if (!f) {
    printf("[textwm] Can't open file\n");
    return;
  }
  char line[120];
  int cwidth = 0, cheight = 0;
  int ucode = 0;
  char cdata[16];
  int ci = -1;
  while (!feof(f)) {
    fgets(line, 119, f);
    if (strncmp(line, "ENCODING ", 9) == 0) {
      sscanf(line, "ENCODING %d", &ucode);
    } else if (strncmp(line, "BBX ", 4) == 0 && cwidth == 0) {
      sscanf(line, "BBX %d %d", &cwidth, &cheight);
      printf("[textwm] font width=%d height=%d\n", cwidth, cheight);
    } else if (strncmp(line, "BITMAP", 6) == 0) {
      ci = 0;
    } else if (strncmp(line, "ENDCHAR", 7) == 0) {
      int code = from_utf(ucode, bold);
      if (code < 32) continue;
      if (bold && code == from_utf(ucode, false)) continue;
      display_set_charmap(display_fd, code, (const unsigned*)cdata);
      ci = -1;
    } else if (ci >= 0) {
      unsigned v;
      sscanf(line, "%x", &v);
      cdata[ci++ & 15] = v;
    }
  }
  fclose(f);
  if (!bold && cwidth && cheight && cheight != font_height) {
    unsigned dcfg = display_get_cfg(display_fd);
    dcfg = (dcfg & ~0xf0) | ((cheight-1) << 4);
    display_set_cfg(display_fd, dcfg);
    display_cfg_changed();
  }
}

void textwm_set_enabled(bool enable) {
  printf("[textwm] enable=%d\n", enable);
  textwm_disabled = !enable;
  int dcfg = display_get_cfg(display_fd);
  if (enable)
    dcfg |= DISPLAY_CFG_TEXT_ON;
  else
    dcfg &= ~DISPLAY_CFG_TEXT_ON;
  display_set_cfg(display_fd, dcfg);
}

void set_resolution(int w, int h) {
  struct VideoMode* mode = 0;
  if (w == 640 && h == 480) mode = &mode_640x480_60;
  else if (w == 800 && h == 600) mode = &mode_800x600_60;
  else if (w == 1024 && h == 768) mode = &mode_1024x768_60;
  else if (w == 1280 && h == 720) mode = &mode_1280x720_60;
  else if (w == 1920 && h == 1080) mode = &mode_1920x1080_25;
  else {
    printf("[textwm] Invalid resolution %dx%d. Supported: 640x480 / 800x600 / 1024x768 / 1280x720 / 1920x1080\n", w, h);
    return;
  }
  display_set_mode(display_fd, mode);
  display_cfg_changed();
}

void command(char* cmd) {
  for (char* ptr = cmd; *ptr; ptr++) if (*ptr == '#') *ptr = ' ';
  unsigned i = -1, color, alpha, bg_alpha;
  if (strncmp(cmd, "color", 5) == 0) {
    if (sscanf(cmd, "color%d %x %d %d", &i, &color, &alpha, &bg_alpha) != 4) goto err;
    display_set_colormap(display_fd, 16 + i, (color << 8) | alpha);
    display_set_colormap(display_fd, i, (color << 8) | bg_alpha);
  } else if (strncmp(cmd, "fgcolor", 7) == 0) {
    if (sscanf(cmd, "fgcolor%d %x %d", &i, &color, &alpha) != 3) goto err;
    display_set_colormap(display_fd, 16 + i, (color << 8) | alpha);
  } else if (strncmp(cmd, "bgcolor", 7) == 0) {
    if (sscanf(cmd, "bgcolor%d %x %d", &i, &color, &alpha) != 3) goto err;
    display_set_colormap(display_fd, i, (color << 8) | alpha);
  } else if (strncmp(cmd, "screen_color", 12) == 0) {
    if (sscanf(cmd, "screen_color %x %d", &color, &alpha) != 2) goto err;
    display_set_colormap(display_fd, SCREEN_BG, (color << 8) | alpha);
  } else if (strncmp(cmd, "window_color", 12) == 0) {
    if (sscanf(cmd, "window_color %x %d", &color, &alpha) != 2) goto err;
    display_set_colormap(display_fd, WINDOW_BG, (color << 8) | alpha);
  } else if (strncmp(cmd, "active_window_color", 19) == 0) {
    if (sscanf(cmd, "active_window_color %x %d", &color, &alpha) != 2) goto err;
    display_set_colormap(display_fd, ACTIVE_WINDOW_BG, (color << 8) | alpha);
  } else if (strcmp(cmd, "graphic off") == 0) {
    set_wallpaper(0);
  } else if (strcmp(cmd, "text off") == 0) {
    textwm_set_enabled(false);
  } else if (strcmp(cmd, "text on") == 0) {
    textwm_set_enabled(true);
  } else if (strncmp(cmd, "wallpaper ", 10) == 0) {
    const char* arg = cmd + 10;
    while (*arg == ' ') arg++;
    set_wallpaper(arg);
  } else if (strncmp(cmd, "font ", 5) == 0) {
    const char* arg = cmd + 5;
    while (*arg == ' ') arg++;
    set_font(arg, false);
  } else if (strncmp(cmd, "font-bold ", 10) == 0) {
    const char* arg = cmd + 10;
    while (*arg == ' ') arg++;
    set_font(arg, true);
  } else if (strcmp(cmd, "display off") == 0) {
    display_set_mode(display_fd, 0);
  } else if (strncmp(cmd, "display ", 8) == 0) {
    int w, h;
    if (sscanf(cmd, "display %dx%d", &w, &h) != 2) goto err;
    set_resolution(w, h);
  } else if (strncmp(cmd, "window", 6) == 0) {
    int ws, x=-1, y=-1, w=-1, h=-1;
    int rcount = sscanf(cmd, "window%d ws=%d x=%d y=%d w=%d h=%d", &i, &ws, &x, &y, &w, &h);
    if (rcount < 2 || (rcount > 2 && rcount != 6) || i < 0 || i >= TTY_COUNT) goto err;
    struct TTY *tty = &ttys[i];
    tty->workspace = ws;
    if (rcount == 6) {
      tty->window_posx = x;
      tty->window_posy = y;
      tty->window_width = w;
      tty->window_height = h;
    }
    resize_tty(i);
  } else if (strncmp(cmd, "active ", 7) == 0) {
    sscanf(cmd, "active %d", &i);
    tty_set_active(i);
  } else if (strcmp(cmd, "togglefull") == 0) {
    ttys[active_tty].workspace = ~ttys[active_tty].workspace;
    resize_tty(active_tty);
  } else goto err;
  return;
err:
  printf("[textwm] Invalid command: %s\n", cmd);
}

#define CMD_BUF_SIZE 160
char cmd_buf[CMD_BUF_SIZE];
int cmd_len = 0;

void cfg_handler(char c) {
  if (c == '\n' || cmd_len == CMD_BUF_SIZE - 1) {
    cmd_buf[cmd_len] = 0;
    if (cmd_buf[0] != 0 && cmd_buf[0] != '#') command(cmd_buf);
    cmd_len = 0;
  } else {
    cmd_buf[cmd_len++] = c;
  }
}

void init_special_chars() {
  static const char c8[] = {0x00, 0x07, 0x1f, 0x3f, 0x3f, 0x7f, 0x7f, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  static const char c9[] = {0x00, 0xe0, 0xf8, 0xfc, 0xfc, 0xfe, 0xfe, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  static const char cA[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x7f, 0x7f, 0x3f, 0x3f, 0x1f, 0x07, 0x00};
  static const char cB[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfe, 0xfe, 0xfc, 0xfc, 0xf8, 0xe0, 0x00};
  display_set_charmap(display_fd, 0x1E8, (const unsigned*)c8);
  display_set_charmap(display_fd, 0x1E9, (const unsigned*)c9);
  display_set_charmap(display_fd, 0x1EA, (const unsigned*)cA);
  display_set_charmap(display_fd, 0x1EB, (const unsigned*)cB);
  display_set_colormap(display_fd, WINDOW_BG + 1, COLORMAP_TEXT_COLOR(242, 82, 0) | COLORMAP_TEXT_ALPHA(64));
  display_set_colormap(display_fd, ACTIVE_WINDOW_BG + 1, COLORMAP_TEXT_COLOR(242, 82, 0) | COLORMAP_TEXT_ALPHA(64));
}

void hborder(int tty_id, unsigned* buf, int y, bool top) {
  if (y < 0 || y >= text_height) return;
  buf += y << 8;
  struct TTY *tty = &ttys[tty_id];
  int wbg = tty_id == active_tty ? ACTIVE_WINDOW_BG : WINDOW_BG;
  int l = tty->window_posx - 1;
  int r = tty->window_posx + tty->width;
  unsigned st = TEXT_FG(wbg) | TEXT_BG(SCREEN_BG);
  unsigned v = TEXT_BG(wbg) | ' ';
  if (l >= 0) {
    buf[l] = (top ? 0x1E8 : 0x1EA) | st;
  } else l = -1;
  if (r < text_width) {
    buf[r] = (top ? 0x1E9 : 0x1EB) | st;
  } else r = text_width;
  for (unsigned* p = buf + l + 1; p < buf + r; ++p) *p = v;
}

void vborder(int tty_id, unsigned* lbuf, int x, bool left) {
  if (x < 0 || x >= text_width) return;
  lbuf[x] = tty_id == active_tty ? TEXT_BG(ACTIVE_WINDOW_BG) | ' ' : TEXT_BG(WINDOW_BG) | ' ';
}

int blink_counter = 0;

void timer_handler(int sig, siginfo_t *si, void *uc) {
  //printf("timer %d\n");
  if (textwm_disabled) return;
  struct TTY *atty = &ttys[active_tty];
  if (atty->cursor_blink) {
    blink_counter = (blink_counter + 1) & 15;
    if (blink_counter >= 8)
      hide_cursor(atty);
    else if (!atty->cursor_hidden)
      show_cursor(atty);
  }
  int workspace = atty->workspace;
  if (workspace < 0) return;
  unsigned taddr = display_get_text_addr(display_fd);
  taddr = taddr == TEXT_BUFFER(14) ? TEXT_BUFFER(14) + TEXT_BUFFER_SIZE / 2 : TEXT_BUFFER(14);
  char* buf = text_buffers + taddr - TEXT_BUFFER(0);
  int fill = TEXT_BG(SCREEN_BG) | ' ';
  for (int* p = (int*)buf; p < (int*)(buf + TEXT_BUFFER_SIZE / 2); ++p) *p = fill;
  for (int tty_id = 0; tty_id < TTY_COUNT; ++tty_id) {
    struct TTY *tty = &ttys[tty_id];
    if (tty->workspace != workspace) continue;
    hborder(tty_id, (unsigned*)buf, tty->window_posy - 1, true);
    for (unsigned y = 0; y < tty->height; ++y) {
      int ty = tty->window_posy + y;
      if (ty < 0 || ty >= text_height) continue;
      const char* src = tty->frame + ((tty->frame_start + y * TEXT_LINE_SIZE) & (TEXT_BUFFER_SIZE - 1));
      unsigned* dst = (unsigned*)(buf + (ty << 10));
      vborder(tty_id, dst, tty->window_posx - 1, true);
      for (unsigned x = 0; x < tty->width; ++x) {
        int tx = tty->window_posx + x;
        if (tx < 0 || tx >= text_width) continue;
        unsigned v = *(unsigned*)(src + (x<<2));
        if (tty_id != active_tty && ((v >> 24)&127) == ACTIVE_WINDOW_BG) v += (WINDOW_BG - ACTIVE_WINDOW_BG) << 24;
        dst[tx] = v;
      }
      vborder(tty_id, dst, tty->window_posx + tty->width, false);
    }
    hborder(tty_id, (unsigned*)buf, tty->window_posy + tty->height, false);
  }
  display_set_text_addr(display_fd, taddr, 0, 0);
}

void initialize_timer(void) {
  struct sigaction sa = { 0 };
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = timer_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGRTMIN, &sa, NULL);

  struct itimerspec its = {
    .it_value.tv_sec  = 0,
    .it_value.tv_nsec = 100000000,
    .it_interval.tv_sec  = 0,
    .it_interval.tv_nsec = 100000000
  };

  struct sigevent sev = { 0 };
  sev.sigev_notify = SIGEV_SIGNAL;
  sev.sigev_signo = SIGRTMIN;

  timer_t timerId;
  timer_create(CLOCK_REALTIME, &sev, &timerId);
  timer_settime(timerId, 0, &its, NULL);
}

#define DEV_CFG "/dev/textwmcfg"

static int open_cfg() { return open(DEV_CFG, O_RDONLY | O_NONBLOCK); }

int main() {
  printf("textwm started\n");
  display_fd = display_open();
  text_buffers = display_map_video_memory(display_fd, TEXT_BUFFER(0), TEXT_BUFFER_SIZE * 15);
  graphic_buffer = display_map_video_memory(display_fd, GRAPHIC_BUFFER(0), GRAPHIC_BUFFER_SIZE);
  display_disable_sbi_console(display_fd);
  init_special_chars();
  init_vt100_graphic_table();

  // TODO move or disable bios graphic buffer
  for (int i = 0; i < TEXT_BUFFER_SIZE / 4 * 15; ++i) ((unsigned*)text_buffers)[i] = DEFAULT_STYLE | ' ';

  const char* cfg_name = "/dev/textwmcfg";
  mkfifo(cfg_name, 0666);

  pfds[0].fd = open_input();
  pfds[0].events = POLLIN;
  pfds[1].fd = open_cfg();
  pfds[1].events = POLLIN;

  read_display_cfg();
  printf("tty width=%d height=%d\n", text_width, text_height);
  init_ttys();

  initialize_timer();

  FILE* init_cfg = fopen("/etc/textwm2.cfg", "r");
  if (init_cfg) {
    int c;
    while ((c = fgetc(init_cfg)) >= 0) cfg_handler(c);
    fclose(init_cfg);
  }

#define BUF_SIZE 64
  static char buf[BUF_SIZE];
  int rsize;
  while (true) {
    if (pfds[0].fd < 0) {
      pfds[0].fd = open_input();
    }
    int poll_st = poll(pfds, TTY_COUNT + 2, -1);
    if (poll_st < 0) {
      if (errno != EINTR)
        printf("[textwm] Error in poll: %d %d\n", poll_st, errno);
      continue;
    }
    //printf("poll %x %x %x\n", pfds[0].revents, pfds[1].revents, pfds[2].revents);
    if (pfds[0].revents & POLLIN) {
      while (true) {
        rsize = parse_input_events(pfds[0].fd, buf, BUF_SIZE);
        if (rsize == 0) break;
        if (write(ttys[active_tty].fd, buf, rsize) != rsize) {
          printf("Failed to write input to TTY\n");
        }
        /*struct TTY *tty = &ttys[active_tty];
        for (int j = 0; j < rsize; ++j) {
          tty->in_fifo[tty->in_end++] = buf[j];
          if (tty->in_end == IN_FIFO_SIZE) tty->in_end = 0;
        }*/
      }
      blink_counter = 0;
    }
    if (pfds[0].revents & (POLLHUP|POLLERR)) {
      close(pfds[0].fd);
      pfds[0].fd = -1;
    }
    if (pfds[1].revents & POLLIN) {
      rsize = read(pfds[1].fd, buf, BUF_SIZE);
      for (int j = 0; j < rsize; ++j) cfg_handler(buf[j]);
    }
    if (pfds[1].revents & POLLHUP) {
      close(pfds[1].fd);
      pfds[1].fd = open_cfg();
    }
    for (int i = 0; i < TTY_COUNT; ++i) {
      if (pfds[i + 2].revents & POLLIN) {
        struct TTY *tty = &ttys[i];
        tty->cursor_blink = false;
        hide_cursor(tty);
        rsize = read(pfds[i + 2].fd, buf, BUF_SIZE);
        for (int j = 0; j < rsize; ++j) tty_handler(i, buf[j]);
        if (!tty->cursor_hidden) show_cursor(tty);
        tty->cursor_blink = true;
      }
      /*struct TTY *tty = &ttys[i];
      while (tty->in_start != tty->in_end) {
        if (write(tty->fd, &tty->in_fifo[tty->in_start], 1) == 1) {
          if (++tty->in_start == IN_FIFO_SIZE) tty->in_start = 0;
        } else
          break;
      }
      if (tty->in_start == tty->in_end)
        pfds[i + 2].events &= ~POLLOUT;
      else
        pfds[i + 2].events |= ~POLLOUT;*/
    }
  }
  return 0;
}

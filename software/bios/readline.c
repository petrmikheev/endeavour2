#include <endeavour2/raw/defs.h>

#include "bios_internal.h"

struct {
  char* buffer;
  unsigned size;
  unsigned max_size;
  int finished;
} state;

int blink_state = 0;

static void blink_set(int v) {
  if (blink_state == v) return;
  blink_state = v;
  unsigned c = *cursor_ptr;
  unsigned bg = (c >> 24) & 127;
  unsigned fg = (c >> 16) & 127;
  *cursor_ptr = TEXT_BG(fg) | TEXT_FG(bg) | (c & 0x8080ffff);
}

static void process(int c) {
  blink_set(0);
  if (c == '\r') c = '\n';
  if (c == '\b' && state.size > 0) {
    state.size--;
    putchar('\b');
  } else if (c == '\n') {
    putchar('\n');
    state.buffer[state.size] = 0;
    state.finished = 1;
    return;
  }
  if (c < 32 || state.size == state.max_size - 1) return;
  putchar(c);
  state.buffer[state.size++] = c;
}

static const char* usb_to_char       = "abcdefghijklmnopqrstuvwxyz1234567890\n\e\b\t -=[]\\.;'`,./";
static const char* usb_to_char_shift = "ABCDEFHHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\e\b\t _+{}|.:\"~<>?";

static void process_usb_code(int modifiers, int code) {
  // 74-78 Home PgUp Delete End PgDn
  // 79-82 Right Left Down Up
  if (code > 56) printf("[%d]", code);
  if (code < 4 || code > 56) return;
  if (modifiers & (KEY_MOD_LSHIFT|KEY_MOD_RSHIFT))
    process(usb_to_char_shift[code - 4]);
  else
    process(usb_to_char[code - 4]);
}

static unsigned last_kb_time = 0;
static unsigned all_same_counter = 0;
static struct KeyboardReport last_kb;

void readline(const char* prompt, char* buffer, unsigned max_size) {
  printf(prompt);
  state.buffer = buffer;
  state.size = 0;
  state.max_size = max_size;
  state.finished = 0;
  while (1) {
    int c;
    while ((c = UART_REGS->rx) >= 0) {
      if (c > 255) {
        printf("\nUART error %d\n", c >> 8);
        *buffer = 0;
        uart_flush();
        blink_set(0);
        return;
      }
      process(c);
      if (state.finished) return;
    }
    if (time_100nsec() - last_kb_time < 200000) continue;  // max every 20ms
    last_kb_time = time_100nsec();
    blink_set((last_kb_time >> 22) & 1);
    struct KeyboardReport kb;
    if (get_keyboard_report(&kb) != 0) continue;
    int all_same = 1;
    int last = 0;
    for (int i = 0; i < 6; ++i) {
      all_same = all_same && kb.pressed[i] == last_kb.pressed[i];
      int code = kb.pressed[i];
      if (!code) continue;
      int new = 1;
      for (int j = 0; j < 6; ++j) new = new && code != last_kb.pressed[j];
      if (new) {
        process_usb_code(kb.modifiers, code);
        if (state.finished) break;
      } else
        last = code;
    }
#define KEY_FIRST_DELAY 20
#define KEY_NEXT_DELAY 1
    if (all_same && last && !state.finished) {
      if (all_same_counter == 0) {
        process_usb_code(kb.modifiers, last);
        all_same_counter = KEY_NEXT_DELAY;
      } else {
        all_same_counter--;
      }
    } else {
      all_same_counter = KEY_FIRST_DELAY;
    }
    ((unsigned*)&last_kb)[0] = ((unsigned*)&kb)[0];
    ((unsigned*)&last_kb)[1] = ((unsigned*)&kb)[1];
    if (state.finished) return;
  }
}

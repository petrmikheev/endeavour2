#ifndef TEXTWM_H
#define TEXTWM_H

#define bool char
#define false 0
#define true 1

void update_taddr(int tty_id);
void resize_tty(int tty_id);

extern int text_width, text_height;
extern char* text_buffers;

extern bool textwm_disabled;

void textwm_set_enabled(bool enable);

#define ACTIVE_WINDOW_BG 32
#define WINDOW_BG 34
#define SCREEN_BG 36

#define DEFAULT_STYLE (TEXT_BG(ACTIVE_WINDOW_BG) | TEXT_FG(31))

#endif // TEXTWM_H

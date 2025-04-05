#ifndef ENDEAVOUR2_KEYBOARD_REPORT_H
#define ENDEAVOUR2_KEYBOARD_REPORT_H

struct KeyboardReport {
  unsigned char modifiers;
  unsigned char reserved;
  unsigned char pressed[6];
#define KEY_MOD_LCTRL   (1<<0)
#define KEY_MOD_LSHIFT  (1<<1)
#define KEY_MOD_LALT    (1<<2)
#define KEY_MOD_LSUPER  (1<<3)
#define KEY_MOD_RCTRL   (1<<4)
#define KEY_MOD_RSHIFT  (1<<5)
#define KEY_MOD_RALT    (1<<6)
#define KEY_MOD_RSUPER  (1<<7)
};

#endif  // ENDEAVOUR2_KEYBOARD_REPORT_H

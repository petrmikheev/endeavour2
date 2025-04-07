#ifndef ENDEAVOUR2_BIOS_DEFS_H
#define ENDEAVOUR2_BIOS_DEFS_H

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

struct HartCfg {
  unsigned ready;
  const void* jump_to;
  unsigned isa;
  // debug info
  unsigned cause;
  unsigned tval;
  unsigned epc;
  unsigned sp;
  unsigned ra;
};

enum VideoModeId {
  VIDEO_MODE_CUSTOM = 0,
  VIDEO_MODE_640x480 = 640,
  VIDEO_MODE_800x600 = 800,
  VIDEO_MODE_1024x768 = 1024,
  VIDEO_MODE_1280x720 = 1280,
  VIDEO_MODE_1920x1080 = 1920,
  VIDEO_MODE_1920x1080_25 = 1921
};

#endif  // ENDEAVOUR2_BIOS_DEFS_H

#ifndef ENDEAVOUR2_DISPLAY_H
#define ENDEAVOUR2_DISPLAY_H

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>

// R, G, B in range 0-255.
// RGB565 takes 5 most significant bits of R, B, and 6 bits of G. Returns 16 bit color.
#define RGB565(R, G, B) ((unsigned short)((((R)>>3)<<11) | (((G)>>2)<<5) | ((B)>>3)))

#define TEXT_FOUR_COLOR_FLAG (1<<31)
#define TEXT_BG(X) ((X)<<24)
#define TEXT_FG(X) ((X)<<16)

// char format in text buffer
// bit  31 30 ... 24    23   22 ... 16 15 ... 8  7  ... 0
//       0 <bg style>    X  <fg style> XXXXXX <char code>     - normal mode
//       1 <bg style>    X  <fg style> <code_l>  <code_h>     - 4-color mode
// in 4-color mode charmap entries <256+code_l> and <256+code_h> are used together.
// colors are <bg style>, <bg style + 1>, <fg style>, <fg style + 1>

#define TEXT_BUFFER_SIZE 0x40000  // 256KB
#define TEXT_LINE_SIZE      1024  // 1KB

#define GRAPHIC_BUFFER_SIZE 0x800000 // 8MB
#define GRAPHIC_LINE_SIZE       4096 // 4KB

// TEXT_BUFFER(0) - TEXT_BUFFER(30)
#define TEXT_BUFFER(i) (((i)+1) * TEXT_BUFFER_SIZE)

// GRAPHIC_BUFFER(0) - GRAPHIC_BUFFER(2)
#define GRAPHIC_BUFFER(i) (((i)+1) * GRAPHIC_BUFFER_SIZE)

// Returns file descriptor which is needed for all other function in the file.
// Use `close` at the end.
static inline int display_open() { return open("/dev/display", O_RDWR); }

// Video memory is low 32MB of main memory. First 32KB are reserved by BIOS and can't be used.
// TEXT_BUFFER and GRAPHIC_BUFFER macro are optional helpers to calculate offset in video memory.
// Examples:
//   void* text_buffers = display_map_video_memory(fd, TEXT_BUFFER(0), TEXT_BUFFER_SIZE * 4)   - map 4 text buffers to user space
//   void* frame_buffer = display_map_video_memory(fd, GRAPHIC_BUFFER(1), GRAPHIC_BUFFER_SIZE) - map second graphic buffer to user space
// Use `munmap` to free.
static inline void* display_map_video_memory(int fd, unsigned offset, unsigned size) {
  return mmap(0, size,  PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)offset);
}

struct TextAddrAndOffset {
  unsigned buffer_addr;
  unsigned short pixel_offset_x;
  unsigned short pixel_offset_y;
};

static inline struct TextAddrAndOffset display_get_text_addr_and_offset(int fd) {
  struct TextAddrAndOffset v;
  ioctl(fd, 0xaa0, &v);
  return v;
}
static inline unsigned display_get_text_addr(int fd) { return display_get_text_addr_and_offset(fd).buffer_addr; }

static inline int display_set_text_addr(int fd, unsigned addr, unsigned short pixel_offset_x, unsigned short pixel_offset_y) {
  struct TextAddrAndOffset v = {addr, pixel_offset_x, pixel_offset_y};
  return ioctl(fd, 0xaa2, &v);
}

static inline unsigned display_get_graphic_addr(int fd) {
  unsigned v;
  ioctl(fd, 0xaa1, &v);
  return v;
}
static inline int display_set_graphic_addr(int fd, unsigned v) { return ioctl(fd, 0xaa3, &v); }

static inline unsigned display_get_frame_number(int fd) {
  unsigned v;
  ioctl(fd, 0xaa8, &v);
  return v;
}

#define COLORMAP_TEXT_COLOR(R, G, B) ((R)<<24 | (G)<<16 | (B)<<8)
#define COLORMAP_TEXT_ALPHA(A) (A)     // 0 - 64

// Example - set text style 15 (default foreground) to half-transparent blue:
//   display_set_colormap(fd, 15, COLORMAP_TEXT_COLOR(0, 0, 255) | COLORMAP_TEXT_ALPHA(32))
static inline int display_set_colormap(int fd, unsigned index, unsigned rgba) {
  struct { unsigned index, value; } v = {index, rgba};
  return ioctl(fd, 0xaa6, &v);
}

// char_code must be in range [32-511]  (codes 0-31 intersect with colormap)
static inline int display_set_charmap(int fd, unsigned char_code, const unsigned char_data[4]) {
  struct { unsigned index, value; } v;
  for (int i = 0; i < 4; ++i) {
    v.index = (char_code << 2) + i;
    v.value = char_data[i];
    if (ioctl(fd, 0xaa6, &v) < 0) return -1;
  }
  return 0;
}

static inline int display_disable_sbi_console(int fd) { return ioctl(fd, 0xaa7, &fd); }

struct VideoMode {
// clock flags
#define HSYNC_INV (1<<31)
#define VSYNC_INV (1<<30)
  unsigned clock;        //  0
  unsigned hResolution;  //  4
  unsigned hSyncStart;   //  8
  unsigned hSyncEnd;     //  C
  unsigned hTotal;       // 10
  unsigned vResolution;  // 14
  unsigned vSyncStart;   // 18
  unsigned vSyncEnd;     // 1C
  unsigned vTotal;       // 20
};

static struct VideoMode mode_640x480_60   = { 25175000,     640,  656,  752,  800,  480,  490,  492,  525};
static struct VideoMode mode_800x600_60   = { 36000000,     800,  824,  896, 1024,  600,  601,  603,  625};
static struct VideoMode mode_1024x768_60  = { 65000000,    1024, 1048, 1184, 1344,  768,  771,  777,  806};
static struct VideoMode mode_1280x720_60  = { 74250000,    1280, 1720, 1760, 1980,  720,  725,  730,  750};
static struct VideoMode mode_1920x1080_25 = { 74250000,    1920, 2448, 2492, 2640, 1080, 1084, 1089, 1125};
static struct VideoMode mode_1920x1080_50 = {148500000,    1920, 2448, 2492, 2640, 1080, 1084, 1089, 1125};

static inline int display_set_mode(int fd, const struct VideoMode* mode) {
  return ioctl(fd, 0xaaa, mode);
}

struct DisplaySize { unsigned x, y; };

static inline struct DisplaySize display_get_size(int fd) {
  struct DisplaySize s;
  ioctl(fd, 0xaa9, &s);
  return s;
}

#define DISPLAY_CFG_TEXT_ON     1
#define DISPLAY_CFG_GRAPHIC_ON  2
#define DISPLAY_CFG_RGB565      0  // default
#define DISPLAY_CFG_RGAB5515    4
#define DISPLAY_CFG_FONT_HEIGHT(X) ((((X)-1)&15) << 4) // allowed range [6, 16]

static inline unsigned display_get_cfg(int fd) {
  unsigned v;
  ioctl(fd, 0xaa4, &v);
  return v;
}
static inline int display_set_cfg(int fd, unsigned v) { return ioctl(fd, 0xaa5, &v); }

#endif  // ENDEAVOUR2_DISPLAY_H

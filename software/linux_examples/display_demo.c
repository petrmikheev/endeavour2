#include <stdio.h>
#include <endeavour2/display.h>

int main() {
  int fd = display_open();
  void* fb = display_map_video_memory(fd, GRAPHIC_BUFFER(1), GRAPHIC_BUFFER_SIZE);

  display_set_mode(fd, &mode_1280x720_60);
  display_set_graphic_addr(fd, GRAPHIC_BUFFER(1));
  display_set_cfg(fd, display_get_cfg(fd) | DISPLAY_CFG_GRAPHIC_ON);

  for (int y = 0; y < 720; ++y) {
    unsigned short* line = fb + y * GRAPHIC_LINE_SIZE;
    for (int x = 0; x < 1280; ++x) {
      if ((x & 127) == 0 || (y & 127) == 0) {
        line[x] = RGB565(255, 0, 0);
      } else {
        line[x] = RGB565(0, x&255, y&255);
      }
    }
  }

  munmap(fb, GRAPHIC_BUFFER_SIZE);
  close(fd);
  return 0;
}

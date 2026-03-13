#include <endeavour2/raw/defs.h>
#include <endeavour2/raw/bios.h>

static int i2c_wait() {
  int cmd;
  while ((cmd = I2C_REGS->cmd) < 0);
  return cmd & (I2C_CMD_DATA_ERR|I2C_CMD_ADDR_ERR);
}

int i2c_write(int addr, int size, const char* data) {
  int err = 0;
  I2C_REGS->cmd = I2C_CMD_WRITE | addr;
  I2C_REGS->counter = size;
  for (int i = 0; i < size; ++i) {
    err |= i2c_wait();
    I2C_REGS->data = *data++;
  }
  err |= i2c_wait();
  return err;
}

int i2c_read(int addr, int size, char* data) {
  int err = 0;
  I2C_REGS->cmd = I2C_CMD_READ | addr;
  I2C_REGS->counter = size;
  for (int i = 0; i < size; ++i) {
    err |= i2c_wait();
    *data++ = I2C_REGS->data;
  }
  return err;
}

int main() {
  printf("Reading Extended Display Identification Data\n");
  char start_addr = 0;
  int err = i2c_write(I2C_ADDR_EDID, 1, &start_addr);
  char buf[128];
  err |= i2c_read(I2C_ADDR_EDID, 128, (char*)buf);
  if (err) {
    printf("Error %02x\n", err);
    return 0;
  }
  printf("EDID version: %d.%d\n", buf[18], buf[19]);
  printf("Display size: %dx%d cm\n", buf[21], buf[22]);
  printf("Standard modes:\n");
  for (int i = 0; i < 8; ++i) {
    unsigned x = ((unsigned)buf[38 + i*2] + 31) * 8;
    unsigned y;
    unsigned m = buf[38 + i*2 + 1];
    switch (m >> 6) {
      case 0: y = x * 10 / 16; break;
      case 1: y = x * 3 / 4; break;
      case 2: y = x * 4 / 5; break;
      case 3: y = x * 9 / 16; break;
    }
    unsigned freq = (m & 0x3f) + 60;
    printf("\t%dx%d@%d\n", x, y, freq);
  }

  for (int i = 0; i < 4; i++) {
    int offset = 54 + (i * 18);
    if (buf[offset] == 0x00 && buf[offset+1] == 0x00 && buf[offset+2] == 0x00) {  // Monitor descript
        if (buf[offset+3] == 0xFC) { // 0xFC = Monitor Name
            printf("Model Name: ");
            for (int j = 0; j < 13; j++) {
                char c = buf[offset + 5 + j];
                if (c == 0x0A) break; // End on newline
                printf("%c", c);
            }
            printf("\n");
        }
    } else if (buf[offset] != 0x00 || buf[offset+1] != 0x00) {  // Detailed Timing Descriptor
        unsigned pix_clk = ((buf[offset+1] << 8) | buf[offset]);  // 10kHz units

        int h_active = ((buf[offset+4] & 0xF0) << 4) | buf[offset+2];
        int h_blank  = ((buf[offset+4] & 0x0F) << 8) | buf[offset+3];
        int h_front_porch = ((buf[offset+11] & 0xC0) << 2) | buf[offset+8];
        int h_sync_width  = ((buf[offset+11] & 0x30) << 4) | buf[offset+9];

        int v_active = ((buf[offset+7] & 0xF0) << 4) | buf[offset+5];
        int v_blank  = ((buf[offset+7] & 0x0F) << 8) | buf[offset+6];
        int v_front_porch = ((buf[offset+11] & 0x0C) << 2) | ((buf[offset+10] & 0xF0) >> 4);
        int v_sync_width  = ((buf[offset+11] & 0x03) << 4) | (buf[offset+10] & 0x0F);

        char* h_sync_pol = (buf[offset+17] & 0x02) ? "+HSync" : "-HSync";
        char* v_sync_pol = (buf[offset+17] & 0x04) ? "+VSync" : "-VSync";

        printf("Modeline \"%dx%d\" %d.%02d %d %d %d %d %d %d %d %d %s %s\n",
            h_active, v_active, pix_clk/100, pix_clk%100,
            h_active, (h_active + h_front_porch), (h_active + h_front_porch + h_sync_width), (h_active + h_blank),
            v_active, (v_active + v_front_porch), (v_active + v_front_porch + v_sync_width), (v_active + v_blank),
            h_sync_pol, v_sync_pol);
    }
  }
}

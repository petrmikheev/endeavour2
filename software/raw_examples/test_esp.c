// Test code for communication to ESP32 module via SLIP protocol.

#include <endeavour2/raw/bios.h>
#include <endeavour2/raw/defs.h>

static void wait(unsigned t) {
  unsigned start = time_100nsec();
  while (1) {
    if ((time_100nsec() - start) > t) return;
  }
}

static void slip_send(unsigned char c) {
  if (c == 0xc0) {
    ESP32_UART_REGS->tx = 0xdb;
    ESP32_UART_REGS->tx = 0xdc;
  } else if (c == 0xdb) {
    ESP32_UART_REGS->tx = 0xdb;
    ESP32_UART_REGS->tx = 0xdd;
  } else {
    ESP32_UART_REGS->tx = c;
  }
}

static int slip_recv(void) {
  unsigned t = time_100nsec();
  int c;
  do { c = ESP32_UART_REGS->rx; } while (c < 0 && time_100nsec() - t < 1000000);
  if (c < 0) return -2;
  c = c & 0xff;
  if (c == 0xc0) return -1;
  if (c != 0xdb) return c;
  do { c = ESP32_UART_REGS->rx; } while (c < 0 && time_100nsec() - t < 1000000);
  if (c < 0) return -2;
  return c == 0xdc ? 0xc0 : 0xdb;
}

static int slip_command(int command, unsigned cmd_data_size, const char* cmd_data, unsigned* resp_size, unsigned* resp_value, char* resp_data) {
  printf("send cmd\n");

  if (cmd_data_size > 500) {
    printf("data_size > 500 not supported\n");
    return -1;
  }
  /*unsigned char checksum = 0xef;
  for (int i = 0; i < cmd_data_size; ++i) checksum ^= cmd_data[i];*/
  ESP32_UART_REGS->tx = 0xc0;
  ESP32_UART_REGS->tx = 0x00;
  slip_send(command);
  slip_send(cmd_data_size & 0xff);
  slip_send(cmd_data_size >> 8);
  ESP32_UART_REGS->tx = 0x00;//slip_send(checksum);
  ESP32_UART_REGS->tx = 0x00;
  ESP32_UART_REGS->tx = 0x00;
  ESP32_UART_REGS->tx = 0x00;
  for (int i = 0; i < cmd_data_size; ++i) slip_send(cmd_data[i]);
  ESP32_UART_REGS->tx = 0xc0;

  printf("waiting response\n");
  int c = slip_recv();
  if (c == -2) {
    printf("Timeout\n");
    return -1;
  }
  if (c != -1) {
    printf("No 0xc0 before response\n");
    return -1;
  }
  printf("receiving response\n");
  if (slip_recv() != 0x01) {
    printf("Invalid response direction\n");
    return -1;
  }
  if (slip_recv() != command) {
    printf("Invalid cmd in response\n");
    return -1;
  }
  int t = slip_recv();
  *resp_size = (slip_recv() << 8) | t;
  *resp_value = slip_recv();
  *resp_value |= slip_recv() << 8;
  *resp_value |= slip_recv() << 16;
  *resp_value |= slip_recv() << 24;
  for (int i = 0; i < *resp_size; ++i) {
    t = slip_recv();
    if (t == -1) {
      printf("Unexpected end of response\n");
      return -1;
    }
    resp_data[i] = t;
  }
  if (slip_recv() != -1) {
    printf("No 0xc0 after response\n");
    return -1;
  }
  if (*resp_size < 4) {
    printf("No status bytes\n");
    return -1;
  }
  printf("Status 0x%02x, error 0x%02x\n", resp_data[*resp_size - 4], resp_data[*resp_size - 3]);

  return 0;
}

char buf[1024];

int main() {
  printf("Test ESP32\n");

  ESP32_UART_REGS->cfg = UART_BAUD_RATE(115200);
  ESP32_UART_REGS->rx = 0;

  BOARD_REGS->esp32_cfg = BOARD_ESP32_EN;

  unsigned start = time_100nsec();
  while (1) {
    if ((time_100nsec() - start) > 5000000) break;
    int c = ESP32_UART_REGS->rx;
    if (c < 0) continue;
    putchar(c & 0xff);
  }

  unsigned addr = 0x40000000;
  unsigned resp_size;
  unsigned val;

  for (int i = 0; i < 5; ++i) {
    buf[0] = 0x7; buf[1] = 0x7; buf[2] = 0x12; buf[3] = 0x20;
    for (int i = 4; i < 36; ++i) buf[i] = 0x55;
    if (slip_command(0x08, 36, buf, &resp_size, &val, buf) < 0) {
      printf("sync failed\n");
    } else {
      printf("sync ok\n");
      break;
    }
  }
  wait(1000000);
  while (ESP32_UART_REGS->rx >= 0);

  if (slip_command(0x0a, 4, (void*)&addr, &resp_size, &val, buf) < 0) {
    printf("cmd failed\n");
  };
  printf("esp32_mem[%08x] = %08x\n", addr, val);

  BOARD_REGS->esp32_cfg = 0;
  printf("End\n");
  return 0;
}

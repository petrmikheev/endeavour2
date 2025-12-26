// Flashing firmware to the onboard ESP32-C3-WROOM-02 module:
//
// 1. turn on endeavour2 in BIOS mode
// 2. `~/endeavour2/scripts/setup_uart.sh`
// 3. `make esp_bridge.bin`
// 4. `./run.sh esp_bridge.bin`
// 5. `cd /path/to/some_esp_idf_project`
// 6. `idf.py build` (see esp idf docs)
// 7. `cd build && python -m esptool --chip esp32c3 -b 115200 --before default_reset --after hard_reset write_flash "@flash_args"`
// 8. when flashing is finished rerun `~/endeavour2/scripts/setup_uart.sh`
// 9. press USER KEY 1 on the board to close the bridge; then if will dump to stdout (listen /dev/ttyUSB0 to see it) log of all transmitted data.

#include <endeavour2/raw/bios.h>
#include <endeavour2/raw/defs.h>

int main() {
  printf("ESP32 UART boot; UART bridge started\n");

  ESP32_UART_REGS->cfg = UART_BAUD_RATE(115200);
  ESP32_UART_REGS->rx = 0;

  char* data = (void*)0x80800000UL;
  char* data_ptr = data;

  UART_REGS->cfg = UART_BAUD_RATE(115200) | UART_CSTOPB;
  BOARD_REGS->esp32_cfg = BOARD_ESP32_EN;
  while (1) {
    if (BOARD_REGS->keys & 2) break;
    int c = UART_REGS->rx;
    int ec = ESP32_UART_REGS->rx;
    if (c & 0x200) {
      printf("Framing error: %08x\n", c);
      break;
    }
    if (ec & 0x200) {
      printf("ESP Framing error: %08x\n", ec);
      break;
    }
    if (c >= 0) {
      while (ESP32_UART_REGS->tx < 0);
      ESP32_UART_REGS->tx = c;
      data_ptr[0] = 0;
      data_ptr[1] = c;
      data_ptr += 2;
    }
    if (ec >= 0) {
      UART_REGS->tx = ec;
      data_ptr[0] = 1;
      data_ptr[1] = ec;
      data_ptr += 2;
    }
  }
  UART_REGS->cfg = UART_BAUD_RATE(115200) | UART_PARITY_EVEN | UART_CSTOPB;

  printf("Dump of transmitted data:\n");
  int s = 2;
  int pp = 0;
  int sp = 0;
  unsigned cmd, s1, s2;
  while (data < data_ptr) {
    if (s != data[0]) {
      s = data[0];
      printf(s ? "\nIN " : "\nOUT");
    }
    if (s) {
      printf(" %02x", data[1]);
    } else {
      int c = data[1];
      if (c == 0xc0) {
        pp = pp ? 0 : 1;
      } else {
        if (!pp) {
          printf(" NP\n");
        } else if (c == 0xdb) sp = 1;
        else {
          if (sp) {
            sp = 0;
            if (c == 0xdc) c = 0xc0;
            if (c == 0xdd) c = 0xdb;
          }
          if (pp == 2) cmd = c;
          else if (pp == 3) s1 = c;
          else if (pp == 4) {
            s2 = c;
            printf(" CMD%02x:%u ", cmd, (s2 << 8) + s1);
          }
          pp++;
        }
      }
    }
    data += 2;
  }
  printf("\n");

  BOARD_REGS->esp32_cfg = 0;
  printf("\nUART bridge closed\n");
  return 0;
}

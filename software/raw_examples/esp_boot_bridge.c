// Usage:
//
// 1. turn on endeavour2 in BIOS mode
// 2. `~/endeavour2/scripts/setup_uart.sh`
// 3. `tail -f /dev/ttyUSB0` (or use minicom)
// 3. `make esp_boot_bridge.bin`
// 4. `./run.sh esp_boot_bridge.bin`
// 5. onboard ESP32-C3-WROOM-02 module will boot from spi flash (see flasing instructions in esp_bridge.c)
//      with its UART0 bridged to /dev/ttyUSB0
// 6. Use USER KEY 1 on the board to close the bridge and shutdown the ESP module.

#include <endeavour2/raw/bios.h>
#include <endeavour2/raw/defs.h>

int main() {
  printf("ESP32 spi flash boot; UART bridge started\n");

  ESP32_UART_REGS->cfg = UART_BAUD_RATE(115200);
  ESP32_UART_REGS->rx = 0;

  BOARD_REGS->esp32_cfg = BOARD_ESP32_EN | BOARD_ESP32_SPI_BOOT;
  while (1) {
    if (BOARD_REGS->keys & 2) break;
    int c = ESP32_UART_REGS->rx;
    if (c & 0x200) {
      printf("Framing error: %08x\n", c);
      break;
    }
    if (c < 0) continue;
    if (c & 0x100) {
      printf("Parity error: %08x\n", c);
      break;
    }
    UART_REGS->tx = c;
  }

  BOARD_REGS->esp32_cfg = 0;
  printf("UART bridge closed\n");
  return 0;
}

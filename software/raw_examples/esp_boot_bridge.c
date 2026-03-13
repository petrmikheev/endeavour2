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

void read_uart(int break_key) {
  while (1) {
    if (GPIO_REGS->data_in & break_key) break;
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
    if (c != '\r') putchar(c);
  }
}

char cmd[] = {
  0x00, 0x00, 0x01, 0x00,
  0x08, 0x00, 0x0c, 0x00,
  0x16, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

int main() {
  printf("ESP32 spi flash boot; UART bridge started\n");

  ESP32_UART_REGS->cfg = UART_BAUD_RATE(115200);
  ESP32_UART_REGS->rx = 0;
  ESP32_SPI_REGS->cfg = SPI_CPOL /*| SPI_CPHA | 15*/;// | 255;

  GPIO_REGS->data_clear = GPIO_ESP32_EN;
  for (volatile int i = 0; i < 1000; ++i);
  GPIO_REGS->data_set = GPIO_ESP32_EN | GPIO_ESP32_SPI_BOOT;

  //while (1){
  read_uart(GPIO_KEY1);
  //if ((GPIO_REGS->data_in & (GPIO_KEY0|GPIO_KEY1)) == (GPIO_KEY0|GPIO_KEY1)) break;
#if 1
  printf("DR=%d HS=%d\n", !!(GPIO_REGS->data_in & GPIO_ESP32_DR), !!(GPIO_REGS->data_in & GPIO_ESP32_HS));
  ESP32_SPI_REGS->nselect = 0;
  for (int i = 0; i < 32; ++i) ESP32_SPI_REGS->data = 0;
  ESP32_SPI_REGS->counter = 64;
  while (ESP32_SPI_REGS->counter);
  ESP32_SPI_REGS->nselect = 1;
    for (int j = 0; j < 8;++j)
  {for (int i = 0; i < 4; ++i) {
    int v = ESP32_SPI_REGS->data;
    printf("%02x %02x ", v&255, v>>8);
  }
  putchar('\n');}
  printf("DR=%d HS=%d\n", !!(GPIO_REGS->data_in & GPIO_ESP32_DR), !!(GPIO_REGS->data_in & GPIO_ESP32_HS));
  read_uart(GPIO_KEY0);//}

  printf("DR=%d HS=%d\n", !!(GPIO_REGS->data_in & GPIO_ESP32_DR), !!(GPIO_REGS->data_in & GPIO_ESP32_HS));
  ESP32_SPI_REGS->nselect = 0;
  for (int i = 0; i < 10; ++i) ESP32_SPI_REGS->data = (unsigned)cmd[i*2] | ((unsigned)cmd[i*2+1]<<8);
  ESP32_SPI_REGS->counter = 20;
  while (ESP32_SPI_REGS->counter);
  ESP32_SPI_REGS->nselect = 1;
    for (int j = 0; j < 8;++j)
  {for (int i = 0; i < 4; ++i) {
    int v = ESP32_SPI_REGS->data;
    printf("%02x %02x ", v&255, v>>8);
  }
  putchar('\n');}
  printf("DR=%d HS=%d\n", !!(GPIO_REGS->data_in & GPIO_ESP32_DR), !!(GPIO_REGS->data_in & GPIO_ESP32_HS));

  read_uart(GPIO_KEY1);

  printf("DR=%d HS=%d\n", !!(GPIO_REGS->data_in & GPIO_ESP32_DR), !!(GPIO_REGS->data_in & GPIO_ESP32_HS));
  ESP32_SPI_REGS->nselect = 0;
  for (int i = 0; i < 32; ++i) ESP32_SPI_REGS->data = 0;
  ESP32_SPI_REGS->counter = 64;
  while (ESP32_SPI_REGS->counter);
  ESP32_SPI_REGS->nselect = 1;
    for (int j = 0; j < 8;++j)
  {for (int i = 0; i < 4; ++i) {
    int v = ESP32_SPI_REGS->data;
    printf("%02x %02x ", v&255, v>>8);
  }
  putchar('\n');}
  printf("DR=%d HS=%d\n", !!(GPIO_REGS->data_in & GPIO_ESP32_DR), !!(GPIO_REGS->data_in & GPIO_ESP32_HS));
  read_uart(GPIO_KEY0);
#endif
  GPIO_REGS->data_clear = GPIO_ESP32_SPI_BOOT | GPIO_ESP32_EN;
  printf("UART bridge closed\n");
  return 0;
}

#include <endeavour2/bios.h>

void readline_impl(const char* prompt, char* buffer, unsigned max_size) {
  printf(prompt);
  int c;
  int size = 0;
  while (1) {
    c = UART_REGS->rx;
    if (c < 0) continue;
    if (c > 255) {
      printf("\nUART error %d\n", c >> 8);
      *buffer = 0;
      return;
    }
    if (c == '\r') c = '\n';
    if (c == '\b' && size > 0) {
      size--;
      putchar('\b');
    } else if (c == '\n') {
      putchar('\n');
      buffer[size] = 0;
      return;
    }
    if (c < 32 || size == max_size - 1) continue;
    putchar(c);
    buffer[size++] = c;
  }
}

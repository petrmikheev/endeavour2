#include <endeavour2/raw/defs.h>

#include "bios_internal.h"

static void uart_putc(char c) {
  while (UART_REGS->tx < 0);
  UART_REGS->tx = c;
}

void putchar(char c) {
  if (c == '\b') {
    uart_putc('\b');
    uart_putc(' ');
  }
  uart_putc(c);
  display_putchar(c);
}

static void print_number(unsigned v, unsigned base, int width, int leading_zero, int sign) {
  char buf[32];
  if (leading_zero && width > 32) width = 32;
  int neg = sign && (int)v < 0;
  if (neg) {
    v = -v;
    if (width > 0) width--;
    else if (width < 0) width++;
  }
  char* ptr = buf;
  while (v > 0 || width > 0 || ptr == buf) {
    *ptr++ = v % base;
    v /= base;
    if (width > 0) width--;
  }
  if (!leading_zero) {
    while (ptr[-1] == 0 && ptr > buf+1) {
      putchar(' ');
      ptr--;
    }
  }
  if (neg) putchar('-');
  while (ptr != buf) { putchar("0123456789ABCDEF"[*--ptr]); width++; }
  while (width++ < 0) putchar(' ');
}

static unsigned parse_uint(const char** str, int base) {
  unsigned res = 0;
  while (1) {
    char c = **str;
    if (c >= '0' && c <= '9') {
      res = res * base + (c - '0');
    } else if (base==16 && c >= 'a' && c <= 'f') {
      res = res * base + (c - 'a' + 10);
    } else if (base==16 && c >= 'A' && c <= 'F') {
      res = res * base + (c - 'A' + 10);
    } else {
      return res;
    }
    (*str)++;
  }
}

static int parse_int(const char** str, int base) {
  int neg = **str == '-';
  if (neg) (*str)++;
  int res = parse_uint(str, base);
  return neg ? -res : res;
}

unsigned long strlen(const char* str) {
  int res = 0;
  while (*str++) res++;
  return res;
}

int strcmp(const char* s1, const char* s2) {
  while (*s1 == *s2 && *s1 != 0) {
    s1++;
    s2++;
  }
  if (*s1 > *s2)
    return 1;
  else if (*s1 < *s2)
    return -1;
  else
    return 0;
}

void printf_impl(const char* fmt, unsigned* arg) {
  char c;
  while ((c = *fmt++) != 0) {
    if (c == '\t') {
      for (int i = 0; i < 4; ++i) putchar(' ');
      continue;
    }
    if (c != '%') {
      putchar(c);
      continue;
    }
    char f = *fmt;
    if (f == '%') {
      fmt++;
      putchar('%');
      continue;
    }
    int leading_zero = f == '0';
    int width = parse_int(&fmt, 10);
    f = *fmt++;
    if (f == 'l') f = *fmt++;
    if (f == 0) break;
    unsigned a = *arg;
    switch (f) {
      case 'B': print_number(a, 2, width, leading_zero, 0); break;  // non-standard
      case 'p':
      case 'x':
      case 'X': print_number(a, 16, width, leading_zero, 0); break;
      case 'o': print_number(a, 8, width, leading_zero, 0); break;
      case 'c': putchar(a); break;
      case 'd':
      case 'i':
        print_number(a, 10, width, leading_zero, 1);
        break;
      case 'u':
        print_number(a, 10, width, leading_zero, 0);
        break;
      case 's': {
        const char* s = (const char*)(long)a;
        int len = strlen(s);
        while (width > len) { putchar(' '); width--; }
        while (*s) putchar(*s++);
        while (-width > len) { putchar(' '); width++; }
        break;
      }
      default:
        putchar('%');
        putchar(f);
    }
    arg++;
  }
}

int sscanf_impl(const char* str, const char* fmt, unsigned** args) {
  unsigned** arg = args;
  while (1) {
    char f = *fmt++;
    if (f == 0 || *str == 0) break;
    if (f == '%') {
      char s = *fmt;
      unsigned width = -1;
      if (s >= '0' && s <= '9') {
        width = parse_uint(&fmt, 10);
        s = *fmt;
      }
      fmt++;
      if (s == '%') {
        if ('%' != *str++)
          break;
        else
          continue;
      }
      while (*str == ' ' || *str == '\t') str++;
      if (s == 's') {
        char* dst = (char*)(*arg++);
        while (*str && *str != ' ' && *str != '\t' && width-- > 0) *dst++ = *str++;
        *dst = 0;
        continue;
      }
      if (s == 'l') s = *fmt++;
      int base;
      int neg = 0;
      switch (s) {
        case 'o': base = 8; break;
        case 'i':
          if (str[0] == '0') {
            if (str[1] == 'x') {
              str += 2;
              base = 16;
            } else {
              base = 8;
            }
            break;
          }
          // no break
        case 'd': if (*str == '-') {
          str++;
          neg = 1;
        }
        case 'u': base = 10; break;
        default: base = 16;
      }
      int r = parse_uint(&str, base);
      *(*arg++) = neg ? -r : r;
    } else {
      if (f != *str++) break;
    }
  }
  return arg - args;
}

unsigned crc32(const char* data, int size) {
  unsigned table[16];
  for (int i = 0; i < 16; ++i) {
    unsigned c = i;
    unsigned crc = 0;
    for (int j = 0; j < 4; ++j) {
      int b = (c ^ crc) & 1;
      crc >>= 1;
      if (b) crc = crc ^ 0xedb88320;
      c >>= 1;
    }
    table[i] = crc;
  }

  unsigned ncrc = 0xffffffff;
  for (int i = 0; i < size; ++i) {
    unsigned c = data[i];
    ncrc = (ncrc>>4) ^ table[(c^ncrc) & 0xf];
    c >>= 4;
    ncrc = (ncrc>>4) ^ table[(c^ncrc) & 0xf];
  }
  return ~ncrc;
}

void uart_flush() {
  while (1) {
    wait(1000);
    if (UART_REGS->rx < 0) {
      UART_REGS->rx = 0;
      return;
    }
    while (UART_REGS->rx >= 0);
  }
}

int read_uart(char* dst, int size, int divisor) {
  unsigned uart_cfg = UART_REGS->cfg;
  if (divisor >= 0) {
    UART_REGS->cfg = (UART_REGS->cfg & 0xffff0000) | divisor;
  }
  for (int i = 0; i < size; ++i) {
    int x;
    do { x = UART_REGS->rx; } while (x < 0);
    if (x < 0x100) {
      *(dst++) = (char)x;
    } else {
      uart_flush();
      if (divisor >= 0) {
        UART_REGS->cfg = uart_cfg;
        wait(100000);
      }
      printf("Error %d at pos %d\n", x>>8, i);
      return 1;
    }
  }
  if (divisor >= 0) {
    UART_REGS->cfg = uart_cfg;
    wait(100000);
  }
  return 0;
}

volatile struct HartCfg* get_hart_cfg(unsigned hartid) { return hartid > 2 ? 0 : &hart_cfg[hartid]; }

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

int i2c_set_reg(int addr, char reg, char value) {
  char buf[2] = {reg, value};
  return i2c_write(addr, 2, buf);
}

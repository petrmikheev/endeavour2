#include <endeavour2/defs.h>

static void uart_putc(char c) {
  while (UART_REGS->tx < 0);
  UART_REGS->tx = c;
}

void putchar_impl(char c) {
  uart_putc(c);
}

static void print_number(unsigned v, unsigned base, int width) {
  char buf[32];
  char* ptr = buf;
  while (v > 0 || width > 0) {
    *ptr++ = v % base;
    v /= base;
    width--;
  }
  while (ptr != buf) putchar_impl("0123456789ABCDEF"[*--ptr]);
}

void printf_impl(const char* fmt, unsigned a1, unsigned a2, unsigned a3, unsigned a4, unsigned a5, unsigned a6, unsigned a7)
{
  unsigned args[7] = {a1, a2, a3, a4, a5, a6, a7};
  unsigned* arg = args;
  char c;
  while ((c = *fmt++) != 0) {
    if (c == '\t') {
      for (int i = 0; i < 4; ++i) putchar_impl(' ');
      continue;
    }
    if (c != '%') {
      putchar_impl(c);
      continue;
    }
    char f = *fmt++;
    if (f == '%') {
      putchar_impl('%');
      continue;
    }
    int width = 1;
    while (f >= '0' && f <= '9') {
      width = f - '0';
      f = *fmt++;
    }
    if (f == 0) break;
    unsigned a = *arg;
    switch (f) {
      case 'B': print_number(a, 2, width * 8); break;
      case 'p':
      case 'x':
      case 'X': print_number(a, 16, width); break;
      case 'o': print_number(a, 8, width); break;
      case 'c': putchar_impl(a); break;
      case 'd':
      case 'i':
        if ((int)a < 0) {
          putchar_impl('-');
          a = -a;
        }
        // no break
      case 'u':
        print_number(a, 10, width);
        break;
      case 's': {
        const char* s = (const char*)(long)a;
        while (*s) putchar_impl(*s++);
        break;
      }
      default:
        putchar_impl('%');
        putchar_impl(f);
    }
    arg++;
  }
}

static unsigned parse_uint(const char** str, int base) {
  unsigned res = 0;
  while (1) {
    char c = **str;
    if (c >= '0' && c <= '9') {
      res = res * base + (c - '0');
    } else if (c >= 'a' && c <= 'f') {
      res = res * base + (c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      res = res * base + (c - 'A' + 10);
    } else {
      return res;
    }
    (*str)++;
  }
  return res;
}

int sscanf_impl(const char* str, const char* fmt, unsigned* a1, unsigned* a2, unsigned* a3, unsigned* a4, unsigned* a5, unsigned* a6) {
  unsigned* args[7] = {a1, a2, a3, a4, a5, a6};
  unsigned** arg = args;
  while (1) {
    char f = *fmt++;
    if (f == 0) break;
    if (f == '%') {
      char s = *fmt++;
      if (s == '%') {
        if ('%' != *str++)
          break;
        else
          continue;
      }
      int base;
      int neg = 0;
      switch (*fmt++) {
        case 'o': base = 8; break;
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

unsigned crc32_impl(const char* data, int size) {
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

static const unsigned short beep_data[8] = {0x800, 0xda8, 0xfff, 0xda8, 0x800, 0x257, 0x0, 0x257};

void beep_impl(unsigned volume, int periods) {
  for (int i = 0; i < periods; ++i) {
    while (AUDIO_REGS->stream < 8);
    for (int j = 0; j < 8; ++j) {
      unsigned v = (((unsigned)beep_data[j] * volume) >> 8) & 0xfff;
      AUDIO_REGS->stream = v | (v << 16);
    }
  }
}

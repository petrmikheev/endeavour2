#include <endeavour2/bios.h>

#include "bios_internal.h"

struct RealTimeClockBCD {
  unsigned char second;
  unsigned char minute;
  unsigned char hour;
  unsigned char day;
  unsigned char wday;
  unsigned char month;
  unsigned char year;
};

static int fromBCD(unsigned char v) { return (v >> 4) * 10 + (v & 15); }
static unsigned char toBCD(int v) { return ((v/10)<<4) | (v%10); }

static int read_time(struct RealTimeClockBCD* t) {
  char start_reg = 4;
  int err = i2c_write(I2C_ADDR_PCF85063A, 1, &start_reg);
  err |= i2c_read(I2C_ADDR_PCF85063A, 7, (char*)t);
  t->second &= 0x7f;
  return err;
}

static int write_time(const struct RealTimeClockBCD* t) {
  char data[8];
  data[0] = 4;
  for (int i = 0; i < 7; ++i) data[i + 1] = ((char*)t)[i];
  return i2c_write(I2C_ADDR_PCF85063A, 8, data);
}

static const char* const week_days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "***"};
static const char* const months[] = {"***", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static void print_time(const struct RealTimeClockBCD* t) {
  int wday = t->wday;
  if (wday > 6) wday = 7;
  int month = fromBCD(t->month);
  if (month > 12) month = 0;
  printf("%s %2u %s %02u:%02u:%02u 20%02u\n", week_days[wday], fromBCD(t->day), months[month], fromBCD(t->hour), fromBCD(t->minute), fromBCD(t->second), fromBCD(t->year));
}

int cmd_date(const char* args) {
  struct RealTimeClockBCD t;
  if (*args == 0) {
    read_time(&t);
    print_time(&t);
    return 0;
  }
  char wday[16], month[16];
  int day, hour, minute, second, year;
  sscanf(args, "%s", wday);
  if (sscanf(args+4, "%u %s %u:%u:%u 20%u", &day, month, &hour, &minute, &second, &year) != 6) {
    printf("Invalid date format\n");
    return 0;
  }
  t.day = toBCD(day);
  t.hour = toBCD(hour);
  t.minute = toBCD(minute);
  t.second = toBCD(second);
  t.year = toBCD(year);
  t.wday = t.month = 1;
  for (int i = 0; i <= 6; ++i) {
    if (strcmp(wday, week_days[i]) == 0) t.wday = i;
  }
  for (int i = 1; i <= 12; ++i) {
    if (strcmp(month, months[i]) == 0) t.month = toBCD(i);
  }
  print_time(&t);
  write_time(&t);
  return 0;
}

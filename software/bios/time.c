#include <endeavour2/raw/defs.h>

#include "bios_internal.h"

void wait(unsigned t) {
  unsigned start = time_100nsec();
  while (1) {
    if ((time_100nsec() - start) > t) return;
  }
}

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

static const unsigned char days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static const char* const week_days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "***"};
static const char* const months[] = {"***", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

unsigned get_seconds_since_2000() {
  struct RealTimeClockBCD ts;
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (read_time(&ts) == 0) break;
    wait(10000);
  }
  unsigned year = fromBCD(ts.year);
  int month = fromBCD(ts.month) - 1;
  unsigned days = year * 365 + (year + 3) / 4 + fromBCD(ts.day) - 1;
  for (int i = 0; i < month; ++i) days += days_in_month[i];
  if ((year & 3) == 0 && month > 1) days++;
  return days * (24*3600) + fromBCD(ts.hour) * 3600 + fromBCD(ts.minute) * 60 + fromBCD(ts.second);
}

void set_seconds_since_2000(unsigned seconds) {
  struct RealTimeClockBCD ts;

  ts.second = toBCD(seconds % 60);
  ts.minute = toBCD((seconds / 60) % 60);
  ts.hour = toBCD((seconds / 3600) % 24);

  unsigned day = seconds / (24*3600);
  ts.wday = (day + 6) % 7;

  unsigned year = (day / (365 * 3 + 366)) * 4;
  day %= (365 * 3 + 366);
  if (day >= 366) {
    year += (day - 366) / 365 + 1;
    day = (day - 366) % 365;
  }

  unsigned month = 0;
  if ((year & 3) == 0 && day == 59) {
    month = 1;
    day = 29;
  } else {
    if ((year & 3) == 0 && day > 59) day--;
    while (day >= days_in_month[month]) {
      day -= days_in_month[month++];
    }
  }

  ts.year = toBCD(year);
  ts.day = toBCD(day + 1);
  ts.month = toBCD(month + 1);

  for (int attempt = 0; attempt < 3; ++attempt) {
    if (write_time(&ts) == 0) break;
    wait(10000);
  }
}

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
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (read_time(&t) == 0) break;
      wait(10000);
    }
    print_time(&t);
    return 0;
  }
  char wday[4], month[4];
  int day, hour, minute, second, year;
  if (sscanf(args, "%3s %u %3s %u:%u:%u 20%u", wday, &day, month, &hour, &minute, &second, &year) != 7) {
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
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (write_time(&t) == 0) break;
    wait(10000);
  }
  return 0;
}

#include <endeavour2/defs.h>

#include "bios_internal.h"

#define CMD_OK           0
#define CMD_NO_COMMAND   1
#define CMD_INVALID_ARGS 2
#define CMD_FAILED       3

static int cmd_write(const char* args) {
  unsigned long addr;
  unsigned val;
  if (sscanf(args, "%lx %x", &addr, &val) != 2) return CMD_INVALID_ARGS;
  *(volatile unsigned*)addr = val;
  return CMD_OK;
}

static int cmd_read(const char* args) {
  unsigned long addr;
  if (sscanf(args, "%lx", &addr) != 1) return CMD_INVALID_ARGS;
  printf("%08x\n", *(volatile unsigned*)addr);
  return CMD_OK;
}

static int cmd_memtest(const char* args) {
  unsigned iter_count = 1;
  unsigned seed = 0;
  sscanf(args, "%u %x", &iter_count, &seed);
  full_memtest(iter_count, seed);
  return CMD_OK;
}

static int cmd_benchmark() {
  run_benchmarks();
  return CMD_OK;
}

static int cmd_uart(const char* args) {
  unsigned long addr;
  unsigned size;
  if (sscanf(args, "%lx %u", &addr, &size) != 2) return CMD_INVALID_ARGS;
  return read_uart((char*)addr, size, UART_BAUD_RATE(12000000)) == 0 ? CMD_OK : CMD_FAILED;
  return CMD_OK;
}

static int cmd_run(const char* args) {
  unsigned long addr;  // TODO load from file
  if (sscanf(args, "%lx", &addr) != 1) return CMD_INVALID_ARGS;
  run_binary((void*)addr, 0, 0);
  return CMD_OK;
}

static int cmd_crc32(const char* args) {
  unsigned long addr;
  unsigned size, expected = 0;
  if (sscanf(args, "%lx %u %x", &addr, &size, &expected) < 2) return CMD_INVALID_ARGS;
  unsigned crc = crc32((char*)addr, size);
  printf("%08x", crc);
  if (expected) {
    printf(crc == expected ? " OK" : " ERROR");
  }
  putchar('\n');
  return (expected && crc != expected) ? CMD_FAILED : CMD_OK;
}

static int cmd_beep(const char* args) {
  unsigned time_ms, freq = 300, volume = 6;
  if (sscanf(args, "%u %u %u", &time_ms, &freq, &volume) == 0) return CMD_INVALID_ARGS;
  beep(time_ms, freq, volume);
  return CMD_OK;
}

static int cmd_sound(const char* args) {
  unsigned long addr;  // TODO load from file
  int volume = 6;
  if (sscanf(args, "%lx %d", &addr, &volume) == 0) return CMD_INVALID_ARGS;
  playWav((void*)addr, volume);
  return CMD_OK;
}

static int cmd_display(const char* args) {
  char modestr[16];
  struct VideoMode mode;
  int count = sscanf(args, "%15s %u %u %u %u %u %u %u %u %u", modestr, &mode.clock,
         &mode.hResolution, &mode.hSyncStart, &mode.hSyncEnd, &mode.hTotal,
         &mode.vResolution, &mode.vSyncStart, &mode.vSyncEnd, &mode.vTotal);
  if (count == 0) return CMD_INVALID_ARGS;
  if (strcmp(modestr, "off") == 0) {
    set_video_mode(0, 0);
  } else if (strcmp(modestr, "640x480") == 0) {
    set_video_mode(VIDEO_MODE_640x480, 0);
  } else if (strcmp(modestr, "800x600") == 0) {
    set_video_mode(VIDEO_MODE_800x600, 0);
  } else if (strcmp(modestr, "1024x768") == 0) {
    set_video_mode(VIDEO_MODE_1024x768, 0);
  } else if (strcmp(modestr, "1280x720") == 0) {
    set_video_mode(VIDEO_MODE_1280x720, 0);
  } else if (strcmp(modestr, "1920x1080") == 0 || strcmp(modestr, "1920x1080_50") == 0) {
    set_video_mode(VIDEO_MODE_1920x1080, 0);
  } else if (strcmp(modestr, "1920x1080_25") == 0) {
    set_video_mode(VIDEO_MODE_1920x1080_25, 0);
  } else if (strcmp(modestr, "custom") == 0 && count == 10) {
    set_video_mode(VIDEO_MODE_CUSTOM, &mode);
  } else {
    return CMD_INVALID_ARGS;
  }
  return CMD_OK;
}

int cmd_date(const char* args);  // implemented in time.c

static int cmd_no_impl() {
  printf("Not implemented\n");
  return CMD_FAILED;
}

struct ConsoleCommand {
  int (*handler)(const char* args);
  const char* name;
  const char* arg_spec;
  const char* description;
};

static int cmd_help();

static const struct ConsoleCommand commands[] = {
  {cmd_help,       "help",        "",                        "show help"},
  {cmd_write,      "W",           "addr val",                "write 4 bytes (hex value) to given address (hex)"},
  {cmd_read,       "R",           "addr",                    "load 4 bytes from given address (hex value)"},
  {cmd_memtest,    "memtest",     "[iter_count] [seed]",     "run full memtest"},
  {cmd_benchmark,  "benchmark",   "",                        "run benchmarks"},
  {cmd_uart,       "uart",        "addr size",               "receive size (decimal) bytes via UART with baud rate 12 MHz"},
  {cmd_crc32,      "crc32",       "addr size [expected]",    "calculate crc32 of data in RAM"},
  {cmd_date,       "date",        "[new_date]",              "print or set date and time"},
  {cmd_display,    "display",     "WIDTHxHEIGHT",            "set display resolution; supports custom mode, e.g.: \"display custom 25175000 640 656 752 800 480 490 492 525\""},
  /*{cmd_no_impl,    "textstyle",   "fg bg",                   "set text style; fg and bg range is [0:15]"},
  {cmd_no_impl,    "disk",        "sd/sd1/sd2/sd3/sd4",      "select sdcard partition for file access (only EXT2 supported)"},
  {cmd_no_impl,    "ls",          "path",                    "show files"},
  {cmd_no_impl,    "cat",         "path",                    "print text file"},
  {cmd_no_impl,    "eval",        "path",                    "run commands from given text file"},
  {cmd_no_impl,    "load",        "path [addr]",             "load file content to RAM"},
  {cmd_no_impl,    "wallpaper",   "[addr/path]",             "set or remove wallpaper (only BMP supported)"},*/
  {cmd_beep,       "beep",        "time_ms [freq] [volume]", "beep sound"},
  {cmd_sound,      "sound",       "addr/path [volume]",      "play WAV file"},
  {cmd_run,        "run",         "addr/path",               "run binary"},
  /*{cmd_no_impl,    "device_tree", "addr/path",               "specify DTB file"},
  {cmd_no_impl,    "kernel_options", "*",                    "override kernel options in device tree"},
  {cmd_no_impl,    "boot",        "addr/path",               "start kernel in supervisor mode"},*/
  {0}
};

static int cmd_help() {
  int max_spec_len = 0;
  for (const struct ConsoleCommand* cmd = commands; cmd->handler; cmd++) {
    int len = strlen(cmd->name) + 1 + strlen(cmd->arg_spec);
    if (len > max_spec_len) max_spec_len = len;
  }
  for (const struct ConsoleCommand* cmd = commands; cmd->handler; cmd++) {
    int len = strlen(cmd->name) + 1 + strlen(cmd->arg_spec);
    printf("\t%s %s", cmd->name, cmd->arg_spec);
    for (int i = max_spec_len - len; i > 0; i--) putchar(' ');
    printf(" - %s\n", cmd->description);
  }
  return 0;
}

static void print_available_commands() {
  printf("Available commands: %s", commands[0].name);
  for (const struct ConsoleCommand* cmd = &commands[1]; cmd->handler; cmd++) {
    printf(", %s", cmd->name);
  }
  putchar('\n');
}

static int run_command(const char* cmd_line) {
  for (const struct ConsoleCommand* cmd = commands; cmd->handler; cmd++) {
    const char* p1 = cmd->name;
    const char* p2 = cmd_line;
    while (*p1 && *p1 == *p2) { p1++; p2++; }
    if (*p1 || (*p2 && *p2 != ' ' && *p2 != '\n')) continue;
    while (*p2 == ' ') p2++;
    int res = cmd->handler(p2);
    if (res == CMD_INVALID_ARGS) {
      printf("Invalid args\nUsage: %s %s\t- %s\n", cmd->name, cmd->arg_spec, cmd->description);
    }
    return res;
  }
  printf("Unknown command\n");
  print_available_commands();
  return CMD_NO_COMMAND;
}

void run_console() {
  printf("\nStarting BIOS console\n");
  print_available_commands();
  while (1) {
    #define CMD_BUF_SIZE 120
    char cmd[CMD_BUF_SIZE];
    readline("> ", cmd, CMD_BUF_SIZE);
    if (*cmd == 0) continue;
    int res = run_command(cmd);
    if (res != CMD_OK) uart_flush();
  }
}

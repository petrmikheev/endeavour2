#include <endeavour2/raw/defs.h>

#include "bios_internal.h"
#include "ext2.h"

#define CMD_OK           0
#define CMD_NO_COMMAND   1
#define CMD_INVALID_ARGS 2
#define CMD_FAILED       3

#define TMP_BUF_1MB ((char*)(RAM_BASE +   0x8000))
#define EVAL_BUF    ((char*)(RAM_BASE + 0x188000))
#define DTB_BUF     ((char*)(RAM_BASE + 0x189000))
#define MAIN_BUF    ((char*)(RAM_BASE + 0x800000))

static int load_file(const char* path, void* addr, unsigned max_size) {
  if (!is_ext2_reader_initialized()) {
    printf("No selected EXT2 fs\n");
    return -1;
  }
  const struct Inode* inode = find_inode(path);
  if (!inode || !is_regular_file(inode)) {
    printf("File not found\n");
    return -1;
  }
  if (read_file(inode, addr, inode->size_lo) != inode->size_lo) {
    printf("IO error\n");
    return -1;
  }
  return inode->size_lo;
}

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
  unsigned long addr;
  if (args[0] != '8') {
    if (load_file(args, TMP_BUF_1MB, 0x100000) <= 0) return CMD_FAILED;
    addr = (unsigned long)TMP_BUF_1MB;
  } else {
    if (sscanf(args, "%lx", &addr) != 1) return CMD_INVALID_ARGS;
  }
  run_binary((void*)addr, 0, 0);
  return CMD_OK;
}

struct dtb_header {
    unsigned magic;
    unsigned totalsize;
    unsigned off_dt_struct;
    unsigned off_dt_strings;
    unsigned off_mem_rsvmap;
    unsigned version;
    unsigned last_comp_version;
    unsigned boot_cpuid_phys;
    unsigned size_dt_strings;
    unsigned size_dt_struct;
};

static int cmd_dtb(const char* args) {
  unsigned long addr;
  if (args[0] != '8') {
    if (load_file(args, DTB_BUF, 0x10000) <= 0) return CMD_FAILED;
  } else {
    if (sscanf(args, "%lx", &addr) != 1) return CMD_INVALID_ARGS;
    unsigned size = ((struct dtb_header*)addr)->totalsize;
    size = (size >> 24) | ((size >> 8) & 0xff00);
    for (int i = 0; i < size / 4; ++i) {
      ((unsigned*)DTB_BUF)[i] = ((unsigned*)addr)[i];
    }
  }
  if (((struct dtb_header*)DTB_BUF)->magic != 0xedfe0dd0) {
    printf("Invalid DTB file\n");
    return CMD_FAILED;
  }
  return CMD_OK;
}

static int cmd_load(const char* args) {
  unsigned long addr;
  if (args[0] != '8') return CMD_INVALID_ARGS;
  if (sscanf(args, "%lx", &addr) != 1) return CMD_INVALID_ARGS;
  while (*args && *args != ' ') args++;
  while (*args == ' ') args++;
  if (load_file(args, (void*)addr, -1) <= 0) return CMD_FAILED;
  return CMD_OK;
}

static int eval_in_progress = 0;

int cmd_eval(const char* args) {
  if (eval_in_progress) {
    printf("Recursive eval not allowed");
    return CMD_FAILED;
  }
  int size = load_file(args, EVAL_BUF, 0xfff);
  if (size <= 0) return CMD_FAILED;
  eval_in_progress = 1;
  char* buf = EVAL_BUF;
  buf[0xfff] = 0;
  while (*buf) {
    int len = 0;
    while (buf[len] && buf[len] != '\n') len++;
    char* cmd = buf;
    buf += len;
    if (*buf) buf++;
    cmd[len] = 0;
    if (cmd[0] == 0 || cmd[0] == '#') continue;
    printf("$ %s\n", cmd);
    if (run_command(cmd) != CMD_OK) {
      eval_in_progress = 0;
      return CMD_FAILED;
    }
  }
  eval_in_progress = 0;
  return CMD_OK;
}

static int cmd_boot(const char* args) {
  unsigned long addr;
  if (sscanf(args, "%lx", &addr) != 1) return CMD_INVALID_ARGS;
  run_in_supervisor_mode((void*)addr, (unsigned long)DTB_BUF);
  return CMD_OK;  // noreturn
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
  unsigned long addr;
  int volume = 6;
  if (sscanf(args, "-v %d", &volume) == 1) {
    args += 3;
    while (*args == ' ' || (*args >= '0' && *args <= '9')) args++;
  }
  if (args[0] != '8') {
    if (load_file(args, MAIN_BUF, 0x100000) <= 0) return CMD_FAILED;
    addr = (unsigned long)MAIN_BUF;
  } else if (sscanf(args, "%lx", &addr) == 0) return CMD_INVALID_ARGS;
  playWav((void*)addr, volume);
  return CMD_OK;
}

static int cmd_wallpaper(const char* args) {
  if (*args == 0 || strcmp(args, "off") == 0) {
    VIDEO_REGS->cfg &= ~VIDEO_GRAPHIC_ON;
    return CMD_OK;
  }
  unsigned long addr;
  int size = 0;
  if (args[0] != '8') {
    size = load_file(args, MAIN_BUF, 0x100000);
    if (size <= 30) return CMD_FAILED;
    addr = (unsigned long)MAIN_BUF;
  } else if (sscanf(args, "%lx", &addr) == 0) return CMD_INVALID_ARGS;

  const char* wallpaper_bmp = (char*)addr;
  unsigned short bitmap_offset = *(unsigned short*)(wallpaper_bmp + 10);
  unsigned short width = *(unsigned short*)(wallpaper_bmp + 18);
  unsigned short height = *(unsigned short*)(wallpaper_bmp + 22);
  unsigned short bits_per_pixel = *(unsigned short*)(wallpaper_bmp + 28);
  unsigned bytes_per_line = (width * 2 + 3) & ~3;
  if ((size && size != bitmap_offset + bytes_per_line * height) || bits_per_pixel != 16) {
    printf("Unsupported image. Expected RGB565 BMP without color table.\n");
    return CMD_FAILED;
  }
  char* frame_buffer  = VIDEO_REGS->graphicAddr;
  for (int j = 0; j < height; ++j) {
    const char* src = wallpaper_bmp + bitmap_offset + (height-j-1) * bytes_per_line;
    char* dst = frame_buffer + j * GRAPHIC_LINE_SIZE;
    for (int i = 0; i < bytes_per_line; ++i) dst[i] = src[i];
  }
  VIDEO_REGS->cfg |= VIDEO_GRAPHIC_ON;
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

static int cmd_disk(const char* args) {
  int res;
  if (strcmp(args, "sd") == 0) {
    res = select_ext2_fs(0);
  } else if (strcmp(args, "sd1") == 0) {
    res = select_ext2_fs(1);
  } else if (strcmp(args, "sd2") == 0) {
    res = select_ext2_fs(2);
  } else if (strcmp(args, "sd3") == 0) {
    res = select_ext2_fs(3);
  } else if (strcmp(args, "sd4") == 0) {
    res = select_ext2_fs(4);
  } else {
    return CMD_INVALID_ARGS;
  }
  return res < 0 ? CMD_FAILED : CMD_OK;
}

static int cmd_textstyle(const char* args) {
  int fg = 0xffffff40, bg = 0;
  sscanf(args, "%x %x", &fg, &bg);
  VIDEO_REGS->regIndex = VIDEO_COLORMAP(0);
  VIDEO_REGS->regValue = bg;
  VIDEO_REGS->regIndex = VIDEO_COLORMAP(15);
  VIDEO_REGS->regValue = fg;
  return CMD_OK;
}

static int cmd_ls(const char* args) {
  const struct Inode* inode = find_inode(args);
  if (!inode) {
    printf("Not found\n");
    return CMD_FAILED;
  }
  if (is_dir(inode)) {
    print_dir(inode);
  } else if (is_regular_file(inode)) {
    printf("regular file uid=%u gid=%u size=%u\n", inode->uid, inode->gid, inode->size_lo);
  } else {
    printf("special file uid=%u gid=%u mode=%08x flags=%08x\n", inode->uid, inode->gid, inode->mode, inode->flags);
  }
  return CMD_OK;
}

static int cmd_cat(const char* args) {
  int size = load_file(args, TMP_BUF_1MB, 4096);
  if (size >= 0) {
    printf("***\n");
    for (int i = 0; i < size; ++i) putchar(TMP_BUF_1MB[i]);
    if (size > 0 && TMP_BUF_1MB[size-1] != '\n') putchar('\n');
    printf(size == 4096 ? "*** (first 4KB)\n" : "***\n");
    return CMD_OK;
  } else {
    return CMD_FAILED;
  }
}

int cmd_date(const char* args);  // implemented in time.c

static int wait_spi_busy() {
  unsigned start = time_100nsec();
  while (1) {
    wait(100);
    SPI_FLASH_REGS->data = 0x05000000;
    SPI_FLASH_REGS->cnt = (1<<16) | 1;
    while (!SPI_FLASH_REGS->readHasData);
    unsigned status1 = SPI_FLASH_REGS->data & 0xff;
    if (!(status1 & 1)) return 0;
    if (time_100nsec() - start > 100000000) {
      printf("Time limit. Status1: %02x\n", status1);
      return -1;
    }
  }
}

static int cmd_flash_bios(const char* args) {
  unsigned long addr;
  unsigned expected_crc;
  if (sscanf(args, "%lx %x", &addr, &expected_crc) != 2) return CMD_INVALID_ARGS;
  if (crc32((void*)addr, BIOS_SIZE) != expected_crc) {
    printf("Incorrect CRC\n");
    return CMD_FAILED;
  }
  unsigned* data = (void*)addr;
  if (data[BIOS_SIZE / 4 - 1] != BIOS_MAGIC) {
    printf("Invalid BIOS image, no magic number\n");
    return CMD_FAILED;
  }
  SPI_FLASH_REGS->data = 0xAB<<24;  // power up
  SPI_FLASH_REGS->cnt = 1<<16;
  wait(100);
  printf("Erasing old data...\n");
  SPI_FLASH_REGS->data = 0x06<<24; // write enable
  SPI_FLASH_REGS->cnt = 1<<16;
  wait(20);
  SPI_FLASH_REGS->data = (0x52<<24) | ((16<<20) - BIOS_SIZE); // erase block
  SPI_FLASH_REGS->cnt = 4<<16;
  if (wait_spi_busy() != 0) return CMD_FAILED;
  printf("Writing new data...\n");
  for (int i = 0; i < BIOS_SIZE / 4; i += 256 / 4) {
    SPI_FLASH_REGS->data = 0x06<<24; // write enable
    SPI_FLASH_REGS->cnt = (1<<16);
    wait(20);
    SPI_FLASH_REGS->data = (0x02<<24) | ((16<<20) - BIOS_SIZE) | (i<<2);
    SPI_FLASH_REGS->cnt = 260<<16;
    for (int j = 0; j < 256 / 4; ++j) {
      while (SPI_FLASH_REGS->writeHasData);
      SPI_FLASH_REGS->data = *data++;
    }
    if (wait_spi_busy() != 0) return CMD_FAILED;
  }
  printf("Verification...\n");
  data = (void*)(RAM_BASE + BIOS_SIZE);
  SPI_FLASH_REGS->data = 0x03000000 | ((16<<20) - BIOS_SIZE);
  SPI_FLASH_REGS->cnt = (4<<16) | BIOS_SIZE;
  for (int i = 0; i < BIOS_SIZE / 4; ++i) {
    while (!SPI_FLASH_REGS->readHasData);
    data[i] = SPI_FLASH_REGS->data;
  }
  if (crc32((void*)data, BIOS_SIZE) != expected_crc) {
    printf("CRC check failed\n");
    return CMD_FAILED;
  }
  printf("DONE\n");
  return CMD_OK;
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
  {cmd_flash_bios, "flash_bios",  "addr crc32",              "write BIOS image (32 KB) from given address in RAM to SPI flash"},
  {cmd_date,       "date",        "[new_date]",              "print or set date and time"},
  {cmd_display,    "display",     "WIDTHxHEIGHT",            "set display resolution; supports custom mode, e.g.: \"display custom 25175000 640 656 752 800 480 490 492 525\""},
  {cmd_textstyle,  "textstyle",   "fg bg",                   "set text style; fg and bg are colors in hex format RRGGBBAA; alpha range is from 0 (transparent) to 64"},
  {cmd_disk,       "disk",        "sd/sd1/sd2/sd3/sd4",      "select sdcard partition for file access (only EXT2 supported)"},
  {cmd_ls,         "ls",          "path",                    "show files"},
  {cmd_cat,        "cat",         "path",                    "print text file"},
  {cmd_eval,       "eval",        "path",                    "run commands from given text file"},
  {cmd_load,       "load",        "addr path",               "load file content to RAM"},
  {cmd_wallpaper,  "wallpaper",   "addr/path/off",           "set or remove wallpaper (only RGB565 BMP supported)"},
  {cmd_beep,       "beep",        "time_ms [freq] [volume]", "beep sound"},
  {cmd_sound,      "sound",       "[-v volume] addr/path",   "play WAV file"},
  {cmd_run,        "run",         "addr/path",               "run binary"},
  {cmd_dtb,        "device_tree", "addr/path",               "specify DTB file"},
  //{cmd_no_impl,    "kernel_options", "*",                    "override kernel options in device tree"},
  {cmd_boot,       "boot",        "addr",                    "start kernel in supervisor mode"},
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
    int next_line = ((cmd - &commands[0]) & 7) == 0;
    printf(next_line ? "\n\t\t%s" : ", %s", cmd->name);
  }
  putchar('\n');
}

int run_command(const char* cmd_line) {
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
  eval_in_progress = 0;
  while (1) {
    #define CMD_BUF_SIZE 120
    char cmd[CMD_BUF_SIZE];
    readline("> ", cmd, CMD_BUF_SIZE);
    if (*cmd == 0) continue;
    int res = run_command(cmd);
    if (res != CMD_OK) uart_flush();
  }
}

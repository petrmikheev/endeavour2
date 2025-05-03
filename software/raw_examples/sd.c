#include <endeavour2/raw/bios.h>

unsigned sector = 28 * 1024 * 1024 * 2;

#define SIZE 4096
unsigned buf[SIZE];

int main() {
  for (int i = 0; i < SIZE/4; ++i) buf[i] = i;
  unsigned resw = bios_sdwrite(buf, sector, SIZE / 512);
  printf("WRITE %u\n", resw);
  for (int i = 0; i < SIZE/4; ++i) buf[i] = 0xcccca55a;
  unsigned resr = bios_sdread(buf, sector, SIZE / 512);
  printf("READ  %u\n", resr);
  for (int i = 0; i < SIZE/4; ++i) {
    if (buf[i] != i) {
      printf("ERR buf[%u] = %08x\n", i, buf[i]);
      return 1;
    }
  }
  printf("OK\n");
  return 0;
}

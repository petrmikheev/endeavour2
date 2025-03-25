#include <endeavour2/defs.h>
#include <endeavour2/bios.h>

unsigned dhrystone();

void memset_1mb_no_unroll(unsigned* dst, unsigned v);
void memset_1mb(unsigned* dst, unsigned v);
void memset_1mb_prefetch(unsigned* dst, unsigned v);

void memcpy_1mb(unsigned* dst, const unsigned* src);
void memcpy_1mb_prefetch(unsigned* restrict dst, const unsigned* restrict src);

unsigned sparse_agg_xor_1mb(const unsigned* src);
void sparse_inplace_xor_1mb(unsigned* data, unsigned v);
unsigned sparse_agg_xor_1mb_prefetch(const unsigned* src);
void sparse_inplace_xor_1mb_prefetch(unsigned* data, unsigned v);

static void print_bench_res(const char* bench_name, unsigned start_time) {
  unsigned time = time_100nsec() - start_time;
  unsigned speed = 1000000000 / time;
  printf("\t%-35s: %u.%02u MB/s\n", bench_name, speed/100, speed%100);
}

static void test_memset(const unsigned* data, unsigned v) {
  for (int i = 0; i < 1024*1024/4; ++i) {
    if (data[i] != v) {
      printf("[ERROR] incorrect result\n");
      break;
    }
  }
}

static void test_memcpy_fill(unsigned* data) {
  for (int i = 0; i < 1024*1024/4; ++i) {
    data[i] = i;
  }
}

static void test_memcpy_check(const unsigned* data) {
  for (int i = 0; i < 1024*1024/4; ++i) {
    if (data[i] != i) {
      printf("[ERROR] incorrect result\n");
      break;
    }
  }
}

void bench() {
  unsigned mDMIPS_MHz = dhrystone();
  printf("\nDhrystone Benchmark\t: %u.%03u DMIPS/MHz\n", mDMIPS_MHz / 1000, mDMIPS_MHz % 1000);

  printf("\nMemory benchmarks\n");
  unsigned* page1 = (unsigned*)(RAM_BASE + 0x100000);
  unsigned* page2 = (unsigned*)(RAM_BASE + 0x200000);
  unsigned start;

  // *** memset
  start = time_100nsec();
  memset_1mb_no_unroll(page1, 0x111);
  print_bench_res("memset (no unroll)", start);
  test_memset(page1, 0x111);

  start = time_100nsec();
  memset_1mb(page1, 0x222);
  print_bench_res("memset", start);
  test_memset(page1, 0x222);

  if (BOARD_REGS->cpu_features & CPU_FEATURES_ZICBOP) {
    start = time_100nsec();
    memset_1mb_prefetch(page1, 0x333);
    print_bench_res("memset prefetch", start);
    test_memset(page1, 0x333);
  }

  // *** memcpy
  test_memcpy_fill(page2);
  memset_1mb(page1, 0x222);
  start = time_100nsec();
  memcpy_1mb(page1, page2);
  print_bench_res("memcpy", start);
  test_memcpy_check(page1);

  if (BOARD_REGS->cpu_features & CPU_FEATURES_ZICBOP) {
    test_memcpy_fill(page2);
    memset_1mb(page1, 0x222);
    start = time_100nsec();
    memcpy_1mb_prefetch(page1, page2);
    print_bench_res("memcpy prefetch", start);
    test_memcpy_check(page1);
  }

  // *** sparse xor
  start = time_100nsec();
  sparse_agg_xor_1mb(page1);
  print_bench_res("sparse_agg_xor", start);

  start = time_100nsec();
  sparse_inplace_xor_1mb(page2, 0xa5a5a5a5);
  print_bench_res("sparse_inplace_xor", start);

  if (BOARD_REGS->cpu_features & CPU_FEATURES_ZICBOP) {
    start = time_100nsec();
    sparse_agg_xor_1mb_prefetch(page1);
    print_bench_res("sparse_agg_xor     prefetch", start);

    start = time_100nsec();
    sparse_inplace_xor_1mb_prefetch(page2, 0xa5a5a5a5);
    print_bench_res("sparse_inplace_xor prefetch", start);
  }
}

#include <endeavour2/defs.h>

#include "bios_internal.h"

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

void mul_vec4_mat4_float_384kflop(float* out, const float* in, const float* mat);
void dummy_mul_float_384kflop(float* data, float v);

static void print_mem_bench_res(const char* bench_name, unsigned start_time) {
  unsigned time = time_100nsec() - start_time;
  unsigned speed = 1000000000 / time;
  printf("\t%-35s: %u.%02u MB/s\n", bench_name, speed/100, speed%100);
}

static void print_fpu_bench_res(const char* bench_name, unsigned start_time) {
  unsigned time = time_100nsec() - start_time;
  unsigned kflops = (384u * 1024u * 10000u) / time;
  printf("\t%-35s: %u.%03u MFLOPS\n", bench_name, kflops / 1000, kflops % 1000);
}

static void test_memset(const unsigned* data, unsigned v) {
  for (int i = 0; i < 1024*1024/4; ++i) {
    if (data[i] != v) {
      printf("[ERROR] incorrect result: mem[%08x] = %08x, expected %08x\n", data + i, data[i], v);
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
      printf("[ERROR] incorrect result: mem[%08x] = %08x, expected %08x\n", data + i, data[i], i);
      break;
    }
  }
}

static const float float_vals[] = {0.1f, 0.7f, 1.3f, 3.15f};
static const double double_vals[] = {0.1, 0.7, 1.3, 3.15};

static void fill_float(float* data, int count) {
  for (int i = 0; i < count; ++i) {
    data[i] = float_vals[i&3];
  }
}

static void fill_double(double* data, int count) {
  for (int i = 0; i < count; ++i) {
    data[i] = double_vals[i&3];
  }
}

unsigned dhrystone();

static void run_benchmarks_th() {
  int hartid = get_hartid();
  if (BOARD_REGS->hart_count > 1) {
    printf("\n[core%u]\n", hartid);
  }

  unsigned mDMIPS = dhrystone();
  unsigned mDMIPS_MHz = (mDMIPS * 1000) / (BOARD_REGS->cpu_frequency / 1000);
  printf("\nDhrystone benchmark\t: %u.%02u DMIPS (%u.%03u DMIPS/MHz)\n",
         mDMIPS / 1000, (mDMIPS % 1000) / 10,  mDMIPS_MHz / 1000, mDMIPS_MHz % 1000);

  if (BOARD_REGS->ram_size < 0x800000) return;

  unsigned* page1 = (unsigned*)(RAM_BASE + 0x080000);
  unsigned* page2 = (unsigned*)(RAM_BASE + 0x700000);
  unsigned start;

  unsigned isa;
  asm volatile("csrr %0, misa" : "=r" (isa));
  int hasFloat = isa & (1<<5);

  if (hasFloat) {
    printf("\nFPU benchmarks\n");

    fill_float((void*)page1, 24);
    start = time_100nsec();
    dummy_mul_float_384kflop((float*)page1, 1.0f);
    print_fpu_bench_res("fmul.s (no load/store)", start);

    fill_float((void*)page1, 16 + 8192 * 3 * 4);
    start = time_100nsec();
    mul_vec4_mat4_float_384kflop((float*)page2, (float*)page1 + 16, (float*)page1);
    print_fpu_bench_res("vector4 * matrix4x4, fp32", start);
  }

  printf("\nMemory benchmarks\n");

  // *** memset
  start = time_100nsec();
  memset_1mb_no_unroll(page1, 0x111);
  print_mem_bench_res("memset (no unroll)", start);
  test_memset(page1, 0x111);

  start = time_100nsec();
  memset_1mb(page1, 0x222);
  print_mem_bench_res("memset", start);
  test_memset(page1, 0x222);

  if (BOARD_REGS->cpu_features[hartid] & CPU_FEATURES_ZICBOP) {
    start = time_100nsec();
    memset_1mb_prefetch(page1, 0x333);
    print_mem_bench_res("memset prefetch", start);
    test_memset(page1, 0x333);
  }

  // *** memcpy
  test_memcpy_fill(page2);
  memset_1mb(page1, 0x222);
  start = time_100nsec();
  memcpy_1mb(page1, page2);
  print_mem_bench_res("memcpy", start);
  test_memcpy_check(page1);

  if (BOARD_REGS->cpu_features[hartid] & CPU_FEATURES_ZICBOP) {
    test_memcpy_fill(page2);
    memset_1mb(page1, 0x222);
    start = time_100nsec();
    memcpy_1mb_prefetch(page1, page2);
    print_mem_bench_res("memcpy prefetch", start);
    test_memcpy_check(page1);
  }

  // *** sparse xor
  start = time_100nsec();
  sparse_agg_xor_1mb(page1);
  print_mem_bench_res("sparse_agg_xor", start);

  start = time_100nsec();
  sparse_inplace_xor_1mb(page2, 0xa5a5a5a5);
  print_mem_bench_res("sparse_inplace_xor", start);

  if (BOARD_REGS->cpu_features[hartid] & CPU_FEATURES_ZICBOP) {
    start = time_100nsec();
    sparse_agg_xor_1mb_prefetch(page1);
    print_mem_bench_res("sparse_agg_xor     prefetch", start);

    start = time_100nsec();
    sparse_inplace_xor_1mb_prefetch(page2, 0xa5a5a5a5);
    print_mem_bench_res("sparse_inplace_xor prefetch", start);
  }

  // *** sdcard
  if (hartid == 0 && get_sdcard_sector_count() >= 2048) {
    start = time_100nsec();
    unsigned count = sdread(page1, 0, 2048);
    print_mem_bench_res("SD card read", start);
    if (count != 2048) {
      printf("[ERROR] SD card read failed, transfered %u of 2048 sectors\n", count);
    }
  }
}

void run_benchmarks() {
  run_benchmarks_th();
  for (int hartid = 1; hartid < BOARD_REGS->hart_count; ++hartid) {
    volatile struct HartCfg* cfg = &hart_cfg[hartid];
    if (!cfg->isa) continue;
    cfg->jump_to = run_benchmarks_th;
    cfg->action = HART_ACTION_FENCEI | HART_ACTION_JUMP;
    software_interrupt(hartid);
    while (1) {
      wait(2000000);
      if (!cfg->jump_to) break;
    }
  }
}

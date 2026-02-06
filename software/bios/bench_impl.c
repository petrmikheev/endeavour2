// moved to a separate file to prevent inlining

#include <endeavour2/raw/defs.h>

void memset_1mb_no_unroll(unsigned* dst, unsigned v) {
  for (int i = 0; i < 1024*1024/4; ++i) {
    dst[i] = v;
  }
}

void memset_1mb(unsigned* dst, unsigned v) {
  for (int i = 0; i < 1024*1024/4; i += 16) {
    for (int j = i; j < i + 16; ++j) dst[j] = v;  // with -O3 compiler unrolls small loops
  }
}

void memset_1mb_prefetch(unsigned* dst, unsigned v) {
  for (int i = 0; i < 1024*1024/4; i += 16) {
    asm volatile("prefetch.w 64(%0)" :: "r" (dst+i));
    for (int j = i; j < i + 16; ++j) dst[j] = v;
  }
}

void memset_1mb_dma(unsigned* dst, unsigned v) {
  volatile struct DmaCmd { unsigned lo, hi; }* commands = (void*)(RAM_BASE + BIOS_SIZE);

  commands[0].lo = v;  // fill bytes 0-4096 in internal buffer with `v`
  commands[0].hi = DMA_CMD_HI(DMA_SET, 0, 4096);

  for (unsigned i = 0; i < 1024*1024/4096; ++i) {
    commands[i + 1].lo = (unsigned long)dst + i*4096;
    commands[i + 1].hi = DMA_CMD_HI(DMA_WRITE, 0, 4096);
  }

  DMA_REGS->cmdAddress = (void*)commands;
  asm volatile("fence ow, ow");
  DMA_REGS->cmdCount = 257;
  asm volatile("fence o, i");
  while (!DMA_REGS->int_stat);
}

/*void memset_1mb_zicboz(unsigned* dst, unsigned v) {
  if (v == 0) {
    for (int i = 0; i < 1024*1024/4; i += 16) {
      asm volatile("cbo.zero 0(%0)" :: "r" (dst+i));
    }
  } else {
    for (int i = 0; i < 1024*1024/4; i += 16) {
      asm volatile("cbo.zero 0(%0)" :: "r" (dst+i));
      for (int j = i; j < i + 16; ++j) dst[j] = v;
    }
  }
}*/

void memcpy_1mb(unsigned* restrict dst, const unsigned* restrict src) {
  for (int i = 0; i < 1024*1024/4; i += 16) {
    for (int j = i; j < i + 16; ++j) dst[j] = src[j];
  }
}

void memcpy_1mb_prefetch(unsigned* restrict dst, const unsigned* restrict src) {
  for (int i = 0; i < 1024*1024/4; i += 16) {
    asm volatile("prefetch.r 64(%0)" :: "r" (src+i));
    asm volatile("prefetch.w 64(%0)" :: "r" (dst+i));
    for (int j = i; j < i + 16; ++j) dst[j] = src[j];
  }
}

void memcpy_1mb_dma(unsigned* restrict dst, const unsigned* restrict src) {
  volatile struct DmaCmd { unsigned lo, hi; }* commands = (void*)(RAM_BASE + BIOS_SIZE);

  for (unsigned i = 0; i < 1024*1024/4096; ++i) {
    commands[i*2 + 0].lo = (unsigned long)src + i*4096;
    commands[i*2 + 0].hi = DMA_CMD_HI(DMA_READ_SYNC, 0, 4096);
    commands[i*2 + 1].lo = (unsigned long)dst + i*4096;
    commands[i*2 + 1].hi = DMA_CMD_HI(DMA_WRITE_SYNC, 0, 4096);
  }

  DMA_REGS->cmdAddress = (void*)commands;
  asm volatile("fence ow, ow");
  DMA_REGS->cmdCount = 512;
  asm volatile("fence o, i");
  while (!DMA_REGS->int_stat);
}

/*void memcpy_1mb_zicboz(unsigned* restrict dst, const unsigned* restrict src) {
  for (int i = 0; i < 1024*1024/4; i += 16) {
    asm volatile("prefetch.r 64(%0)" :: "r" (src+i));
    asm volatile("cbo.zero 0(%0)" :: "r" (dst+i));
    for (int j = i; j < i + 16; ++j) dst[j] = src[j];
  }
}*/

unsigned sparse_agg_xor_1mb(const unsigned* src) {
  unsigned res;
  for (int i = 0; i < 1024*1024/4; i += 64) {
    res ^= src[i];
    res ^= src[i + 16];
    res ^= src[i + 32];
    res ^= src[i + 48];
  }
  return res;
}

void sparse_inplace_xor_1mb(unsigned* data, unsigned v) {
  for (int i = 0; i < 1024*1024/4; i += 64) {
    data[i] ^= v;
    data[i + 16] ^= v;
    data[i + 32] ^= v;
    data[i + 48] ^= v;
  }
}

unsigned sparse_agg_xor_1mb_prefetch(const unsigned* src) {
  unsigned res;
  for (int i = 0; i < 1024*1024/4; i += 64) {
    asm volatile("prefetch.r 256(%0)" :: "r" (src+i));
    res ^= src[i];
    asm volatile("prefetch.r 320(%0)" :: "r" (src+i));
    res ^= src[i + 16];
    asm volatile("prefetch.r 384(%0)" :: "r" (src+i));
    res ^= src[i + 32];
    asm volatile("prefetch.r 448(%0)" :: "r" (src+i));
    res ^= src[i + 48];
  }
  return res;
}

void sparse_inplace_xor_1mb_prefetch(unsigned* data, unsigned v) {
  for (int i = 0; i < 1024*1024/4; i += 64) {
    asm volatile("prefetch.r 256(%0)" :: "r" (data+i));
    data[i] ^= v;
    asm volatile("prefetch.r 320(%0)" :: "r" (data+i));
    data[i + 16] ^= v;
    asm volatile("prefetch.r 384(%0)" :: "r" (data+i));
    data[i + 32] ^= v;
    asm volatile("prefetch.r 448(%0)" :: "r" (data+i));
    data[i + 48] ^= v;
  }
}

struct Vec4f { float x, y, z, a; };
struct Mat4f { float c[4][4]; };

void mul_vec4_mat4_float_384kflop(struct Vec4f* restrict out, const struct Vec4f* restrict in, const struct Mat4f* restrict mat) {
  for (int j = 0; j < 8192; ++j) {
    for (int i = 0; i < 3; ++i) {
      out[i].x = in[i].x * mat->c[0][0] + in[i].y * mat->c[0][1] + in[i].z * mat->c[0][2] + in[i].a * mat->c[0][3];
      out[i].y = in[i].x * mat->c[1][0] + in[i].y * mat->c[1][1] + in[i].z * mat->c[1][2] + in[i].a * mat->c[1][3];
      out[i].z = in[i].x * mat->c[2][0] + in[i].y * mat->c[2][1] + in[i].z * mat->c[2][2] + in[i].a * mat->c[2][3];
      out[i].a = in[i].x * mat->c[3][0] + in[i].y * mat->c[3][1] + in[i].z * mat->c[3][2] + in[i].a * mat->c[3][3];
    }
    in += 3;
    out += 3;
  }
}

void dummy_mul_float_384kflop(float* data, float v) {
  for (int j = 0; j < 16384; ++j) {
    for (int i = 0; i < 12; ++i) {
      data[i] *= v;
      data[i] *= v;
    }
  }
}

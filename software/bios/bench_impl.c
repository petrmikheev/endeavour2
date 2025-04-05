// moved to a separate file to prevent inlining

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

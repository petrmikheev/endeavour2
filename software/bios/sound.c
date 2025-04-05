#include <endeavour2/defs.h>

#include "bios_internal.h"

// generated with python ', '.join(['0x%x' % int(math.sin(x/32*math.pi/2)*0x800) for x in range(32)])
static const unsigned short sin_table[32] = {
    0x0, 0x64, 0xc8, 0x12c, 0x18f, 0x1f1, 0x252, 0x2b1, 0x30f, 0x36b, 0x3c5, 0x41c, 0x471, 0x4c3, 0x513, 0x55f,
    0x5a8, 0x5ed, 0x62f, 0x66c, 0x6a6, 0x6dc, 0x70e, 0x73b, 0x764, 0x788, 0x7a7, 0x7c2, 0x7d8, 0x7e9, 0x7f6, 0x7fd};

void beep(unsigned duration_ms, unsigned frequency, int volume) {
  int step = 1;
  int rate = frequency * 128;
  while (rate > 44100) {
    step *= 2;
    rate /= 2;
  }
  int sleeping = (AUDIO_REGS->cfg & (AUDIO_EMPTY|AUDIO_NO_SLEEP)) == AUDIO_EMPTY;
  while (!(AUDIO_REGS->cfg & AUDIO_EMPTY));
  AUDIO_REGS->cfg = AUDIO_SAMPLE_RATE(rate) | AUDIO_VOLUME(volume);
  if (volume < 0) {
    for (int i = 0; i < duration_ms * rate / 1000; ++i) {
      while (AUDIO_REGS->stream == 0);
      AUDIO_REGS->stream = 0;
    }
    return;
  }
  if (sleeping) {  // amplifier needs time to turn on
    for (int i = 0; i < rate / 2; ++i) {
      while (AUDIO_REGS->stream == 0);
      AUDIO_REGS->stream = 0;
    }
  }
  int periods = duration_ms * frequency / 1000;
  for (int i = 0; i < periods; ++i) {
    while (AUDIO_REGS->stream < 128);
    for (int j = 0; j < 32; j += step) {
      unsigned v = 0x800 + sin_table[j];
      AUDIO_REGS->stream = v | (v << 16);
    }
    AUDIO_REGS->stream = 0xfff;
    for (int j = 32-step; j >= 0; j -= step) {
      unsigned v = 0x800 + sin_table[j];
      AUDIO_REGS->stream = v | (v << 16);
    }
    for (int j = step; j < 32; j += step) {
      unsigned v = 0x800 - sin_table[j];
      AUDIO_REGS->stream = v | (v << 16);
    }
    AUDIO_REGS->stream = 0x0;
    for (int j = 32-step; j > 0; j -= step) {
      unsigned v = 0x800 - sin_table[j];
      AUDIO_REGS->stream = v | (v << 16);
    }
  }
}

struct WavHeader {
  unsigned riffHeader;
  unsigned size;
  unsigned waveHeader;
  unsigned formatHeader;
  unsigned len_fmt_data;
  unsigned short format_type; // 1 is pcm
  unsigned short num_channels;
  unsigned sample_rate;
  unsigned bytes_per_second;
  unsigned short bytes_all_channels;
  unsigned short bits_per_sample;
  unsigned dataHeader;
  unsigned dataSize;
};

void playWav(void* filePtr, int volume) {
  struct WavHeader* header = (struct WavHeader*)filePtr;
  AUDIO_REGS->cfg = AUDIO_SAMPLE_RATE(header->sample_rate) | AUDIO_VOLUME(volume);
  int points = header->dataSize / header->bytes_all_channels;
  printf("channels=%u rate=%u bits=%u duration=%us\n", header->num_channels, header->sample_rate, header->bits_per_sample, points / header->sample_rate);
  if (header->bits_per_sample != 16 || (header->num_channels != 1 && header->num_channels != 2)) {
    printf("Error: only 16-bit mono/stereo stream supported\n");
    return;
  }
  int stereo = header->num_channels == 2;
  void* data = filePtr + sizeof(struct WavHeader);
  for (int i = 0; i < points; ++i) {
    while (AUDIO_REGS->stream == 0);
    unsigned v1, v2;
    if (stereo) {
      unsigned v = ((unsigned*)data)[i];
      v1 = ((v + 0x8000) & 0xffff) >> 4;
      v2 = (((v>>16) + 0x8000) & 0xffff) >> 4;
    } else {
      unsigned v = ((unsigned short*)data)[i];
      v1 = v2 = ((v + 0x8000) & 0xffff) >> 4;
    }
    AUDIO_REGS->stream = v1 | (v2 << 16);
    if ((BOARD_REGS->keys & 3) || UART_REGS->rx >= 0) break;
  }
}

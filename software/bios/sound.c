#include <endeavour2/raw/defs.h>

#include "bios_internal.h"

// generated with python ', '.join(['0x%04x' % int(math.sin(x/32*math.pi/2)*0x8000) for x in range(32)])
static const unsigned short sin_table[32] = {
    0x0000, 0x0647, 0x0c8b, 0x12c8, 0x18f8, 0x1f19, 0x2528, 0x2b1f, 0x30fb, 0x36ba, 0x3c56, 0x41ce, 0x471c, 0x4c3f, 0x5133, 0x55f5,
    0x5a82, 0x5ed7, 0x62f2, 0x66cf, 0x6a6d, 0x6dca, 0x70e2, 0x73b5, 0x7641, 0x7884, 0x7a7d, 0x7c29, 0x7d8a, 0x7e9d, 0x7f62, 0x7fd8};

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
      unsigned v = sin_table[j];
      AUDIO_REGS->stream = v | (v << 16);
    }
    AUDIO_REGS->stream = 0xfff;
    for (int j = 32-step; j >= 0; j -= step) {
      unsigned v = sin_table[j];
      AUDIO_REGS->stream = v | (v << 16);
    }
    for (int j = step; j < 32; j += step) {
      unsigned v = 0x10000 - sin_table[j];
      AUDIO_REGS->stream = v | (v << 16);
    }
    AUDIO_REGS->stream = 0x0;
    for (int j = 32-step; j > 0; j -= step) {
      unsigned v = 0x10000 - sin_table[j];
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
    if (stereo) {
      AUDIO_REGS->stream = ((unsigned*)data)[i];
    } else {
      unsigned v = ((unsigned short*)data)[i];
      AUDIO_REGS->stream = v | (v << 16);
    }
    if ((GPIO_REGS->data_in & (GPIO_KEY0 | GPIO_KEY1)) || UART_REGS->rx >= 0) break;
  }
}

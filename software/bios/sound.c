#include <endeavour2/defs.h>
#include <endeavour2/bios.h>

static const unsigned short beep_data[8] = {0x800, 0xda8, 0xfff, 0xda8, 0x800, 0x257, 0x0, 0x257};

void beep_impl(unsigned volume, int periods) {
  while (!(AUDIO_REGS->cfg & AUDIO_EMPTY));
  AUDIO_REGS->cfg = AUDIO_SAMPLE_RATE(2400) | AUDIO_VOLUME(3); // beep 300Hz
  for (int i = 0; i < 90; ++i) {
    while (AUDIO_REGS->stream < 8);
    AUDIO_REGS->stream = 0;
  }
  for (int i = 0; i < periods; ++i) {
    while (AUDIO_REGS->stream < 8);
    for (int j = 0; j < 8; ++j) {
      unsigned v = (((unsigned)beep_data[j] * volume) >> 8) & 0xfff;
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

void playWav(void* filePtr, int volume, int seconds) {
  struct WavHeader* header = (struct WavHeader*)filePtr;
  unsigned* data = filePtr + sizeof(struct WavHeader);
  AUDIO_REGS->cfg = AUDIO_SAMPLE_RATE(header->sample_rate) | AUDIO_VOLUME(volume);
  printf("channels=%d\nrate=%d\nbits=%d\n", header->num_channels, header->sample_rate, header->bits_per_sample);
  int points = header->dataSize / header->bytes_all_channels;
  printf("points=%d\n", points);
  if (seconds > 0 && points > header->sample_rate * seconds) points = header->sample_rate * seconds;
  for (int i = 0; i < points; ++i) {
    while (AUDIO_REGS->stream < 8);
    unsigned v = data[i + 44100 * 10 ];
    unsigned v1 = ((v + 0x8000) & 0xffff) >> 4;
    unsigned v2 = (((v>>16) + 0x8000) & 0xffff) >> 4;
    AUDIO_REGS->stream = v1 | (v2 << 16);
  }
}

#include <linux/platform_device.h>
#include <linux/of.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <linux/timer.h>

struct EndeavourAudio {
  unsigned cfg;
  unsigned stream;
};

static struct EndeavourAudio __iomem * audio_regs;

// cfg flags
#define AUDIO_SAMPLE_RATE(X) (60000000 / (X) - 1)
#define AUDIO_VOLUME(X) (((unsigned)(X)&15) << 16)
#define AUDIO_MAX_VOLUME 15
#define AUDIO_NO_SLEEP 0x00100000
#define AUDIO_EMPTY    0x80000000

#define STREAM_BUF_SIZE 16384

static void endeavour_pcm_no_sleep(int enable) {
  unsigned cfg = audio_regs->cfg;
  audio_regs->cfg = enable ? (cfg | AUDIO_NO_SLEEP) : (cfg & ~AUDIO_NO_SLEEP);
}

static int endeavour_pcm_get_volume(void) {
  return (audio_regs->cfg >> 16) & 0xf;
}

static void endeavour_pcm_set_volume(int v) {
  audio_regs->cfg = (audio_regs->cfg & ~0xf0000) | AUDIO_VOLUME(v);
}

static void endeavour_pcm_set_rate(int samples_per_second) {
  audio_regs->cfg = (audio_regs->cfg & ~0xffff) | AUDIO_SAMPLE_RATE(samples_per_second);
}

static int endeavour_pcm_queue_remaining_size(void) {
  return audio_regs->stream;
}

static void endeavour_pcm_add_sample(short left, short right) {
  unsigned l = (((unsigned)left + 0x8000) & 0xffff) >> 4;
  unsigned r = (((unsigned)right + 0x8000) & 0xffff) >> 4;
  audio_regs->stream = (l << 16) | r;
}

static struct {
  struct snd_pcm_substream *substream;
  snd_pcm_uframes_t pointer;
} stream_data = {0, 0};

static void endeavour_audio_timer(struct timer_list *t) {
  mod_timer(t, jiffies);
  struct snd_pcm_substream *substream = stream_data.substream;
  if (!substream) return;
  struct snd_pcm_runtime *runtime = substream->runtime;
  if (!runtime || !snd_pcm_running(substream)) return;
  int samples_to_write = (int)runtime->control->appl_ptr - stream_data.pointer;
  int remaining = endeavour_pcm_queue_remaining_size();
  //printk("pos %ld  to_write %d  free %d\n", stream_data.pointer, samples_to_write, remaining);
  if (samples_to_write > remaining) samples_to_write = remaining;
  if (samples_to_write >= 768) samples_to_write = 768;
  else samples_to_write &= ~255;
  if (samples_to_write == 0) return;

  while (samples_to_write > 0) {
    // Assuming S16_LE format, 2 channels (stereo)
    short* src = (short*)(runtime->dma_area + ((unsigned)stream_data.pointer & (STREAM_BUF_SIZE/4-1)) * 4);
    for (int i = 0; i < 512; i += 2) {
      endeavour_pcm_add_sample(src[i], src[i+1]);
    }
    stream_data.pointer += 256;
    samples_to_write -= 256;
  }
  snd_pcm_period_elapsed(substream);
}

static int endeavour_volume_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo) {
  uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
  uinfo->count = 1;
  uinfo->value.integer.min = 0;
  uinfo->value.integer.max = 15;
  return 0;
}

static int endeavour_volume_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol) {
  ucontrol->value.integer.value[0] = endeavour_pcm_get_volume();
  return 0;
}

static int endeavour_volume_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol) {
  int new_volume = ucontrol->value.integer.value[0];
  if (new_volume < 0 || new_volume > 15) return -EINVAL;
  int old_volume = endeavour_pcm_get_volume();

  if (old_volume != new_volume) {
    endeavour_pcm_set_volume(new_volume);
    return 1; // 1 indicates value changed
  }
  return 0;
}

static const struct snd_kcontrol_new volume_control = {
  .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
  .name  = "Volume",
  .info  = endeavour_volume_info,
  .get   = endeavour_volume_get,
  .put   = endeavour_volume_put,
};

static struct snd_pcm_hardware endeavour_pcm_hw = {
  .info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP_VALID),
  .formats          = SNDRV_PCM_FMTBIT_S16_LE,
  .rates            = SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_44100,
  .rate_min         = 5500,
  .rate_max         = 44100,
  .channels_min     = 2,
  .channels_max     = 2,
  .buffer_bytes_max = STREAM_BUF_SIZE,
  .period_bytes_min = 1024,
  .period_bytes_max = 1024,
  .periods_min      = STREAM_BUF_SIZE / 1024,
  .periods_max      = STREAM_BUF_SIZE / 1024,
};

static int endeavour_pcm_open(struct snd_pcm_substream *ss) {
  //printk("pcm open\n");
  ss->runtime->hw = endeavour_pcm_hw;
  //ss->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV;
  //ss->dma_buffer.dev.dev = ss->pcm->card->dev;
  return 0;
}

static int endeavour_pcm_close(struct snd_pcm_substream *ss) {
  //printk("pcm close\n");
  return 0;
}

static int endeavour_hw_params(struct snd_pcm_substream *ss, struct snd_pcm_hw_params *hw_params) {
  //printk("pcm hw params\n");
  if (ss->runtime == NULL) printk("ss->runtime is NULL\n");
  if (ss->dma_buffer.dev.type == SNDRV_DMA_TYPE_UNKNOWN) printk("SNDRV_DMA_TYPE_UNKNOWN\n");
  unsigned int rate = params_rate(hw_params);
  endeavour_pcm_set_rate(rate);
  unsigned b = params_buffer_bytes(hw_params);
  int err = snd_pcm_lib_malloc_pages(ss, b);
  if (err < 0) {
    printk("snd_pcm_lib_malloc_pages() failed: rate=%d, bytes=%d, error %d\n", rate, b, err);
  }
  return err;
}

static int endeavour_hw_free(struct snd_pcm_substream *ss) {
  //printk("pcm hw free\n");
  return snd_pcm_lib_free_pages(ss);
}

static int endeavour_pcm_prepare(struct snd_pcm_substream *ss) {
  //printk("pcm prepare\n");
  stream_data.pointer = 0;
  return 0;
}

static int endeavour_pcm_trigger(struct snd_pcm_substream *ss, int cmd) {
  switch (cmd) {
  case SNDRV_PCM_TRIGGER_START:
  case SNDRV_PCM_TRIGGER_RESUME:
    //printk("pcm trigger start\n");
    stream_data.substream = ss;
    return 0;
  case SNDRV_PCM_TRIGGER_STOP:
  case SNDRV_PCM_TRIGGER_SUSPEND:
    //printk("pcm trigger stop\n");
    stream_data.substream = NULL;
    return 0;
  default:
    return -EINVAL;
  }
}

static snd_pcm_uframes_t endeavour_pcm_pointer(struct snd_pcm_substream *ss) {
  return stream_data.pointer & (STREAM_BUF_SIZE/4-1);
}

static int endeavour_audio_probe(struct platform_device *dev) {
  audio_regs = devm_platform_get_and_ioremap_resource(dev, 0, NULL);
  if (IS_ERR((void*)audio_regs))
    return PTR_ERR((void*)audio_regs);
  printk("Initializing audio driver\n");
  endeavour_pcm_no_sleep(1);

  struct snd_card *card;
  int ret = snd_card_new(&dev->dev, 0, "EndeavourSoundCard", THIS_MODULE, 0, &card);
  if (ret < 0) {
    printk("snd_card_new() failed: error %d\n", ret);
    return ret;
  }
  strcpy(card->driver, "endeavour-audio");
  strcpy(card->shortname, "EndeavourSoundCard");

  static struct {} device_data;
  static struct snd_device_ops ops = { NULL };
  if ((ret = snd_device_new(card, SNDRV_DEV_LOWLEVEL, &device_data, &ops)) < 0) {
    printk("snd_device_new() failed: error %d\n", ret);
    return ret;
  }

  struct snd_pcm *pcm;
  ret = snd_pcm_new(card, card->driver, 0, 1, 0, &pcm);
  if (ret < 0)
    return ret;

  static struct snd_pcm_ops my_pcm_ops = {
        .open      = endeavour_pcm_open,
        .close     = endeavour_pcm_close,
        .ioctl     = snd_pcm_lib_ioctl,
        .hw_params = endeavour_hw_params,
        .hw_free   = endeavour_hw_free,
        .prepare   = endeavour_pcm_prepare,
        .trigger   = endeavour_pcm_trigger,
        .pointer   = endeavour_pcm_pointer,
  };
  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &my_pcm_ops);
  pcm->private_data = NULL;
  pcm->info_flags = 0;
  strcpy(pcm->name, card->shortname);
  snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_VMALLOC, NULL, STREAM_BUF_SIZE, STREAM_BUF_SIZE);

  strcpy(card->mixername, "LM4811");
  struct snd_kcontrol *kcontrol = snd_ctl_new1(&volume_control, NULL);
  if ((ret = snd_ctl_add(card, kcontrol)) < 0)
    return ret;

  if ((ret = snd_card_register(card)) < 0)
    return ret;

  static struct timer_list timer;
  timer_setup(&timer, endeavour_audio_timer, 0);
  mod_timer(&timer, jiffies + 1);

  return 0;
}

static const struct of_device_id endeavour_audio_match[] = {
  { .compatible = "endeavour,audio" },
  {}
};

static struct platform_driver endeavour_audio_driver = {
  .driver = {
    .name           = "endeavour-audio",
    .of_match_table = endeavour_audio_match,
  },
  .probe = endeavour_audio_probe,
};
builtin_platform_driver(endeavour_audio_driver);

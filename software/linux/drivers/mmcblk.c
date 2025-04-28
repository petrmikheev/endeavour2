#include <linux/platform_device.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/of.h>

static spinlock_t mmc_lk;
static void __iomem * mmc_membase;
static unsigned mmc_sector_count;
static unsigned mmc_rca;

#define SDCARD_CMD 0x0
#define SDCARD_DATA 0x4
#define SDCARD_FIFO0 0x8
#define SDCARD_FIFO1 0xC
#define SDCARD_PHY 0x10
#define SDCARD_FIFO0_LE 0x18  // FIFO0 with big-endian -> little-endian conversion
#define SDCARD_FIFO1_LE 0x1C  // FIFO1 with big-endian -> little-endian conversion

static unsigned* receive_sector(unsigned* ptr, unsigned port) {
  unsigned* end = ptr + 128;
  const void __iomem * addr = mmc_membase + port;
  while (ptr < end) {
    ptr[0] = ioread32(addr);
    ptr[1] = ioread32(addr);
    ptr[2] = ioread32(addr);
    ptr[3] = ioread32(addr);
    ptr += 4;
  }
  return ptr;
}

static const unsigned* send_sector(const unsigned* ptr, unsigned port) {
  const unsigned* end = ptr + 128;
  void __iomem * addr = mmc_membase + port;
  while (ptr < end) {
    iowrite32(ptr[0], addr);
    iowrite32(ptr[1], addr);
    iowrite32(ptr[2], addr);
    iowrite32(ptr[3], addr);
    ptr += 4;
  }
  return ptr;
}

static const unsigned
    SDIO_CMD      = 0x00000040,
    SDIO_R1       = 0x00000100,
    SDIO_R1b      = 0x00000300,
    SDIO_WRITE    = 0x00000400,
    SDIO_MEM      = 0x00000800,
    SDIO_FIFO     = 0x00001000,
    SDIO_ACK      = 0x04000000,
    SDIO_ERR      = 0x00008000,
    SDIO_BUSY     = 0x00104800;

static void command(unsigned cmd, unsigned arg) {
  iowrite32(arg, mmc_membase + SDCARD_DATA);
  iowrite32(SDIO_CMD | SDIO_ERR | cmd, mmc_membase + SDCARD_CMD);
  while (ioread32(mmc_membase + SDCARD_CMD) & SDIO_BUSY);
}

static void sd_wait_ready(void) {
  int counter = 0;
  while (1) {
    command(SDIO_R1 | 13, mmc_rca);
    if (ioread32(mmc_membase + SDCARD_DATA) & (1<<8)) return;
    if (++counter > 10000000) {
      printk("mmcblk timeout\n");
      return;
    }
  }
}

static unsigned sdread(unsigned* dst, unsigned sector, unsigned sector_count) {
  if (sector_count == 0) return 0;
  sd_wait_ready();
  command(SDIO_R1 | SDIO_MEM | 18, sector);
  unsigned fifo = SDIO_FIFO;
  for (unsigned b = 0; b < sector_count - 1; ++b) {
    if (ioread32(mmc_membase + SDCARD_CMD) & SDIO_ERR) {
      command(SDIO_R1b | 12, 0);
      return b;
    }
    iowrite32(SDIO_MEM | fifo, mmc_membase + SDCARD_CMD);
    dst = receive_sector(dst, fifo ? SDCARD_FIFO0_LE : SDCARD_FIFO1_LE);
    fifo ^= SDIO_FIFO;
    while (ioread32(mmc_membase + SDCARD_CMD) & SDIO_BUSY);
  }
  int err = ioread32(mmc_membase + SDCARD_CMD) & SDIO_ERR;
  command(SDIO_R1b | 12, 0);
  if (err) return sector_count - 1;
  receive_sector(dst, fifo ? SDCARD_FIFO0_LE : SDCARD_FIFO1_LE);
  return sector_count;
}

static unsigned sdwrite(const unsigned* src, unsigned sector, unsigned sector_count) {
  if (sector_count == 0) return 0;
  sd_wait_ready();
  src = send_sector(src, SDCARD_FIFO0_LE);
  iowrite32(sector, mmc_membase + SDCARD_DATA);
  iowrite32(SDIO_CMD | SDIO_ERR | SDIO_R1 | SDIO_WRITE | SDIO_ACK | SDIO_MEM | 25, mmc_membase + SDCARD_CMD);
  unsigned fifo = 0;
  for (unsigned b = 0; b < sector_count - 1; ++b) {
    fifo ^= SDIO_FIFO;
    src = send_sector(src, fifo ? SDCARD_FIFO1_LE : SDCARD_FIFO0_LE);
    while (ioread32(mmc_membase + SDCARD_CMD) & SDIO_BUSY);
    if (ioread32(mmc_membase + SDCARD_CMD) & SDIO_ERR) {
      command(SDIO_R1b | 12, 0);
      return b;
    }
    iowrite32(SDIO_WRITE | SDIO_MEM | fifo, mmc_membase + SDCARD_CMD);
  }
  while (ioread32(mmc_membase + SDCARD_CMD) & SDIO_BUSY);
  int err = ioread32(mmc_membase + SDCARD_CMD) & SDIO_ERR;
  command(SDIO_R1b | 12, 0);
  return err ? sector_count - 1 : sector_count;
}

static bool process_segment(void* ptr, unsigned sector, unsigned sector_count, bool write) {
  if (write)
    return sdwrite((unsigned*)ptr, sector, sector_count) == sector_count;
  else
    return sdread((unsigned*)ptr, sector, sector_count) == sector_count;
}

static void mmcblk_submit_bio(struct bio *bio) {
  struct bio_vec bvec;
  struct bvec_iter iter;
  if (bio_op(bio) != REQ_OP_READ && bio_op(bio) != REQ_OP_WRITE) {
    printk("mmcblk unsupported op %d\n", bio_op(bio));
  }
  bool write = bio_data_dir(bio) == WRITE;

  bio_for_each_segment(bvec, bio, iter) {
    char *ptr = bvec_virt(&bvec);
    if ((unsigned)ptr & 3) printk("mmcblk: wrong buffer alignment\n");
    unsigned sector = iter.bi_sector;
    unsigned sector_count = bvec.bv_len >> 9;
    if (!process_segment(ptr, sector, sector_count, write)) {
      printk("mmcblk error write=%d  ptr=0x%x  sector=%d  count=%d\n", (int)write, (unsigned)ptr, sector, sector_count);
      bio_io_error(bio);
      return;
    }
  }

  bio_endio(bio);
}

static struct block_device_operations mmcblk_ops = {
  .owner = THIS_MODULE,
  .submit_bio = mmcblk_submit_bio
};

static unsigned sbi_get_val(int arg) {
  register uintptr_t a1 asm ("a1") = (uintptr_t)(0);
  register uintptr_t a6 asm ("a6") = (uintptr_t)(arg);
  register uintptr_t a7 asm ("a7") = (uintptr_t)(0x0A000000);
  asm volatile ("ecall" : "=r" (a1) : "r" (a6), "r" (a7));
  return a1;
}

static int mmcblk_probe(struct platform_device *dev)
{
  // Note: sdcard is already initialized by BIOS
  mmc_membase = devm_platform_get_and_ioremap_resource(dev, 0, NULL);
  if (IS_ERR(mmc_membase))
    return PTR_ERR(mmc_membase);
  mmc_sector_count = sbi_get_val(1);
  mmc_rca = sbi_get_val(2);
  printk("mmcblk  reg 0x%x  rca 0x%04x  size %u MB\n", (unsigned)dev->resource[0].start, mmc_rca >> 16, mmc_sector_count >> 11);

  int major_number = register_blkdev(101, "mmcblk");

  struct gendisk *mmc = blk_alloc_disk(NULL, NUMA_NO_NODE);
  if (!mmc) return -ENOMEM;

  spin_lock_init(&mmc_lk);

  snprintf(mmc->disk_name, 8, "mmcblk0");  // /dev/mmcblk0

  mmc->flags = 0;
  mmc->major = major_number;
  mmc->fops = &mmcblk_ops;
  mmc->first_minor = 0;

  set_capacity(mmc, mmc_sector_count);

  return add_disk(mmc);
}

static const struct of_device_id mmcblk_match[] = {
  { .compatible = "endeavour,mmcblk" },
  {}
};

static struct platform_driver mmcblk_driver = {
  .driver = {
    .name           = "endeavour-mmcblk",
    .of_match_table = mmcblk_match,
  },
  .probe = mmcblk_probe,
};
builtin_platform_driver(mmcblk_driver);

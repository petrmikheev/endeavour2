#include <asm/io.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mm.h>

#define DISPLAY_RESERVED_START (32<<10)  // 32 KB, reserved by BIOS
#define DISPLAY_RESERVED_END   (32<<20)  // 32 MB

struct EndeavourVideoMode {
// clock flags
#define HSYNC_INV (1<<31)
#define VSYNC_INV (1<<30)
  unsigned clock;        //  0
  unsigned hResolution;  //  4
  unsigned hSyncStart;   //  8
  unsigned hSyncEnd;     //  C
  unsigned hTotal;       // 10
  unsigned vResolution;  // 14
  unsigned vSyncStart;   // 18
  unsigned vSyncEnd;     // 1C
  unsigned vTotal;       // 20
};

struct EndeavourVideo {
  struct EndeavourVideoMode mode;
// cfg flags
#define VIDEO_TEXT_ON     1
#define VIDEO_GRAPHIC_ON  2
#define VIDEO_RGB565      0
#define VIDEO_RGAB5515    4
#define VIDEO_FONT_HEIGHT(X) ((((X)-1)&15) << 4) // allowed range [6, 16]
  unsigned cfg;          // 24
  unsigned regIndex;     // 28
  unsigned regValue;     // 2C
  unsigned textAddr;     // 30
  unsigned graphicAddr;  // 34
  unsigned textOffset;   // 38
  unsigned frameNumber;  // 3c
};

static volatile struct EndeavourVideo __iomem * display_regs;

struct CharmapData {
  unsigned index;
  unsigned value;
};

struct TextAddrAndOffset {
  unsigned buffer_addr;
  unsigned short pixel_offset_x;
  unsigned short pixel_offset_y;
};

void set_endeavour_sbi_console(bool v);

static void set_pixel_freq(unsigned freq) {
  register uintptr_t a0 asm ("a0") = (uintptr_t)(freq);
  register uintptr_t a1 asm ("a1") = (uintptr_t)(0);
  register uintptr_t a6 asm ("a6") = (uintptr_t)(3);
  register uintptr_t a7 asm ("a7") = (uintptr_t)(0x0A000000);
  asm volatile ("ecall" : : "r" (a0), "r" (a1), "r" (a6), "r" (a7));
}

static long display_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
  // printk("display_ioctl cmd=%u arg=%lu\n", cmd, arg);
  union {
    unsigned v;
    struct CharmapData cd;
    struct TextAddrAndOffset ta;
    struct EndeavourVideoMode vm;
    struct { unsigned x, y; } size;
  } p;
  switch (cmd) {
    case 0xaa0: // get text addr
      p.ta.buffer_addr = display_regs->textAddr & (DISPLAY_RESERVED_END - 1);
      *(unsigned*)&p.ta.pixel_offset_x = display_regs->textOffset;
      return copy_to_user((void*)arg, &p.ta, sizeof(p.ta));
      break;
    case 0xaa1: // get graphic addr
      p.v = display_regs->graphicAddr & (DISPLAY_RESERVED_END - 1);
      if (copy_to_user((void*)arg, &p.v, sizeof(p.v))) return -1;
      break;
    case 0xaa2: // set text addr
      if (copy_from_user(&p.ta, (void*)arg, sizeof(struct TextAddrAndOffset))) return -1;
      display_regs->textAddr = 0x80000000 + (p.ta.buffer_addr & (DISPLAY_RESERVED_END - 1));
      display_regs->textOffset = *(unsigned*)&p.ta.pixel_offset_x;
      break;
    case 0xaa3: // set graphic addr
      if (copy_from_user(&p.v, (void*)arg, sizeof(p.v))) return -1;
      display_regs->graphicAddr = 0x80000000 + (p.v & (DISPLAY_RESERVED_END - 1));
      break;
    case 0xaa4: // get cfg
      p.v = display_regs->cfg;
      if (copy_to_user((void*)arg, &p.v, sizeof(p.v))) return -1;
      break;
    case 0xaa5: // set cfg
      if (copy_from_user(&p.v, (void*)arg, sizeof(p.v))) return -1;
      display_regs->cfg = p.v;
      break;
    case 0xaa6: // set charmap
      if (copy_from_user(&p.cd, (void*)arg, sizeof(struct CharmapData))) return -1;
      display_regs->regIndex = p.cd.index;
      display_regs->regValue = p.cd.value;
      break;
    case 0xaa7:
      set_endeavour_sbi_console(0);
      break;
    case 0xaa8: // get frame number
      p.v = display_regs->frameNumber;
      if (copy_to_user((void*)arg, &p.v, sizeof(p.v))) return -1;
      break;
    case 0xaa9: // get display size
      p.size.x = display_regs->mode.hResolution;
      p.size.y = display_regs->mode.vResolution;
      if (copy_to_user((void*)arg, &p.size, sizeof(p.size))) return -1;
      break;
    case 0xaaa: // set display mode
      if (arg == 0) {
        set_pixel_freq(0);
        return 0;
      }
      if (copy_from_user(&p.vm, (void*)arg, sizeof(struct EndeavourVideoMode))) return -1;
      set_pixel_freq(p.vm.clock);
      display_regs->mode = p.vm;
      break;
    default:
      return -1;
  }
  return 0;
}

static int display_mmap(struct file *filp, struct vm_area_struct *vma) {
  unsigned len = vma->vm_end - vma->vm_start;
  unsigned offset = vma->vm_pgoff << PAGE_SHIFT;
  if (offset < DISPLAY_RESERVED_START || offset + len > DISPLAY_RESERVED_END) {
    return -EINVAL;
  }
  return remap_pfn_range(vma, vma->vm_start, (0x80000000 >> PAGE_SHIFT) + vma->vm_pgoff, len, vma->vm_page_prot);
}

static const struct file_operations display_ops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = display_ioctl,
    .mmap = display_mmap
};

static int display_probe(struct platform_device *pdev) {
  printk("Initializing display driver\n");
  display_regs = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
  if (IS_ERR((void*)display_regs))
    return PTR_ERR((void*)display_regs);
  int textbuf_major = register_chrdev(0, "display", &display_ops);
  if (textbuf_major < 0) {
    printk("Can't register display chrdev\n");
    return textbuf_major;
  }
  dev_t devNo = MKDEV(textbuf_major, 0);
  struct class *pClass = class_create("display");
  if (IS_ERR(pClass)) {
    printk("Can't create class\n");
    unregister_chrdev_region(devNo, 1);
    return -1;
  }
  struct device *pDev;
  if (IS_ERR(pDev = device_create(pClass, NULL, devNo, NULL, "display"))) {
    printk("Can't create device /dev/display\n");
    class_destroy(pClass);
    unregister_chrdev_region(devNo, 1);
    return -1;
  }
  return 0;
}

static const struct of_device_id display_match[] = {
  { .compatible = "endeavour,display" },
  {}
};

static struct platform_driver display_driver = {
  .driver = {
    .name = "endeavour_display",
    .of_match_table = display_match,
  },
  .probe = display_probe,
};
builtin_platform_driver(display_driver);

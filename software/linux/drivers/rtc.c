#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

static unsigned sbi_get_time(void) {
  register uintptr_t a1 asm ("a1") = (uintptr_t)(0);
  register uintptr_t a6 asm ("a6") = (uintptr_t)(4);
  register uintptr_t a7 asm ("a7") = (uintptr_t)(0x0A000000);
  asm volatile ("ecall" : "=r" (a1) : "r" (a6), "r" (a7));
  return a1;
}

static void sbi_set_time(unsigned t) {
  register uintptr_t a0 asm ("a0") = (uintptr_t)(t);
  register uintptr_t a1 asm ("a1") = (uintptr_t)(0);
  register uintptr_t a6 asm ("a6") = (uintptr_t)(5);
  register uintptr_t a7 asm ("a7") = (uintptr_t)(0x0A000000);
  asm volatile ("ecall" : : "r" (a0), "r" (a1), "r" (a6), "r" (a7));
}

static time64_t year_2000 = 946684800;

static int endeavour_get_time(struct device *dev, struct rtc_time *tm) {
  rtc_time64_to_tm(year_2000 + sbi_get_time(), tm);
  return 0;
}

static int endeavour_set_time(struct device *dev, struct rtc_time *tm) {
  sbi_set_time(rtc_tm_to_time64(tm) - year_2000);
  return 0;
}

static const struct rtc_class_ops endeavour_rtc_ops = {
  .read_time = endeavour_get_time,
  .set_time = endeavour_set_time
};

static int __init endeavour_rtc_probe(struct platform_device *dev) {
  printk("Initializing rtc driver");

  struct rtc_device *rtc;
  rtc = devm_rtc_allocate_device(&dev->dev);
  if (IS_ERR(rtc))
    return PTR_ERR(rtc);

  rtc->ops = &endeavour_rtc_ops;
  rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
  rtc->range_max = RTC_TIMESTAMP_END_2099;

  platform_set_drvdata(dev, rtc);
  return devm_rtc_register_device(rtc);
}

static const struct of_device_id endeavour_rtc_of_match[] = {
  { .compatible = "endeavour,rtc" },
  {}
};
MODULE_DEVICE_TABLE(of, endeavour_rtc_of_match);

static struct platform_driver endeavour_rtc_driver = {
  .probe = endeavour_rtc_probe,
  .driver = {
    .name = KBUILD_MODNAME,
    .of_match_table = endeavour_rtc_of_match,
  },
};

module_platform_driver_probe(endeavour_rtc_driver, endeavour_rtc_probe);

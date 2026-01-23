#include "linux/interrupt.h"
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/of_irq.h>

#define REG_IN_0_31    0x24
#define REG_OUT_32_63  0x20
#define REG_OUT_64_95  0x30

struct simple_gpio_data {
    void __iomem *base;
    struct gpio_chip chip;
    int irq26, irq27;
    //spinlock_t lock; // Protects Read-Modify-Write on output registers
};

static int simple_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
    struct simple_gpio_data *priv = gpiochip_get_data(gc);
    uint32_t val;

    if (offset < 32) {
        val = ioread32(priv->base + REG_IN_0_31);
        printk("GPIO #%d -> %d\n", offset, !!(val & BIT(offset)));
        return !!(val & BIT(offset));
    } else if (offset < 64) {
        val = ioread32(priv->base + REG_OUT_32_63);
        return !!(val & BIT(offset - 32));
    } else {
        val = ioread32(priv->base + REG_OUT_64_95);
        return !!(val & BIT(offset - 64));
    }
}

static void simple_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
    struct simple_gpio_data *priv = gpiochip_get_data(gc);
    //unsigned long flags;
    uint32_t val;
    void __iomem *reg;
    unsigned int bit;

    if (offset < 32) return; // Inputs are read-only

    if (offset < 64) {
        reg = priv->base + REG_OUT_32_63;
        bit = offset - 32;
    } else {
        reg = priv->base + REG_OUT_64_95;
        bit = offset - 64;
    }
printk("GPIO #%d <- %d\n", offset, value);
    //spin_lock_irqsave(&priv->lock, flags);
    val = ioread32(reg);
    if (value) val |= BIT(bit);
    else val &= ~BIT(bit);
    iowrite32(val, reg);
    //spin_unlock_irqrestore(&priv->lock, flags);
}

static int simple_direction_input(struct gpio_chip *gc, unsigned int offset)
{
    // Only pins 0-31 are physically wired as inputs
    return (offset < 32) ? 0 : -EINVAL;
}

static int simple_direction_output(struct gpio_chip *gc, unsigned int offset, int value)
{
    // Only pins 32-95 are physically wired as outputs
    if (offset < 32) return -EINVAL;

    simple_gpio_set(gc, offset, value);
    return 0;
}

static void simple_gpio_irq_mask(struct irq_data *d)
{
    int id = irqd_to_hwirq(d);
    printk("simple_gpio_irq_mask %d\n", id);
    struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
    struct simple_gpio_data *priv = gpiochip_get_data(gc);
    disable_irq(id == 26 ? priv->irq26 : priv->irq27);
}

static void simple_gpio_irq_unmask(struct irq_data *d)
{
    int id = irqd_to_hwirq(d);
    printk("simple_gpio_irq_unmask %d\n", id);
    struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
    struct simple_gpio_data *priv = gpiochip_get_data(gc);
    enable_irq(id == 26 ? priv->irq26 : priv->irq27);
}

static int simple_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
    int id = irqd_to_hwirq(d);
    printk("simple_gpio_irq_set_type %d %u\n", id, type);
    return 0;
}

static void simple_gpio_irq_ack(struct irq_data *d) {}

static struct irq_chip endeavour_gpio_irq_chip = {
    .name = "endeavour-gpio-irq",
    .flags = IRQCHIP_IMMUTABLE,
    GPIOCHIP_IRQ_RESOURCE_HELPERS,
    .irq_mask       = simple_gpio_irq_mask,
    .irq_unmask     = simple_gpio_irq_unmask,
    .irq_ack        = simple_gpio_irq_ack,
    .irq_set_type   = simple_gpio_irq_set_type,
};

static irqreturn_t irq26_handler(int irq, void *dev_id)
{
    printk("irq26\n");
    struct simple_gpio_data *priv = dev_id;
    generic_handle_domain_irq(priv->chip.irq.domain, 26);
    return IRQ_HANDLED;
}

static irqreturn_t irq27_handler(int irq, void *dev_id)
{
    printk("irq27\n");
    struct simple_gpio_data *priv = dev_id;
    generic_handle_domain_irq(priv->chip.irq.domain, 27);
    return IRQ_HANDLED;
}

static int simple_gpio_probe(struct platform_device *pdev)
{
    printk("Initializing GPIO driver");
    struct simple_gpio_data *priv;
    struct resource *res;

    priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv) return -ENOMEM;

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    priv->base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(priv->base)) return PTR_ERR(priv->base);

    priv->chip.label = "endeavour-gpio";
    priv->chip.parent = &pdev->dev;
    priv->chip.get = simple_gpio_get;
    priv->chip.set = simple_gpio_set;
    priv->chip.direction_input = simple_direction_input;
    priv->chip.direction_output = simple_direction_output;
    priv->chip.base = 0;
    priv->chip.ngpio = 96;

    struct gpio_irq_chip *girq = &priv->chip.irq;
    girq->chip = &endeavour_gpio_irq_chip;
    girq->parent_handler = NULL;
    girq->num_parents = 0;
    girq->handler = handle_edge_irq;
    girq->default_type = IRQ_TYPE_NONE;

    priv->irq27 = platform_get_irq(pdev, 0);
    int ret = devm_request_irq(&pdev->dev, priv->irq27, irq27_handler, IRQF_TRIGGER_HIGH, "gpio27", priv);
    if (ret) return ret;

    priv->irq26 = platform_get_irq(pdev, 1);
    ret = devm_request_irq(&pdev->dev, priv->irq26, irq26_handler, IRQF_TRIGGER_HIGH, "gpio26", priv);
    if (ret) return ret;

    disable_irq(priv->irq26);
    disable_irq(priv->irq27);

    return devm_gpiochip_add_data(&pdev->dev, &priv->chip, priv);
}

static const struct of_device_id simple_gpio_ids[] = {
    { .compatible = "endeavour,gpio" },
    { }
};
MODULE_DEVICE_TABLE(of, simple_gpio_ids);

static struct platform_driver simple_gpio_driver = {
    .driver = { .name = "endeavour-gpio", .of_match_table = simple_gpio_ids },
    .probe = simple_gpio_probe,
};
module_platform_driver(simple_gpio_driver);

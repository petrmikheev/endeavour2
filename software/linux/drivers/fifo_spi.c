#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/completion.h>

#define REG_CR    0x00  /* Control Register */
#define REG_DR    0x04  /* Data Register */
#define REG_TCR   0x08  /* Transaction Counter */
#define REG_NSEL  0x0C  /* Active-low chip select */

#define CR_INTERRUPT_EN (1 << 31)
#define CR_CPOL         (1 << 30)
#define CR_CPHA         (1 << 29)

struct fifo_spi {
    void __iomem *regs;
    struct completion xfer_done;
    struct device *dev;
};

static irqreturn_t fifo_spi_irq(int irq, void *dev_id)
{
    struct fifo_spi *ms = dev_id;
    u32 cr = readl(ms->regs + REG_CR);
    writel(cr & ~CR_INTERRUPT_EN, ms->regs + REG_CR);  // disable interrupt
    complete(&ms->xfer_done);
    return IRQ_HANDLED;
}

static int fifo_spi_transfer_one(struct spi_controller *ctrl,
                                 struct spi_device *spi,
                                 struct spi_transfer *t)
{
    struct fifo_spi *ms = spi_controller_get_devdata(ctrl);
    unsigned cr = (30000000 + spi->max_speed_hz - 1) / spi->max_speed_hz - 1;
    if (cr > 255) cr = 255;
    if (spi->mode & SPI_CPOL) cr |= CR_CPOL;
    if (spi->mode & SPI_CPHA) cr |= CR_CPHA;

    writel(cr, ms->regs + REG_CR);
    writel(0, ms->regs + REG_NSEL);  // select & reset TX buffer pointer

    unsigned long irq_flags;
    // Fill first 32 bytes of TX buffer
    const u16 *txptr = t->tx_buf;
    int i = 0;
    if (t->tx_buf) {
        local_irq_save(irq_flags);
        for (; i < 16 && i < (t->len + 1) / 2; i++) {
            writel(txptr[i], ms->regs + REG_DR);
        }
    }

    writel(t->len, ms->regs + REG_TCR);  // start transaction

    // Fill the rest of TX buffer
    if (t->tx_buf) {
        for (; i < (t->len + 1) / 2; i++) {
            writel(txptr[i], ms->regs + REG_DR);
        }
        local_irq_restore(irq_flags);
    }

    // Wait for interrupt with 1 second timeout
    reinit_completion(&ms->xfer_done);
    writel(cr | CR_INTERRUPT_EN, ms->regs + REG_CR);
    if (!wait_for_completion_timeout(&ms->xfer_done, HZ)) {
        dev_err(ms->dev, "SPI transfer timed out\n");
        return -ETIMEDOUT;
    }

    writel(1, ms->regs + REG_NSEL);  // deselect & reset RX buffer pointer

    // Read RX buffer
    if (t->rx_buf) {
        u16 *rxptr = t->rx_buf;
        for (i = 0; i < t->len / 2; i++) {
            *(rxptr++) = (u16)readl(ms->regs + REG_DR);
        }
        if (t->len & 1) {
          *(u8*)rxptr = (u8)readl(ms->regs + REG_DR);
        }
    }

    spi_finalize_current_transfer(ctrl);
    return 0;
}

static size_t get_max_transfer_size(struct spi_device *spi) { return 2046; }

static int fifo_spi_probe(struct platform_device *pdev)
{
    struct spi_controller *ctrl;
    struct fifo_spi *ms;
    struct resource *res;
    int irq, ret;

    ctrl = spi_alloc_master(&pdev->dev, sizeof(*ms));
    if (!ctrl) return -ENOMEM;

    platform_set_drvdata(pdev, ctrl);
    ms = spi_controller_get_devdata(ctrl);
    ms->dev = &pdev->dev;

    /* Map Registers */
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    ms->regs = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(ms->regs)) return PTR_ERR(ms->regs);

    /* Request IRQ */
    irq = platform_get_irq(pdev, 0);
    ret = devm_request_irq(&pdev->dev, irq, fifo_spi_irq, 0, "fifo_spi", ms);
    if (ret) return ret;

    init_completion(&ms->xfer_done);

    /* Controller Capabilities */
    ctrl->mode_bits = SPI_CPOL | SPI_CPHA;
    ctrl->max_transfer_size = get_max_transfer_size;
    ctrl->transfer_one = fifo_spi_transfer_one;
    ctrl->num_chipselect = 1;
    ctrl->dev.of_node = pdev->dev.of_node;

    ret = devm_spi_register_controller(&pdev->dev, ctrl);
    if (ret) {
        spi_controller_put(ctrl);
        return ret;
    }

    return 0;
}

static const struct of_device_id fifo_spi_dt_ids[] = {
    { .compatible = "endeavour,fifo-spi" },
    {}
};
MODULE_DEVICE_TABLE(of, fifo_spi_dt_ids);

static struct platform_driver fifo_spi_driver = {
    .probe = fifo_spi_probe,
    .driver = {
        .name = "endeavour_fifo_spi",
        .of_match_table = fifo_spi_dt_ids,
    },
};
module_platform_driver(fifo_spi_driver);

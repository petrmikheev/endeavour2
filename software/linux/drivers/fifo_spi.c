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

static int fifo_spi_transfer_one(struct spi_controller *ctlr,
                                 struct spi_device *spi,
                                 struct spi_transfer *t)
{
    printk("SPI START len=%d\n", t->len);
    struct fifo_spi *ms = spi_controller_get_devdata(ctlr);
    u32 cr = 0;
    //if (spi->mode & SPI_CPOL) cr |= CR_CPOL;
    //if (spi->mode & SPI_CPHA) cr |= CR_CPHA;
    cr = CR_CPOL | 0;  // hardcoded mode=2, divisor=0 (baud_rate=30 MHz)

    writel(cr, ms->regs + REG_CR);
    writel(0, ms->regs + REG_NSEL);  // select & reset TX buffer pointer

    // Fill first 32 bytes of TX buffer
    const u16 *txptr = t->tx_buf;
    int i = 0;
    if (t->tx_buf) {
        for (; i < 16 && i < (t->len + 1) / 2; i++) {
            writel(txptr[i], ms->regs + REG_DR);
        }
    }

    writel(t->len + 1, ms->regs + REG_TCR);  // start transaction

    // Fill the rest of TX buffer
    if (t->tx_buf) {
        for (; i < (t->len + 1) / 2; i++) {
            writel(txptr[i], ms->regs + REG_DR);
        }
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
        // fix dummy bit
        u8* bb = t->rx_buf;
        for (i = 0; i < t->len - 1; ++i, bb++) {
            bb[0] = (bb[0] << 1) | (bb[1]>>7);
        }
        bb[0] = (bb[0] << 1) | ((readl(ms->regs + REG_DR) >> 7) & 1);
        bb = t->rx_buf;
        for (i = 0; i < 32; ++i, bb++) {
            printk("%02x ", bb[i]);
        }
        printk("\n");
        /*if (t->len & 1) {
          *(u8*)rxptr = (u8)readl(ms->regs + REG_DR);
        }*/
    }

    spi_finalize_current_transfer(ctlr);
    printk("SPI END\n");
    return 0;
}

static size_t get_max_transfer_size(struct spi_device *spi) { return 2047; }

static int fifo_spi_probe(struct platform_device *pdev)
{
    struct spi_controller *ctlr;
    struct fifo_spi *ms;
    struct resource *res;
    int irq, ret;

    ctlr = spi_alloc_master(&pdev->dev, sizeof(*ms));
    if (!ctlr) return -ENOMEM;

    platform_set_drvdata(pdev, ctlr);
    ms = spi_controller_get_devdata(ctlr);
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
    ctlr->mode_bits = SPI_CPOL | SPI_CPHA;
    ctlr->max_transfer_size = get_max_transfer_size;
    ctlr->transfer_one = fifo_spi_transfer_one;
    ctlr->num_chipselect = 1;
    ctlr->dev.of_node = pdev->dev.of_node;

    ret = devm_spi_register_controller(&pdev->dev, ctlr);
    if (ret) {
        spi_controller_put(ctlr);
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

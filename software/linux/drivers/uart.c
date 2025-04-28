#include <linux/bits.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/tty_flip.h>

#define REG_RX  0x0  // negative value - buffer empty
#define REG_TX  0x4  // negative value - buffer full
#define REG_CFG 0x8

// REG_CFG flags
#define CFG_BAUD_RATE(X) (60000000 / (X) - 1)
#define CFG_PARITY_EN    (1<<16)
#define CFG_PARITY_ODD   (2<<16)
#define CFG_CSTOPB       (4<<16)

// REG_RX flags
#define RX_PARITY_ERROR  0x100
#define RX_FRAMING_ERROR 0x200

struct endeavour_uart_data {
  struct uart_port port;
  struct timer_list timer;
};

#define to_data(port) container_of(port, struct endeavour_uart_data, port)

static struct console endeavour_console;

static struct uart_driver endeavour_uart_driver = {
  .owner = THIS_MODULE,
  .driver_name = KBUILD_MODNAME,
  .dev_name = "ttyS",
  .major = 100,
  .minor = 0,
  .nr = 1,
  .cons = &endeavour_console,
};

static void endeavour_uart_stop_tx(struct uart_port *port) {
  printk("stop_tx\n");
  //endeavour_uart_update_irq_reg(port, false, EV_TX);
}

static void endeavour_uart_putchar(struct uart_port *port, unsigned char ch) {
  while ((int)ioread32(port->membase + REG_TX) < 0)
    cpu_relax();
  iowrite32(ch, port->membase + REG_TX);
}

static void endeavour_uart_start_tx(struct uart_port *port) {
  char ch;
  uart_port_tx(port, ch, true, endeavour_uart_putchar(port, ch));
  //endeavour_uart_update_irq_reg(port, true, EV_TX);
}

static void endeavour_uart_stop_rx(struct uart_port *port) {
  printk("stop_rx\n");
  /*struct endeavour_uart_port *uart = to_endeavour_uart_port(port);
  del_timer(&uart->timer);*/
}

static unsigned int endeavour_uart_tx_empty(struct uart_port *port) {
  return ioread32(port->membase + REG_TX) < 0 ? TIOCSER_TEMT : 0;
}

static void endeavour_uart_set_termios(struct uart_port *port, struct ktermios *new, const struct ktermios *old)
{
  unsigned new_cfg = CFG_BAUD_RATE(uart_get_baud_rate(port, new, old, 732, 16000000));
  if (new->c_cflag & CSTOPB) new_cfg |= CFG_CSTOPB;
  if (new->c_cflag & PARENB) new_cfg |= CFG_PARITY_EN;
  if (new->c_cflag & PARODD) new_cfg |= CFG_PARITY_ODD;
  printk(KERN_INFO "set_termios %s CFG=0x%x\n", port->name, new_cfg);

  unsigned long flags;
  uart_port_lock_irqsave(port, &flags);
  iowrite32(new_cfg, port->membase + REG_CFG);
  uart_port_unlock_irqrestore(port, flags);
}

static void endeavour_uart_shutdown(struct uart_port *port) {
  printk("endeavour_uart_shutdown %s\n", port->name);
}

static void endeavour_uart_timer(struct timer_list *t)
{
  struct endeavour_uart_data *data = from_timer(data, t, timer);
  struct uart_port *port = &data->port;

  int ch;
  int count = 0;
  while ((ch=ioread32(port->membase + REG_RX)) >= 0) {
    // if (ch & (RX_PARITY_ERROR|RX_FRAMING_ERROR)) handle errors
    port->icount.rx++;
    count++;
    if (!uart_handle_sysrq_char(port, (char)ch))
      uart_insert_char(port, 1, 0, (char)ch, TTY_NORMAL);
  }
  if (count > 0) {
    tty_flip_buffer_push(&port->state->port);
  }
  mod_timer(&data->timer, jiffies + uart_poll_timeout(port));
}

static int endeavour_uart_startup(struct uart_port *port) {
  // TODO use UART interrupt
  struct endeavour_uart_data *data = to_data(port);
  timer_setup(&data->timer, endeavour_uart_timer, 0);
  mod_timer(&data->timer, jiffies + uart_poll_timeout(port));
  return 0;
}

static void endeavour_uart_set_mctrl(struct uart_port *port, unsigned int mctrl) {}
static unsigned int endeavour_uart_get_mctrl(struct uart_port *port) { return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR; }
static const char *endeavour_uart_type(struct uart_port *port) { return "endeavour,uart"; }
static void endeavour_uart_config_port(struct uart_port *port, int flags) { port->type = 1; }
static int endeavour_uart_verify_port(struct uart_port *port, struct serial_struct *ser) { return 0; }

static const struct uart_ops endeavour_uart_ops = {
  .tx_empty     = endeavour_uart_tx_empty,
  .set_mctrl    = endeavour_uart_set_mctrl,
  .get_mctrl    = endeavour_uart_get_mctrl,
  .stop_tx      = endeavour_uart_stop_tx,
  .start_tx     = endeavour_uart_start_tx,
  .stop_rx      = endeavour_uart_stop_rx,
  .startup      = endeavour_uart_startup,
  .shutdown     = endeavour_uart_shutdown,
  .set_termios  = endeavour_uart_set_termios,
  .type         = endeavour_uart_type,
  .config_port  = endeavour_uart_config_port,
  .verify_port  = endeavour_uart_verify_port,
  // TODO .ioctl to set `output_to_sbi`
};

static struct uart_port *global_port = NULL;

static int endeavour_uart_probe(struct platform_device *pdev)
{
  struct endeavour_uart_data *data;
  static struct uart_port *port;

  data = devm_kzalloc(&pdev->dev, sizeof(struct endeavour_uart_data), GFP_KERNEL);
  if (!data)
    return -ENOMEM;

  port = &data->port;
  port->membase = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
  if (IS_ERR(port->membase))
    return PTR_ERR(port->membase);

  port->dev = &pdev->dev;
  port->iotype = UPIO_MEM;
  port->flags = UPF_BOOT_AUTOCONF;
  port->ops = &endeavour_uart_ops;
  port->fifosize = 16;
  port->type = PORT_UNKNOWN;
  port->line = 0;
  spin_lock_init(&port->lock);

  platform_set_drvdata(pdev, port);
  global_port = port;
  return uart_add_one_port(&endeavour_uart_driver, &data->port);
}

/*static void endeavour_uart_remove(struct platform_device *pdev) {
  printk("endeavour_uart_remove\n");  // should never happen
}*/

static const struct of_device_id endeavour_uart_of_match[] = {
  { .compatible = "endeavour,uart" },
  {}
};
MODULE_DEVICE_TABLE(of, endeavour_uart_of_match);

static struct platform_driver endeavour_uart_platform_driver = {
  .probe = endeavour_uart_probe,
  //.remove_new = endeavour_uart_remove,
  .driver = {
    .name = KBUILD_MODNAME,
    .of_match_table = endeavour_uart_of_match,
  },
};

static bool output_to_sbi = false;

void set_endeavour_sbi_console(bool v);
void set_endeavour_sbi_console(bool v) { output_to_sbi = v; }

static void endeavour_console_write(struct console *con, const char *s, unsigned int count)
{
  if (output_to_sbi || !global_port) {
    register uintptr_t a1 asm ("a1") = (uintptr_t)(0);
    register uintptr_t a6 asm ("a6") = (uintptr_t)(0);
    register uintptr_t a7 asm ("a7") = (uintptr_t)(0x0A000000);
    for (int i = 0; i < count; ++i) {
      register uintptr_t a0 asm ("a0") = (uintptr_t)(s[i]);
      asm volatile ("ecall" : : "r" (a0), "r" (a1), "r" (a6), "r" (a7));
    }
  } else {
    unsigned long flags;
    uart_port_lock_irqsave(global_port, &flags);
    uart_console_write(global_port, s, count, endeavour_uart_putchar);
    uart_port_unlock_irqrestore(global_port, flags);
  }
}

static int __init earlycon_setup(struct earlycon_device *device, const char *opt) {
  device->con->write = endeavour_console_write;
  return 0;
}
EARLYCON_DECLARE(endeavour_sbi, earlycon_setup);

static int endeavour_uart_console_setup(struct console *co, char *options)
{
  int baud = 115200;
  int bits = 8;
  int parity = 'e';
  int flow = 'n';

  if (!global_port || !global_port->membase)
    return -ENODEV;

  if (options) {
    char* uart_options = options;
    if (strncmp(options, "sbi:", 4) == 0) {
      output_to_sbi = true;
      uart_options += 4;
    }
    printk(KERN_INFO "endeavour console options: %s\n", options);
    uart_parse_options(uart_options, &baud, &parity, &bits, &flow);
  }

  return uart_set_options(global_port, co, baud, parity, bits, flow);
}

static struct console endeavour_console = {
  .name = "ttyS",
  .write = endeavour_console_write,
  .device = uart_console_device,
  .setup = endeavour_uart_console_setup,
  .flags = CON_PRINTBUFFER,
  .index = -1,
  .data = &endeavour_uart_driver,
};

static int __init endeavour_uart_init(void) {
  int res;

  res = uart_register_driver(&endeavour_uart_driver);
  if (res)
    return res;

  res = platform_driver_register(&endeavour_uart_platform_driver);
  if (res)
    uart_unregister_driver(&endeavour_uart_driver);

  return res;
}

static void __exit endeavour_uart_exit(void) {
  platform_driver_unregister(&endeavour_uart_platform_driver);
  uart_unregister_driver(&endeavour_uart_driver);
}

module_init(endeavour_uart_init);
module_exit(endeavour_uart_exit);


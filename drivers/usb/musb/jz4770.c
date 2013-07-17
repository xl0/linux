/*
 * Author: River <zwang@ingenic.cn>
 * Restructured by Maarten ter Huurne <maarten@treewalker.org>, using the
 * tusb6010 module as a template.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/usb/otg.h>
#include <linux/usb/nop-usb-xceiv.h>
#include <linux/act8600_power.h>

#include <asm/mach-jz4770/board-gcw0.h>
#include <asm/mach-jz4770/jz4770cpm.h>
#include <asm/mach-jz4770/jz4770intc.h>
#include <asm/mach-jz4770/jz4770gpio.h>

#include "musb_core.h"


struct jz_musb_glue {
	struct device		*dev;
	struct platform_device	*musb;
};

static inline void jz_musb_phy_enable(void)
{
	printk(KERN_INFO "jz4760: Enable USB PHY.\n");

	__cpm_enable_otg_phy();

	/* Wait PHY Clock Stable. */
	udelay(300);
}

static inline void jz_musb_phy_disable(void)
{
	printk(KERN_INFO "jz4760: Disable USB PHY.\n");

	__cpm_suspend_otg_phy();
}

static inline void jz_musb_phy_reset(void)
{
	REG_CPM_USBPCR |= USBPCR_POR;
	udelay(30);
	REG_CPM_USBPCR &= ~USBPCR_POR;

	udelay(300);
}

static inline void jz_musb_set_device_only_mode(void)
{
	printk(KERN_INFO "jz4760: Device only mode.\n");

	/* Device Mode. */
	REG_CPM_USBPCR &= ~(USBPCR_USB_MODE);

	REG_CPM_USBPCR |= USBPCR_VBUSVLDEXT;
}

static inline void jz_musb_set_normal_mode(void)
{
	printk(KERN_INFO "jz4760: Normal mode.\n");

	__gpio_as_otg_drvvbus();

	/* OTG Mode. */
	REG_CPM_USBPCR |= USBPCR_USB_MODE;
	REG_CPM_USBPCR &= ~(USBPCR_VBUSVLDEXT |
				USBPCR_VBUSVLDEXTSEL |
				USBPCR_OTG_DISABLE);

	REG_CPM_USBPCR |= (USBPCR_IDPULLUP0 | USBPCR_IDPULLUP1);
}

static inline void jz_musb_init_regs(struct musb *musb)
{

	printk(KERN_NOTICE "Before jz_musb_init_regs:\n");
	usbpcr_debug();


	REG_CPM_USBPCR = 

	/* fil */
	REG_CPM_USBVBFIL = 0x80;

	/* rdt */
	REG_CPM_USBRDT = 0x96;

	/* rdt - filload_en */
	REG_CPM_USBRDT |= (1 << 25);

	/* TXRISETUNE & TXVREFTUNE. */
//	REG_CPM_USBPCR &= ~(USBPCR_TXVREFTUNE_MASK | USBPCR_TXRISETUNE_MASK);

//	REG_CPM_USBPCR |= 0x35;

	jz_musb_set_normal_mode();

	jz_musb_phy_reset();
	printk(KERN_NOTICE "jz_musb_init_regs:\n");
	usbpcr_debug();
}

static void jz_musb_set_vbus(struct musb *musb, int is_on)
{
	u8 devctl = musb_readb(musb->mregs, MUSB_DEVCTL);
	MY_TRACE;
	WARN_ON(is_on && is_peripheral_active(musb));


	/* HDRC controls CPEN, but beware current surges during device
	 * connect.  They can trigger transient overcurrent conditions
	 * that must be ignored.
	 */

	if (is_on) {
		musb->is_active = 1;
		musb->xceiv->otg->default_a = 1;
		usb_phy_chstate(musb->xceiv, OTG_STATE_A_WAIT_VRISE);
		devctl |= MUSB_DEVCTL_SESSION;

		MUSB_HST_MODE(musb);
		act8600_q_set(1, 1);
	} else {
		musb->is_active = 0;

		act8600_q_set(1, 0);
		/* NOTE:  we're skipping A_WAIT_VFALL -> A_IDLE and
		 * jumping right to B_IDLE...
		 */

		musb->xceiv->otg->default_a = 0;
		usb_phy_chstate(musb->xceiv, OTG_STATE_B_IDLE);
		devctl &= ~MUSB_DEVCTL_SESSION;

		MUSB_DEV_MODE(musb);
	}
	musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);

	dev_dbg(musb->xceiv->dev, "VBUS %s, devctl %02x "
		/* otg %3x conf %08x prcm %08x */ "\n",
		otg_state_string(musb->xceiv->state),
		musb_readb(musb->mregs, MUSB_DEVCTL));
}

/* ---------------------- OTG ID PIN Routines ---------------------------- */

static struct timer_list otg_id_pin_stable_timer;

static unsigned int read_gpio_pin(unsigned int pin, unsigned int loop)
{
	unsigned int t, v;
	unsigned int i;

	i = loop;

	v = t = 0;

	while (i--) {
		t = __gpio_get_pin(pin);
		if (v != t)
			i = loop;

		v = t;
	}

	return v;
}

static void do_otg_id_pin_state(struct musb *musb)
{
	struct device *dev = musb->controller;
	unsigned int default_a;
	unsigned int pin = read_gpio_pin(GPIO_OTG_ID_PIN, 5000);

	dev_info(dev, "USB OTG ID pin state: %d\n", pin);

	default_a = !pin;

	musb->xceiv->otg->default_a = default_a;

//	jz_musb_set_vbus(musb, default_a);

	if (pin) {
		/* B */
#ifdef CONFIG_USB_MUSB_PERIPHERAL_HOTPLUG
		__gpio_unmask_irq(OTG_HOTPLUG_PIN);
#endif
		__gpio_as_irq_fall_edge(GPIO_OTG_ID_PIN);
	} else {
		/* A */
#ifdef CONFIG_USB_MUSB_PERIPHERAL_HOTPLUG
		__gpio_mask_irq(OTG_HOTPLUG_PIN); // otg's host mode not support hotplug
#endif
		__gpio_as_irq_rise_edge(GPIO_OTG_ID_PIN);
	}
}

static void otg_id_pin_stable_func(unsigned long data)
{
	struct musb *musb = (struct musb *)data;

	do_otg_id_pin_state(musb);
}

static irqreturn_t jz_musb_otg_id_irq(int irq, void *data)
{
	mod_timer(&otg_id_pin_stable_timer, GPIO_OTG_STABLE_JIFFIES + jiffies);

	return IRQ_HANDLED;
}

static int otg_id_pin_setup(struct musb *musb)
{
	struct device *dev = musb->controller;
	int ret;

	ret = devm_gpio_request(dev, GPIO_OTG_ID_PIN, "USB OTG ID");
	if (ret) {
		dev_err(dev, "Failed to request USB OTG ID pin: %d\n", ret);
		return ret;
	}
	/*
	 * Note: If pull is enabled, the initial state is read as 0 regardless
	 *       of whether something is plugged or not.
	 */
	__gpio_as_input(GPIO_OTG_ID_PIN);
	__gpio_disable_pull(GPIO_OTG_ID_PIN);

	__gpio_as_input(OTG_HOTPLUG_PIN);
	__gpio_disable_pull(OTG_HOTPLUG_PIN);

	/* Update OTG ID PIN state. */
	do_otg_id_pin_state(musb);
	setup_timer(&otg_id_pin_stable_timer, otg_id_pin_stable_func, (unsigned long)musb);

	ret = request_irq(GPIO_OTG_ID_IRQ, jz_musb_otg_id_irq,
				IRQF_DISABLED, "otg-id-irq", musb);
	if (ret) {
		dev_err(dev, "Failed to request USB OTG ID IRQ: %d\n", ret);
		return ret;
	}

	return ret;
}

static void otg_id_pin_cleanup(struct musb *musb)
{
	free_irq(GPIO_OTG_ID_IRQ, "otg-id-irq");
	del_timer(&otg_id_pin_stable_timer);
}

/* ---------------------------------------------------------------- */

static irqreturn_t jz_musb_interrupt(int irq, void *__hci)
{
	unsigned long	flags;
	struct musb	*musb = __hci;

	irqreturn_t rv, rv_dma, rv_usb;
	rv = rv_dma = rv_usb = IRQ_NONE;

	spin_lock_irqsave(&musb->lock, flags);

	MY_TRACE;
	printk("MUSB mode: %s\n", MUSB_MODE(musb));
	usbpcr_debug();
	musb_power_show(musb_readb(musb->mregs, MUSB_POWER));
	musb_devctl_show(musb_readb(musb->mregs, MUSB_DEVCTL));
	musb_intr_rx_enable_show(musb_readb(musb->mregs, MUSB_INTRRXE));
	musb_intr_tx_enable_show(musb_readb(musb->mregs, MUSB_INTRTXE));


#if defined(CONFIG_USB_INVENTRA_DMA)
	if (musb->b_dma_share_usb_irq) 
		rv_dma = musb_call_dma_controller_irq(irq, musb);
#endif

	musb->int_usb = musb_readb(musb->mregs, MUSB_INTRUSB);
	musb->int_tx = musb_readw(musb->mregs, MUSB_INTRTX);
	musb->int_rx = musb_readw(musb->mregs, MUSB_INTRRX);

	musb_intrusb_show(musb->int_usb);
	musb_intr_rx_show(musb->int_rx);
	musb_intr_tx_show(musb->int_tx);


	if (musb->int_usb || musb->int_tx || musb->int_rx)
		rv_usb = musb_interrupt(musb);

	spin_unlock_irqrestore(&musb->lock, flags);

	rv = (rv_dma == IRQ_HANDLED || rv_usb == IRQ_HANDLED) ?
		IRQ_HANDLED : IRQ_NONE;

	return rv;
}

static ssize_t show_id(struct device *dev, struct device_attribute *attr,
	 char *buf)
{
	return sprintf(buf, "%d\n", read_gpio_pin(GPIO_OTG_ID_PIN, 5000));
}

static ssize_t show_vbus(struct device *dev, struct device_attribute *attr,
	 char *buf)
{
	return sprintf(buf, "%d\n", read_gpio_pin(OTG_HOTPLUG_PIN, 5000));
}

static DEVICE_ATTR(id_pin, 0666, show_id, NULL);
static DEVICE_ATTR(vbus_pin, 0666, show_vbus, NULL);

static int jz_musb_create_attrs(struct musb *musb)
{
	int ret;

	ret = device_create_file(musb->controller, &dev_attr_id_pin);
	if (ret)
		goto out;

	ret = device_create_file(musb->controller, &dev_attr_vbus_pin);
	if (ret) {
		device_remove_file(musb->controller, &dev_attr_id_pin);
		goto out;
	}
out:
	return ret;
}

static void jz_musb_remove_attrs(struct musb *musb)
{
	device_remove_file(musb->controller, &dev_attr_id_pin);
	device_remove_file(musb->controller, &dev_attr_vbus_pin);
}

static int __init jz_musb_platform_init(struct musb *musb)
{
	int ret;

	musb->xceiv = usb_get_phy(USB_PHY_TYPE_USB2);
	if (!musb->xceiv) {
		pr_err("HS USB OTG: no transceiver configured\n");
		return -ENODEV;
	}

	musb->b_dma_share_usb_irq = 1;
	musb->isr = jz_musb_interrupt;

	jz_musb_init_regs(musb);

	/* host mode and otg(host) depend on the id pin */
	ret = otg_id_pin_setup(musb);
	if (ret)
		goto out;

	ret = jz_musb_create_attrs(musb);
	if (ret) {
		otg_id_pin_cleanup(musb);
		goto out;
	}
out:
	return ret;
}

static int jz_musb_platform_exit(struct musb *musb)
{
	jz_musb_remove_attrs(musb);

	jz_musb_phy_disable();

	otg_id_pin_cleanup(musb);

	usb_nop_xceiv_unregister();

	return 0;
}

static void jz_musb_platform_enable(struct musb *musb)
{
	jz_musb_phy_enable();
}

static void jz_musb_platform_disable(struct musb *musb)
{
	jz_musb_phy_disable();
}


static const struct musb_platform_ops jz_musb_ops = {
	.init		= jz_musb_platform_init,
	.exit		= jz_musb_platform_exit,

	.enable		= jz_musb_platform_enable,
	.disable	= jz_musb_platform_disable,

	.set_vbus	= jz_musb_set_vbus,
};

static u64 jz_musb_dmamask = DMA_BIT_MASK(32);

static int __init jz_musb_probe(struct platform_device *pdev)
{
	struct musb_hdrc_platform_data	*pdata = pdev->dev.platform_data;
	struct platform_device		*musb;
	struct jz_musb_glue		*glue;

	int				ret = -ENOMEM;

	glue = kzalloc(sizeof(*glue), GFP_KERNEL);
	if (!glue) {
		dev_err(&pdev->dev, "failed to allocate glue context\n");
		goto err0;
	}

	musb = platform_device_alloc("musb-hdrc", PLATFORM_DEVID_AUTO);
	if (!musb) {
		dev_err(&pdev->dev, "failed to allocate musb device\n");
		goto err1;
	}

	musb->dev.parent		= &pdev->dev;
	musb->dev.dma_mask		= &jz_musb_dmamask;
	musb->dev.coherent_dma_mask	= jz_musb_dmamask;

	glue->dev			= &pdev->dev;
	glue->musb			= musb;

	pdata->platform_ops		= &jz_musb_ops;

	platform_set_drvdata(pdev, glue);

	ret = platform_device_add_resources(musb, pdev->resource,
			pdev->num_resources);
	if (ret) {
		dev_err(&pdev->dev, "failed to add resources\n");
		goto err2;
	}

	ret = platform_device_add_data(musb, pdata, sizeof(*pdata));
	if (ret) {
		dev_err(&pdev->dev, "failed to add platform_data\n");
		goto err2;
	}

	ret = platform_device_add(musb);
	if (ret) {
		dev_err(&pdev->dev, "failed to register musb device\n");
		goto err2;
	}

	return 0;

err2:
	platform_device_put(musb);

err1:
	kfree(glue);

err0:
	return ret;
}

static int __exit jz_musb_remove(struct platform_device *pdev)
{
	struct jz_musb_glue		*glue = platform_get_drvdata(pdev);

	platform_device_del(glue->musb);
	platform_device_put(glue->musb);
	kfree(glue);

	return 0;
}

static struct platform_driver jz_musb_driver = {
	.remove		= __exit_p(jz_musb_remove),
	.driver		= {
		.name	= "musb-jz",
	},
};

static int __init jz_musb_init(void)
{
	return platform_driver_probe(&jz_musb_driver, jz_musb_probe);
}
subsys_initcall(jz_musb_init);

static void __exit jz_musb_exit(void)
{
	platform_driver_unregister(&jz_musb_driver);
}
module_exit(jz_musb_exit);

MODULE_DESCRIPTION("JZ4770 MUSB Glue Layer");
MODULE_AUTHOR("River <zwang@ingenic.cn>");
MODULE_LICENSE("GPL v2");

/*
 * PMU driver for act8600 PMU
 *
 * Copyright 2010 Ingenic Semiconductor LTD.
 * Copyright 2012 Maarten ter Huurne <maarten@treewalker.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <asm/types.h>
#include <linux/act8600_power.h>
#include <linux/slab.h>

#if 0
#define dprintk(x...) printk(x)
#else
#define dprintk(x...)
#endif

#define ACT8600_Q_REG	0xb0

extern void i2c_jz_setclk(struct i2c_client *client,unsigned long i2cclk);


struct act8600_device {
	struct i2c_client *client;
};

struct act8600_device *act8600 = NULL;

static int act8600_write_reg(u8 reg, u8 val)
{
	char msg[2];

	if (!act8600) {
		dprintk("!!! act8600_write_reg before init\n");
		return -ENODEV;
	}
	dprintk("act8600_write_reg(%02X, %02X)\n", reg, val);

	memcpy(&msg[0], &reg, 1);
	memcpy(&msg[1], &val, 1);

	return i2c_master_send(act8600->client, msg, 2);
}

static int act8600_read_reg(u8 reg, u8 *val)
{
	int ret;

	if (!act8600) {
		dprintk("!!! act8600_read_reg before init\n");
		return -ENODEV;
	}

	i2c_master_send(act8600->client, &reg, 1);
	ret = i2c_master_recv(act8600->client, val, 1);
	dprintk("act8600_read_reg(%02X) = %02X\n", reg, *val);
	return ret;
}

static int act8600_output_voltage_set(int outnum, u8 value)
{
	if (outnum < 1 || outnum > 8)
		return -EINVAL;

	/* write VSET register */
	return act8600_write_reg(outnum * 0x10, value);
}

static const u8 act8600_on_regs[] = {
	0x12, 0x22, 0x32, 0x41, 0x51, 0x61, 0x71, 0x81
};

int act8600_output_enable(int outnum, bool enable)
{
	u8 reg, value;

	if (outnum < 1 || outnum > 8)
		return -EINVAL;
	reg = act8600_on_regs[outnum - 1];

	/* write ON bit */
	act8600_read_reg(reg, &value);
	value = (value & ~(1 << 7)) | ((u8)enable << 7);
	return act8600_write_reg(reg, value);
}
EXPORT_SYMBOL_GPL(act8600_output_enable);

static int act8600_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	int i;
	struct act8600_platform_pdata_t *pdata = i2c->dev.platform_data;

	act8600 = kzalloc(sizeof(struct act8600_device), GFP_KERNEL);
	if (!act8600 )
		return -ENOMEM;

	act8600->client = i2c;
	i2c_set_clientdata(i2c, act8600);

	dprintk("act8600_power:\n");
	for (i = 0; i < pdata->nr_outputs; i++) {
		struct act8600_outputs_t *p = &pdata->outputs[i];
		act8600_output_voltage_set(p->outnum, p->value);
		act8600_output_enable(p->outnum, p->enable);

		dprintk("%d\t\t%d\t\t%d\n", p->outnum, p->value, p->enable);
	}

	/* MtH: The bits changed by these writes are undocumented. */
#if 1
	act8600_write_reg(0x91, 0xd0);
	act8600_write_reg(0x90, 0x17);
	act8600_write_reg(0x91, 0xc0);
#endif
	return 0;
}


int act8600_q_set(int q, int enable)
{
	u8 tmp;
	int ret;

	if (q < 1 || q > 3)
		return -EINVAL;

	if (!enable) {
		ret = act8600_write_reg(ACT8600_Q_REG, 0);
		WARN_ON(ret < 0);
		goto out;
	}

	/* When enabling a switch, the other two must me disabled.
	 * q1,2,3 are bits 7, 6, 5. */

	ret = act8600_write_reg(ACT8600_Q_REG, (1 << (8 - q)));
	if (ret < 0) {
		__WARN();
		goto out;
	}

	/* bits 4 and 3 are "status bits" for q1 and a2. 0 -> success. */
	ret = act8600_read_reg(ACT8600_Q_REG, &tmp);
	if (ret < 0) {
		__WARN();
		goto out;
	}

	if (!(tmp & (1 << (5 - q)))) {
		__WARN();
		ret = -EIO;
	}
out:
	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL_GPL(act8600_q_set);


static int act8600_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id act8600_id[] = {
	{ ACT8600_NAME, 0 },
	{ }
};

static struct i2c_driver act8600_pmu_driver = {
	.probe		= act8600_probe,
	.remove		= act8600_remove,
	.id_table	= act8600_id,
	.driver = {
		.name	= ACT8600_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init act8600_pmu_init(void)
{
	return i2c_add_driver(&act8600_pmu_driver);
}

static void __exit act8600_pmu_exit(void)
{
	i2c_del_driver(&act8600_pmu_driver);
}

subsys_initcall(act8600_pmu_init);
module_exit(act8600_pmu_exit);

MODULE_DESCRIPTION("ACT8600 PMU Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ztyan@ingenic.cn");

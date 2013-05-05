/*
 * drivers/power/act8600_power.h -- Core interface for ACT8600
 *
 * Copyright 2010 Ingenic Semiconductor LTD.
 * Copyright 2012 Maarten ter Huurne <maarten@treewalker.org>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __ACT8600_POWER_H__
#define __ACT8600_POWER_H__

#include <linux/types.h>

#define ACT8600_NAME		"act8600"
#define ACT8600_I2C_ADDR	0x5A

struct act8600_outputs_t {
	int outnum;
	int value;
	bool enable;
};

struct act8600_platform_pdata_t {
	struct act8600_outputs_t *outputs;
	int nr_outputs;
};

/**
 * act8600_output_enable - Enable or disable one of the power outputs.
 * @outnum: output pin: 1-8
 * @enable: true to enable, false to disable
 * @returns zero on success and error code upon failure
 */
int act8600_output_enable(int outnum, bool enable);

/**
 * act8600_q_set - enable/disable a Q awitch.
 * The Q[123] switches are used to control the USB VBUS line.
 * Q1 connects the line to the 5V input line, powering the line.
 * Q2 connects it to the VSYS output, powering the line.
 * Q3 connects it to CHGIN, and is used to power the sysrem from USB.
 *
 * Q1 and Q2 switch off automatically when the current is over 700 ma.
 * Q2 switches off when the CHGIN voltage is over 6v.
 * @q - q switch index, 1, 2, or 3.
 * @enable - enable or disable it, 1 or 0.
 * @returns 0 in success, -error core on failure.
 */
int act8600_q_set(int q, bool enable);

#endif  /* __ACT8600_POWER_H__ */

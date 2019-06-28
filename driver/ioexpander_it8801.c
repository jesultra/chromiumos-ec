/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "driver/ioexpander_it8801.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "registers.h"
#include "task.h"

static int i2c_init_done;

/*
 * Initialize the raw keyboard interface.
 */
void keyboard_raw_init(void)
{
	/*
	 * TODO(b:): The I/O expander communicated with EC
	 * is through I2C, but the I2C is not ready during
	 * initialization of the keyboard raw. So this
	 * function can not do anything.
	 */
}

/*
 * Finish initialization after task scheduling has started.
 */
void keyboard_raw_task_start(void)
{
	/* KSO alternate function switching(KSO[21:18]) */
	i2c_write8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
		IT8801_REG_GPIO00_KSO19, IT8801_REG_MASK_GPIOAFS_FUNC2);
	i2c_write8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
		IT8801_REG_GPIO01_KSO18, IT8801_REG_MASK_GPIOAFS_FUNC2);
	i2c_write8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
		IT8801_REG_GPIO22_KSO21, IT8801_REG_MASK_GPIOAFS_FUNC2);
	i2c_write8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
		IT8801_REG_GPIO23_KSO20, IT8801_REG_MASK_GPIOAFS_FUNC2);

	if (IS_ENABLED(CONFIG_KEYBOARD_COL2_INVERTED))
		/* KSO[2] is high, others are low. */
		i2c_write8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
			IT8801_REG_KSOMCR, IT8801_REG_MASK_KSOSDIC | 0x02);
	else
		/* KSO[21:18,12:11,6:0] pins low. */
		i2c_write8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
			IT8801_REG_KSOMCR, IT8801_REG_MASK_AKSOSC);

	/* Keyboard scan in interrupt enable register */
	i2c_write8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
		IT8801_REG_KSIIER, 0xff);
	/* Gather KSI interrupt enable */
	i2c_write8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
		IT8801_REG_GIECR, IT8801_REG_MASK_GKSIIE);
	/* Alert response enable */
	i2c_write8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
		IT8801_REG_SMBCR, IT8801_REG_MASK_ARE);

	keyboard_raw_enable_interrupt(1);
}

/* Column mapping to KSO of IT8801 */
static const uint8_t kso_mapping[18] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
	0x06, 0x12, 0x13, 0x14, 0x15, 0x0b,
	0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11
};

/*
 * Drive the specified column low.
 */
test_mockable void keyboard_raw_drive_column(int col)
{
	int kso_val = 0;

	if (!i2c_init_done)
		return;

	/* Tri-state all outputs */
	if (col == KEYBOARD_COLUMN_NONE)
		/* KSO[21:18,12:11,6:0] output high */
		kso_val = IT8801_REG_MASK_KSOSDIC | IT8801_REG_MASK_AKSOSC;
	/* Assert all outputs */
	else if (col == KEYBOARD_COLUMN_ALL)
		/* KSO[21:18,12:11,6:0] output low */
		kso_val = IT8801_REG_MASK_AKSOSC;
	/* Selected KSO output low, all others KSO pull high. */
	else
		kso_val = kso_mapping[col];

	if (IS_ENABLED(CONFIG_KEYBOARD_COL2_INVERTED))
		/* KSO[2] is inverted. */
		i2c_write8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
			IT8801_REG_KSOMCR, kso_val ^= 0x02);
	else
		i2c_write8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
			IT8801_REG_KSOMCR, kso_val);
}

/*
 * Read raw row state.
 * Bits are 1 if signal is present, 0 if not present.
 */
test_mockable int keyboard_raw_read_rows(void)
{
	int data, ksieer;

	if (!i2c_init_done)
		return 0;

	i2c_read8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
		IT8801_REG_KSIDR, &data);

	/* This register needs to write clear after reading data */
	i2c_read8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
		IT8801_REG_KSIEER, &ksieer);
	i2c_write8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
		IT8801_REG_KSIEER, ksieer);

	/* Bits are active-low, so invert returned levels */
	return data ^ 0xff;
}

/*
 * Enable or disable keyboard matrix scan interrupts.
 */
void keyboard_raw_enable_interrupt(int enable)
{
	if (!i2c_init_done)
		return;

	if (enable) {
		i2c_write8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
			IT8801_REG_KSIEER, 0xff);
		gpio_clear_pending_interrupt(GPIO_IT8801_SMB_INT);
		gpio_enable_interrupt(GPIO_IT8801_SMB_INT);
	} else {
		gpio_disable_interrupt(GPIO_IT8801_SMB_INT);
	}
}

/*
 * Interrupt handler for keyboard matrix scan interrupt.
 */
void io_expander_it8801_interrupt(enum gpio_signal signal)
{
	/* Wake the scan task */
// 	task_wake(TASK_ID_KEYSCAN);
}

/*
 * Check the I2C function has been initialized.
 */
static void i2c_init_check(void)
{
	i2c_init_done = 1;
}
DECLARE_HOOK(HOOK_INIT, i2c_init_check, HOOK_PRIO_INIT_I2C + 1);

static void dump_register(int reg)
{
	int rv;
	int data;

	ccprintf("[%Xh] = ", reg);

	rv = i2c_read8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_REG_ADDR,
		reg, &data);

	if (!rv)
		ccprintf("0x%02x\n", data);
	else
		ccprintf("ERR (%d)\n", rv);
}

static int it8801_dump(int argc, char **argv)
{
	dump_register(IT8801_REG_KSIIER);
	dump_register(IT8801_REG_KSIEER);
	dump_register(IT8801_REG_KSIDR);
	dump_register(IT8801_REG_KSOMCR);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(it8801_dump, it8801_dump, "NULL",
			"Dumps IT8801 registers");

/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ITE IT5205 USB Type-C USB alternate mode mux.
 */

#include "common.h"
#include "console.h"
#include "i2c.h"
#include "usb_mux.h"
#include "usb_mux_it5205.h"
#include "util.h"

#define MUX_STATE_DP_USB_MASK (MUX_USB_ENABLED | MUX_DP_ENABLED)

static int it5205_read(int i2c_addr, uint8_t reg, int *val)
{
	return i2c_read8(I2C_PORT_USB_MUX, i2c_addr, reg, val);
}

static int it5205_write(int i2c_addr, uint8_t reg, uint8_t val)
{
	return i2c_write8(I2C_PORT_USB_MUX, i2c_addr, reg, val);
}

static int it5205_init(int i2c_addr)
{
	int val, ret;

	/*  Verify chip ID registers. */
	ret = it5205_read(i2c_addr, IT5205_REG_CHIP_ID3, &val);
	if (ret)
		return ret;
	if (val != IT5205_CHIP_ID3)
		return EC_ERROR_UNKNOWN;

	ret = it5205_read(i2c_addr, IT5205_REG_CHIP_ID2, &val);
	if (ret)
		return ret;
	if (val != IT5205_CHIP_ID2)
		return EC_ERROR_UNKNOWN;

	ret = it5205_read(i2c_addr, IT5205_REG_CHIP_ID1, &val);
	if (ret)
		return ret;
	if (val != IT5205_CHIP_ID1)
		return EC_ERROR_UNKNOWN;

	ret  = it5205_read(i2c_addr, IT5205_REG_CHIP_ID0, &val);
	if (ret)
		return ret;
	if (val != IT5205_CHIP_ID0)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

/* Writes control register to set switch mode */
static int it5205_set_mux(int i2c_addr, mux_state_t mux_state)
{
	uint8_t reg = 0;

	if ((mux_state & MUX_STATE_DP_USB_MASK) == MUX_USB_ENABLED)
		reg |= IT5205_MODE_USB;
	else if ((mux_state & MUX_STATE_DP_USB_MASK) == MUX_DP_ENABLED)
		reg |= IT5205_MODE_DP;
	else if ((mux_state & MUX_STATE_DP_USB_MASK) == MUX_STATE_DP_USB_MASK)
		reg |= IT5205_MODE_DP_USB;

	if (mux_state & MUX_POLARITY_INVERTED)
		reg |= IT5205_MODE_POLARITY_INVERTED;

	return it5205_write(i2c_addr, IT5205_REG_MUXCR, reg);
}

/* Reads control register and updates mux_state accordingly */
static int it5205_get_mux(int i2c_addr, mux_state_t *mux_state)
{
	int reg, ret;

	ret = it5205_read(i2c_addr, IT5205_REG_MUXCR, &reg);
	if (ret)
		return ret;

	*mux_state = 0;
	if ((reg & IT5205_MODE_DP_USB_MASK) == IT5205_MODE_USB)
		*mux_state |= MUX_USB_ENABLED;
	else if ((reg & IT5205_MODE_DP_USB_MASK) == IT5205_MODE_DP)
		*mux_state |= MUX_DP_ENABLED;
	else if ((reg & IT5205_MODE_DP_USB_MASK) == IT5205_MODE_DP_USB)
		*mux_state |= MUX_STATE_DP_USB_MASK;

	if (reg & IT5205_MODE_POLARITY_INVERTED)
		*mux_state |= MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

static int console_command_it5205(int argc, char **argv)
{
	int ret, addr, reg, val;
	char *e;

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	addr = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	reg = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	if (!strcasecmp(argv[1], "w")) {
		if (argc < 5)
			return EC_ERROR_PARAM_COUNT;

		val = strtoi(argv[4], &e, 0);
		if (*e)
			return EC_ERROR_PARAM4;

		ret = it5205_write(addr, reg, val);
		if (ret)
			return EC_ERROR_ACCESS_DENIED;
	}

	ret = it5205_read(addr, reg, &val);
	if (ret)
		return EC_ERROR_UNKNOWN;

	ccprintf("it5205 addr:%x reg:%xh value is %xh\n", addr, reg, val);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(it5205, console_command_it5205,
			"it5205 [r/w] [addr] [reg] | [val]",
			"Read or write a mux register");

const struct usb_mux_driver it5205_usb_mux_driver = {
	.init = it5205_init,
	.set = it5205_set_mux,
	.get = it5205_get_mux,
};

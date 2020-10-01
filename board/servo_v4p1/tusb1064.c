/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c.h"
#include "tusb1064.h"
#include "ioexpanders.h"
#include "usb_mux.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/* Generic driver init function */
static int tusb1064_init(const struct usb_mux *me)
{
	/* Disconnect USB3.1 and DP */
	int reg;
	int val;

	reg =( (0 & REG_GENERAL_DP_EN_CTRL)	|
		 REG_GENERAL_CTLSEL_DISABLE |
		 REG_GENERAL_EQ_OVERRIDE);
	val=tusb1064_write_byte(me->i2c_port, TUSB1064_REG_GENERAL, reg);
	if (val)
		return val;

	return EC_SUCCESS;


	return init_tusb1064(me->i2c_port);
}

/* Writes control register to set switch mode */
static int tusb1064_set_mux(const struct usb_mux *me, mux_state_t mux_state)
{
	int reg;
	CPRINTS("tusb1064_set_mux 0x%X",mux_state);

	reg =( (0 & REG_GENERAL_DP_EN_CTRL)
		| REG_GENERAL_CTLSEL_DISABLE );

	if (mux_state & USB_PD_MUX_USB_ENABLED)
		reg |= REG_GENERAL_CTLSEL_USB3;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		reg |= REG_GENERAL_CTLSEL_ANYDP;
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		reg |= REG_GENERAL_FLIPSEL;

	return tusb1064_write(me, TUSB1064_REG_GENERAL, reg);
}

/* Reads control register and updates mux_state accordingly */
static int tusb1064_get_mux(const struct usb_mux *me, mux_state_t *mux_state)
{
	int reg;
	int val;

	val = tusb1064_read(me, TUSB1064_REG_GENERAL, &reg);
	if(val)
		return EC_ERROR_INVAL;

	*mux_state = 0;
	if (reg & REG_GENERAL_CTLSEL_USB3)
		*mux_state |= USB_PD_MUX_USB_ENABLED;
	if (reg & REG_GENERAL_CTLSEL_ANYDP)
		*mux_state |= USB_PD_MUX_DP_ENABLED;
	if (reg & REG_GENERAL_FLIPSEL)
		*mux_state |= USB_PD_MUX_POLARITY_INVERTED;

	// TODO: Incorporate AUX Override bits too.
	CPRINTS("tusb1064_get_mux 0x%X", *mux_state);
	return EC_SUCCESS;
}

int init_tusb1064(int port)
{
	int val, reg;

	/* Disconnect USB3.1 and DP */
	reg =( (0 & REG_GENERAL_DP_EN_CTRL) |
		REG_GENERAL_CTLSEL_DISABLE |
		REG_GENERAL_EQ_OVERRIDE );
	val=tusb1064_write_byte(port, TUSB1064_REG_GENERAL, reg);
	if (val)
		return val;
	
	/* Default to "Floating Pin" DP Equalization */
	reg=TUSB1064_DP1EQ(TUSB1064_DP_EQ_RX_10_0_DB) |
			TUSB1064_DP3EQ(TUSB1064_DP_EQ_RX_10_0_DB);
	val=tusb1064_write_byte(port, TUSB1064_REG_DP1DP3EQ_SEL, reg);
	if (val)
		return val;

	reg=TUSB1064_DP0EQ(TUSB1064_DP_EQ_RX_10_0_DB) |
			TUSB1064_DP2EQ(TUSB1064_DP_EQ_RX_10_0_DB);
	val=tusb1064_write_byte(port, TUSB1064_REG_DP0DP2EQ_SEL, reg);
	if (val)
		return val;

	#if 0
	val=tusb1064_read_byte(port, TUSB1064_REG_GENERAL, &reg);
	if (val)
		return val;
	/* Read Modify Write (test) */
	reg &= ~REG_GENERAL_CTLSEL_MASK;
	reg |= REG_GENERAL_CTLSEL_DISABLE;
	val=tusb1064_write_byte(port, TUSB1064_REG_GENERAL, reg);
	if(val)
		return val;
	#endif

	/* Disable AUX mux override */
	reg=((~TUSB1064_AUXDPCTRL_AUX_SNOOP_DISABLE & 0) |
		 (~TUSB1064_AUXDPCTRL_AUX_SBU_OVR & 0) |
		 (~(TUSB1064_AUXDPCTRL_DP3_DISABLE |
			TUSB1064_AUXDPCTRL_DP2_DISABLE |
			TUSB1064_AUXDPCTRL_DP1_DISABLE |
			TUSB1064_AUXDPCTRL_DP0_DISABLE) & 0) );
	val=tusb1064_write_byte(port, TUSB1064_REG_AUXDPCTRL, reg);
	if(val)
		return val;

	return EC_SUCCESS;
}

int tusb1064_read(const struct usb_mux *me, uint8_t reg, int *val)
{
	return i2c_read8(me->i2c_port, me->i2c_addr_flags,
			 reg, val);
}

int tusb1064_write(const struct usb_mux *me, uint8_t reg, int val)
{
	return i2c_write8(me->i2c_port, me->i2c_addr_flags,
			  reg, val);
}

// TODO: Get rid of these hardcoded functions
//--------------
int tusb1064_write_byte(int port, uint8_t reg, int val)
{
	return i2c_write8(port, TUSB1064_ADDR_FLAGS, reg, val);
}

int tusb1064_read_byte(int port, uint8_t reg, int *val)
{
	return i2c_read8(port, TUSB1064_ADDR_FLAGS, reg, val);
}
//--------------

const struct usb_mux_driver tusb1064_usb_mux_driver = {
	/* CAUTION: This is an UFP/RX/SINK redriver mux */
	/* Functions may be called backwards/DFP in TCPMv1 */
	.init = tusb1064_init,
	.set = tusb1064_set_mux,
	.get = tusb1064_get_mux,
};
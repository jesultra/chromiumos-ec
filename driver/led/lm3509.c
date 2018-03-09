/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI LM3509 LED driver.
 */

#include "i2c.h"
#include "lm3509.h"
#include "host_command.h"
#include "ec_commands.h"
#include "util.h"

inline int lm3509_write(uint8_t reg, uint8_t val)
{
	return i2c_write8(I2C_PORT_KBLIGHT, LM3509_I2C_ADDR, reg, val);
}

inline int lm3509_read(uint8_t reg, int *val)
{
	return i2c_read8(I2C_PORT_KBLIGHT, LM3509_I2C_ADDR, reg, val);
}

int lm3509_poweron(void)
{
	int ret = 0;

	/* BIT= description
	 * [2]= set both main and seconfary current same, both control by BMAIN.
	 * [1]= enable secondary current sink.
	 * [0]= enable main current sink.
	 */
	ret |= lm3509_write(LM3509_REG_GP, 0x07);
	/* Brigntness register
	 * 0x00= 0%
	 * 0x1F= 100%
	 */
	ret |= lm3509_write(LM3509_REG_BMAIN, 0x1F);

	return ret;
}

int lm3509_poweroff(void)
{
	int ret = 0;

	ret |= lm3509_write(LM3509_REG_GP, 0x00);
	ret |= lm3509_write(LM3509_REG_BMAIN, 0x00);

	return ret;
}

/* Transfer table for host command to actual lm3509 driver.
 * Due to host command range is 0~100%, but the lm3509
 * brightness level has floating point, actually we only
 * need to report an integer value to host. Consequently,
 * to enlarge brightness level ten times for calculation
 * and comparison.
 */
const int lm3509_brightness[32] = {
	  0,   1,   6,   10,
	 11,  13,  16,   20,
	 24,  28,  31,   37,
	 43,  52,  62,   75,
	 87, 100, 125,  150,
	168, 187, 225,  262,
	312, 375, 437,  525,
	612, 700, 875, 1000};

/*****************************************************************************/
/* Host commands */

static int hc_set_kb_light(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_keyboard_backlight *p = args->params;
	int ret;
	int val;
	int i, percent, level_high, level_low;

	if (p->percent == 0) {
		val = 0;
		goto write_kblight;
	}
	if (p->percent >= 100) {
		val = 31;
		goto write_kblight;
	}

	for (i = 1; i < ARRAY_SIZE(lm3509_brightness); i++) {

		level_high = lm3509_brightness[i];
		/* Enlarge the percentage ten times for comparison. */
		percent = ENLARGE_DECUPLE(p->percent);

		/* Compare the level set by host cmd with lm3509
		 * brightness level and calculate the nearest
		 * lm3509 brightness value.
		 */
		if (level_high < percent)
			continue;

		level_low = lm3509_brightness[i-1];
		if (level_high - percent > percent - level_low)
			val = i-1;
		else
			val = i;
		goto write_kblight;
	}

write_kblight:
	ret = lm3509_write(LM3509_REG_BMAIN, val);

	if (ret)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT,
		     hc_set_kb_light,
		     EC_VER_MASK(0));

static int hc_get_kb_light(struct host_cmd_handler_args *args)
{
	struct ec_response_pwm_get_keyboard_backlight *r = args->response;
	int ret;
	int val;

	ret = lm3509_read(LM3509_REG_BMAIN, &val);

	if (ret)
		return EC_RES_ERROR;

	/* Round return vlaue to the nearest integer */
	r->percent = ROUND_INTEGER(lm3509_brightness[val&0x1F]);

	if ((val&0x1F) > 0)
		r->enabled = 1;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_KEYBOARD_BACKLIGHT,
		     hc_get_kb_light,
		     EC_VER_MASK(0));

/*****************************************************************************/

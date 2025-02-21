/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "common.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "math_util.h"
#include "util.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/battery.h>

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_ERR);

enum battery_present battery_is_present(void)
{
	int state;

	if (gpio_get_level(GPIO_BATT_PRES_ODL)) {
		return BP_NO;
	}

	/*
	 *  According to the battery manufacturer's reply:
	 *  To detect a bad battery, need to read the 0x00 register.
	 *  If the 12th bit(Permanently Failure) is 1, it means a bad battery.
	 */
	if (sb_read(SB_MANUFACTURER_ACCESS, &state)) {
		return BP_NO;
	}

	/* Detect the 12th bit value */
	if (state & BIT(12)) {
		return BP_NO;
	}

	return BP_YES;
}

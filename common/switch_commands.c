/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO switch host commands for Chrome EC */

#include "board.h"
#include "gpio.h"
#include "host_command.h"
#include "util.h"


int switch_command_enable_backlight(uint8_t *data, int *resp_size)
{
	struct ec_params_switch_enable_backlight *p =
			(struct ec_params_switch_enable_backlight *)data;
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, p->enabled);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SWITCH_ENABLE_BKLIGHT,
		switch_command_enable_backlight);


int switch_command_enable_wireless(uint8_t *data, int *resp_size)
{
	struct ec_params_switch_enable_wireless *p =
			(struct ec_params_switch_enable_wireless *)data;
	gpio_set_level(GPIO_RADIO_ENABLE_WLAN, p->enabled);
	gpio_set_level(GPIO_RADIO_ENABLE_BT, p->enabled);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SWITCH_ENABLE_WIRELESS,
		switch_command_enable_wireless);

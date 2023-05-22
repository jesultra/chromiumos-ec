/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "battery.h"
#include "cbi.h"

#include <zephyr/drivers/gpio.h>

enum battery_present battery_hw_present(void)
{
	const struct gpio_dt_spec *batt_pres;

	batt_pres = GPIO_DT_FROM_NODELABEL(gpio_ec_batt_pres_odl);

	/*
	 * The GPIO is low when the battery is physically present.
	 * But if battery cell voltage < 2.5V, it will not able to
	 * pull down EC_BATT_PRES_ODL. So we need to set pre-charge
	 * current even EC_BATT_PRES_ODL is high.
	 */
	return gpio_pin_get_dt(batt_pres) ? BP_NOT_SURE : BP_YES;
}

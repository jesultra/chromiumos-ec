/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* MEC1322 eval board-specific configuration */

#include "gpio.h"
#include "registers.h"
#include "util.h"

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[] = {
	{"LED1", GPIO_PORT(15), (1 << 4), GPIO_ODR_LOW, NULL},
	{"LED2", GPIO_PORT(15), (1 << 5), GPIO_ODR_HIGH, NULL},
	{"LED3", GPIO_PORT(15), (1 << 6), GPIO_ODR_LOW, NULL},
	/* Unimplemented signals which we need to emulate for now */
	GPIO_SIGNAL_NOT_IMPLEMENTED("RECOVERYn"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("DEBUG_LED"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("WP"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("ENTERING_RW"),
};
BUILD_ASSERT(ARRAY_SIZE(gpio_list) == GPIO_COUNT);

/* Pins with alternate functions */
const struct gpio_alt_func gpio_alt_funcs[] = {
	{GPIO_A, 0x03, 1, MODULE_UART},		/* UART0 */
	{GPIO_G, 0x40, 3, MODULE_I2C},		/* I2C5 SCL */
	{GPIO_G, 0x80, 3, GPIO_OPEN_DRAIN},	/* I2C5 SDA */
	{GPIO_B, 0x03, 1, MODULE_UART},		/* UART1 */
};
const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);

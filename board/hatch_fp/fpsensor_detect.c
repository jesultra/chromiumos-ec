/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor_detect.h"
#include "gpio.h"
#include "timer.h"

enum fp_transport_type get_fp_transport_type(void)
{
	enum fp_transport_type ret;

	gpio_set_level(GPIO_DIVIDER_HIGHSIDE, 1);
	usleep(1);
	switch (gpio_get_level(GPIO_TRANSPORT_SEL)) {
	case 0:
		ret = FP_TRANSPORT_TYPE_UART;
		break;
	case 1:
		ret = FP_TRANSPORT_TYPE_SPI;
		break;
	default:
		ret = FP_TRANSPORT_TYPE_UNKNOWN;
		break;
	}

	/* We leave GPIO_DIVIDER_HIGHSIDE enabled, since the dragonclaw
	 * development board use it to enable the AND gate (U10) to CS.
	 * Production boards could disable this to save power since it's
	 * only needed for initial detection on those boards.
	 */
	return ret;
}

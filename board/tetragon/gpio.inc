/* -*- mode:c -*-
 *
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * List of all GPIO pins available for Host computer to manipulate.
 * The are named based on their location on the two 70-pin DIL
 * connectors on either side of the HyperDebug board.  Pins with
 * special functions are commented out, and declared with relevant
 * symbolic name further below.
 */

/*
 * The common define GPIO_INT_BOTH includes GPIO_INPUT, which interferes with
 * GPIO_ALTERNATE.  Instead we want to declare the intent to detect either
 * rising or falling edges, making it conditional on the pins possibly later
 * being re-configured as GPIO inputs.
 */
#define ANY_EDGE (GPIO_INT_F_RISING | GPIO_INT_F_FALLING)

/* Low level means the LED is on. */
GPIO(TX_LED,		PIN(2, 7),  GPIO_OUT_HIGH)
GPIO(RX_LED,		PIN(2, 8),  GPIO_OUT_HIGH)

GPIO(SAM3X_SWDIO,	PIN(0, 8),  GPIO_INPUT)
GPIO(SAM3X_SWCLK,	PIN(2, 11), GPIO_INPUT)
GPIO(SAM3X_RESET,	PIN(2, 10), GPIO_OUT_HIGH | GPIO_OPEN_DRAIN)
	
/* Unimplemented signals since we are not an EC */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

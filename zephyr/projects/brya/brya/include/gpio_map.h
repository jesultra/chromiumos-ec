/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_GPIO_MAP_H
#define __ZEPHYR_GPIO_MAP_H

#include <devicetree.h>
#include <gpio_signal.h>

#define GPIO_ENTERING_RW	GPIO_UNIMPLEMENTED
#define GPIO_WP_L		GPIO_UNIMPLEMENTED

#ifdef CONFIG_PLATFORM_EC_USBC
#define TCPC_ALERT_INT(gpio, edge) GPIO_INT(gpio, edge, tcpc_alert_event)
#define PPC_INT(gpio, edge) GPIO_INT(gpio, edge, ppc_interrupt)
#define BC12_INT(gpio, edge) GPIO_INT(gpio, edge, bc12_interrupt)
#define RETIMER_INT(gpio, edge) GPIO_INT(gpio, edge, retimer_interrupt)
#else
#define TCPC_ALERT_INT(gpio, edge)
#define PPC_INT(gpio, edge)
#define BC12_INT(gpio, edge)
#define RETIMER_INT(gpio, edge)
#endif
#define GPIO_EC_BATT_PRES_ODL GPIO_BATT_PRES_ODL

/*
 * Set EC_CROS_GPIO_INTERRUPTS to a space-separated list of GPIO_INT items.
 *
 * Each GPIO_INT requires three parameters:
 *   gpio_signal - The enum gpio_signal for the interrupt gpio
 *   interrupt_flags - The interrupt-related flags (e.g. GPIO_INT_EDGE_BOTH)
 *   handler - The platform/ec interrupt handler.
 *
 * Ensure that this files includes all necessary headers to declare all
 * referenced handler functions.
 *
 * For example, one could use the follow definition:
 * #define EC_CROS_GPIO_INTERRUPTS \
 *   GPIO_INT(NAMED_GPIO(h1_ec_pwr_btn_odl), GPIO_INT_EDGE_BOTH, button_print)
 */
#define EC_CROS_GPIO_INTERRUPTS                                           \
	GPIO_INT(GPIO_LID_OPEN, GPIO_INT_EDGE_BOTH, lid_interrupt)        \
	GPIO_INT(GPIO_POWER_BUTTON_L, GPIO_INT_EDGE_BOTH,                 \
		 power_button_interrupt)                                  \
	GPIO_INT(GPIO_WP_L, GPIO_INT_EDGE_BOTH, switch_interrupt)         \
	GPIO_INT(GPIO_AC_PRESENT, GPIO_INT_EDGE_BOTH, extpower_interrupt) \
	TCPC_ALERT_INT(GPIO_USB_C0_C2_TCPC_INT_ODL, GPIO_INT_EDGE_FALLING)\
	TCPC_ALERT_INT(GPIO_USB_C1_TCPC_INT_ODL, GPIO_INT_EDGE_FALLING)   \
	PPC_INT(GPIO_USB_C0_PPC_INT_ODL, GPIO_INT_EDGE_FALLING)           \
	PPC_INT(GPIO_USB_C1_PPC_INT_ODL, GPIO_INT_EDGE_FALLING)           \
	PPC_INT(GPIO_USB_C2_PPC_INT_ODL, GPIO_INT_EDGE_FALLING)           \
	BC12_INT(GPIO_USB_C0_BC12_INT_ODL, GPIO_INT_EDGE_FALLING)         \
	BC12_INT(GPIO_USB_C1_BC12_INT_ODL, GPIO_INT_EDGE_FALLING)         \
	BC12_INT(GPIO_USB_C2_BC12_INT_ODL, GPIO_INT_EDGE_FALLING)         \
	RETIMER_INT(GPIO_USB_C0_RT_INT_ODL, GPIO_INT_EDGE_FALLING)        \
	RETIMER_INT(GPIO_USB_C2_RT_INT_ODL, GPIO_INT_EDGE_FALLING)

#endif /* __ZEPHYR_GPIO_MAP_H */

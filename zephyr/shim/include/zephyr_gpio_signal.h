/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if !defined(__CROS_EC_GPIO_SIGNAL_H) || defined(__CROS_EC_ZEPHYR_GPIO_SIGNAL_H)
#error "This file must only be included from gpio_signal.h. Include gpio_signal.h directly."
#endif
#define __CROS_EC_ZEPHYR_GPIO_SIGNAL_H

#include <devicetree.h>
#include <toolchain.h>

#define GPIO_SIGNAL(id) DT_STRING_UPPER_TOKEN(id, enum_name)
#define GPIO_SIGNAL_WITH_COMMA(id) \
	COND_CODE_1(DT_NODE_HAS_PROP(id, enum_name), (GPIO_SIGNAL(id), ), ())
enum gpio_signal {
	GPIO_UNIMPLEMENTED = -1,
#if DT_NODE_EXISTS(DT_PATH(named_gpios))
	DT_FOREACH_CHILD(DT_PATH(named_gpios), GPIO_SIGNAL_WITH_COMMA)
#endif
	GPIO_COUNT,
	GPIO_LIMIT = 0x0FFF,
};
#undef GPIO_SIGNAL_WITH_COMMA
BUILD_ASSERT(GPIO_COUNT < GPIO_LIMIT);

/** @brief Converts a node identifier under named gpios to enum
 *
 * Converts the specified node identifier name, which should be nested under
 * the named_gpios node, into the correct enum gpio_signal that can be used
 * with platform/ec gpio API
 */
#define NAMED_GPIO(name) GPIO_SIGNAL(DT_PATH(named_gpios, name))

/** @brief Obtain a named gpio enum from a label and property
 *
 * Obtains a valid enum gpio_signal that can be used with platform/ec gpio API
 * from the property of a labeled node. The property has to point to a
 * named_gpios node.
 */
#define NAMED_GPIO_NODELABEL(label, prop) \
	GPIO_SIGNAL(DT_PHANDLE(DT_NODELABEL(label), prop))

enum ioex_port {
	IOEX_C0_NCT38XX = 0,
	IOEX_C2_NCT38XX,
	IOEX_ID_1_C0_NCT38XX,
	IOEX_ID_1_C2_NCT38XX,
	IOEX_PORT_COUNT
};
/*
 * While we don't support IO expanders at the moment, multiple
 * platform/ec headers (e.g., espi.h) require some of these constants
 * to be defined.  Define them as a compatibility measure.
 */
enum ioex_signal {
	IOEX_SIGNAL_START = GPIO_LIMIT + 1,
	__IOEX_PLACEHOLDER = GPIO_LIMIT,
	IOEX_ID_1_USB_C0_RT_RST_ODL,
	IOEX_ID_1_USB_C0_FRS_EN,
	IOEX_ID_1_USB_C0_OC_ODL,
	IOEX_ID_1_USB_C2_RT_RST_ODL,
	IOEX_ID_1_USB_C2_FRS_EN,
	IOEX_ID_1_USB_C1_OC_ODL,
	IOEX_ID_1_USB_C2_OC_ODL,
	IOEX_USB_C0_OC_ODL,
	IOEX_USB_C0_FRS_EN,
	IOEX_USB_C0_RT_RST_ODL,
	IOEX_USB_C2_RT_RST_ODL,
	IOEX_USB_C1_OC_ODL,
	IOEX_USB_C2_OC_ODL,
	IOEX_USB_C2_FRS_EN,
	IOEX_SIGNAL_END,
	IOEX_LIMIT = 0x1FFF,
};
BUILD_ASSERT(IOEX_SIGNAL_END < IOEX_LIMIT);

#define IOEX_COUNT (IOEX_SIGNAL_END - IOEX_SIGNAL_START)

#include "ioexpander.h"
/*
 *  Define the IO expander IO in gpio.inc by the format:
 *    IOEX(name, ioex_port, port, offset, flags)
 *      - name: the name of this IO pin
 *      - ioex: the IO expander port (defined in board.c) this IO
 *                 pin belongs to.
 *      - port: the port number in the IO expander chip.
 *      - offset: the bit offset in the port above.
 *      - flags: the same as the flags of GPIO.
 *
 */
#define IOEX(name, ioex, port, index, flags) \
			{#name, ioex, port, BIT(index), flags},

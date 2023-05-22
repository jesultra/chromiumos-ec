/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "driver/retimer/bb_retimer_public.h"
#include "extpower.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "ioexpander.h"
#include "power/icelake.h"
#include "system.h"
#include "task.h"
#include "usb_mux.h"
#include "usbc/usb_muxes.h"
#include "usbc_ppc.h"
#include "util.h"

static int board_pre_task_peripheral_init(void)
{
	USB_MUX_ENABLE_ALTERNATIVE(usb_mux_alt_chain_1);

	return 0;
}
SYS_INIT(board_pre_task_peripheral_init, APPLICATION,
	 CONFIG_APPLICATION_INIT_PRIORITY);

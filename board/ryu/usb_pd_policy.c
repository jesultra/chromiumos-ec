/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "case_closed_debug.h"
#include "charge_manager.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_mux.h"
#include "usb_pd.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	struct charge_port_info charge;
	charge.current = max_ma;
	charge.voltage = supply_voltage;
	charge_manager_update_charge(CHARGE_SUPPLIER_PD, port, &charge);
}

void typec_set_input_current_limit(int port, uint32_t max_ma,
				   uint32_t supply_voltage)
{
	struct charge_port_info charge;
	charge.current = max_ma;
	charge.voltage = supply_voltage;
	charge_manager_update_charge(CHARGE_SUPPLIER_TYPEC, port, &charge);
}

	/* Any voltage less than the max is allowed */
int pd_set_power_supply_ready(int port)
{
	/* provide VBUS */
	gpio_set_level(GPIO_CHGR_OTG, 1);
	charger_enable_otg_power(1);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Kill VBUS */
	charger_enable_otg_power(0);
	gpio_set_level(GPIO_CHGR_OTG, 0);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_snk_is_vbus_provided(int port)
{
	return gpio_get_level(GPIO_CHGR_ACOK);
}

	/* TODO: use battery level to decide to accept/reject power swap */
int pd_check_vconn_swap(int port)
{
	/*
	 * VCONN is provided directly by the battery(PPVAR_SYS)
	 * but use the same rules as power swap
	 */
	return pd_get_dual_role() == PD_DRP_TOGGLE_ON ? 1 : 0;
}

__override void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	board_set_vbus_source_current_limit(port, rp);
}

static void hpd_irq_deferred(void)
{
	gpio_set_level(GPIO_USBC_DP_HPD, 1);
}
DECLARE_DEFERRED(hpd_irq_deferred);

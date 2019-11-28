/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define PDO_FIXED_FLAGS (PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP)

/* Used to fake VBUS presence since no GPIO is available to read VBUS */
static int vbus_present;

const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000, 1500, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

int pd_set_power_supply_ready(int port)
{
	/* Turn on the "up" LED when we output VBUS */
	gpio_set_level(GPIO_LED_U, 1);
	CPRINTS("Power supply ready/%d", port);
	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Turn off the "up" LED when we shutdown VBUS */
	gpio_set_level(GPIO_LED_U, 0);
	/* Disable VBUS */
	CPRINTS("Disable VBUS", port);
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	CPRINTS("USBPD current limit port %d max %d mA %d mV",
		port, max_ma, supply_voltage);
	/* do some LED coding of the power we can sink */
	if (max_ma) {
		if (supply_voltage > 6500)
			gpio_set_level(GPIO_LED_R, 1);
		else
			gpio_set_level(GPIO_LED_L, 1);
	} else {
		gpio_set_level(GPIO_LED_L, 0);
		gpio_set_level(GPIO_LED_R, 0);
	}
}

__override void typec_set_input_current_limit(int port, uint32_t max_ma,
					      uint32_t supply_voltage)
{
	CPRINTS("TYPEC current limit port %d max %d mA %d mV",
		port, max_ma, supply_voltage);
	gpio_set_level(GPIO_LED_R, !!max_ma);
}

void button_event(enum gpio_signal signal)
{
	vbus_present = !vbus_present;
	CPRINTS("VBUS %d", vbus_present);
}

static int command_vbus_toggle(int argc, char **argv)
{
	vbus_present = !vbus_present;
	CPRINTS("VBUS %d", vbus_present);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(vbus, command_vbus_toggle,
			"",
			"Toggle VBUS detected");

int pd_snk_is_vbus_provided(int port)
{
	return vbus_present;
}

__override int pd_check_data_swap(int port, int data_role)
{
	/* Always allow data swap */
	return 1;
}

__override void pd_check_pr_role(int port, int pr_role, int flags)
{
}

__override void pd_check_dr_role(int port, int dr_role, int flags)
{
}
/* ----------------- Vendor Defined Messages ------------------ */
__override const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
__override void svdm_safe_dp_mode(int port)
{
	/* make DP interface safe until configure */
	dp_flags[port] = 0;
	/* board_set_usb_mux(port, TYPEC_MUX_NONE, pd_get_polarity(port)); */
}

__override int svdm_dp_config(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, USB_SID_DISPLAYPORT);
	/* board_set_usb_mux(port, TYPEC_MUX_DP, pd_get_polarity(port)); */
	payload[0] = VDO(USB_SID_DISPLAYPORT, 1,
			 CMD_DP_CONFIG | VDO_OPOS(opos));
	payload[1] = VDO_DP_CFG(MODE_DP_PIN_E, /* pin mode */
				1,             /* DPv1.3 signaling */
				2);            /* UFP connected */
	return 2;
};

__override void svdm_dp_post_config(int port)
{
	dp_flags[port] |= DP_FLAGS_DP_ON;
	if (!(dp_flags[port] & DP_FLAGS_HPD_HI_PENDING))
		return;
}

__override int svdm_dp_attention(int port, uint32_t *payload)
{
	/* ack */
	return 1;
}

__override void svdm_exit_dp_mode(int port)
{
	svdm_safe_dp_mode(port);
	/* gpio_set_level(PORT_TO_HPD(port), 0); */
}
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */

/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "config.h"
#include "case_closed_debug.h"
#include "charge_manager.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_mux.h"
#include "usb_pd_config.h"
#include "power.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define PDO_FIXED_FLAGS (PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP |\
			 PDO_FIXED_COMM_CAP)

const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000, 1500, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
		PDO_BATT(4500, 14000, 10000),
		PDO_VAR(4500, 14000, 3000),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
#ifdef CONFIG_CHARGE_MANAGER
	struct charge_port_info charge;

	charge.current = max_ma;
	charge.voltage = supply_voltage;
	charge_manager_update_charge(CHARGE_SUPPLIER_PD, port, &charge);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
#endif
}

void typec_set_input_current_limit(int port, uint32_t max_ma,
				   uint32_t supply_voltage)
{
#ifdef CONFIG_CHARGE_MANAGER
	struct charge_port_info charge;

	charge.current = max_ma;
	charge.voltage = supply_voltage;
	charge_manager_update_charge(CHARGE_SUPPLIER_TYPEC, port, &charge);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
#endif
}

int pd_is_valid_input_voltage(int mv)
{
	/* Any voltage less than the max is allowed */
	return 1;
}

void pd_transition_voltage(int idx)
{
	/* No-operation: we are always 5V */
}

int pd_set_power_supply_ready(int port)
{
	/* provide VBUS */
#if 0
	usbpd_set_power_role(port, PD_ROLE_SOURCE);
#endif
	power_enable_vbus(port, TRUE);
	USBPD_ENABLE_STAT_VBUS_5V(port);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Kill VBUS */
#if 0
	usbpd_set_power_role(port, PD_ROLE_SINK);
#endif
	power_enable_vbus(port, FALSE);
	USBPD_DISABLE_STAT_VBUS_5V(port);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_snk_is_vbus_provided(int port)
{
	/*
	if(power_get_m_volt(port))	return 1;
	else	return 0;
	*/
	return 1;
}

int pd_board_checks(void)
{
	return EC_SUCCESS;
}

int pd_check_power_swap(int port)
{
	/* TODO: use battery level to decide to accept/reject power swap
	 * Allow power swap as long as we are acting as a dual role device,
	 * otherwise assume our role is fixed (not in S0 or console command
	 * to fix our role).
	 */
	return pd_get_dual_role() == PD_DRP_TOGGLE_ON ? 1 : 0;
}

int pd_check_data_swap(int port, int data_role)
{
	/* Always allow data swap: we can be DFP or UFP for USB */
	return 1;
}

int pd_check_vconn_swap(int port)
{
	/*
	 * VCONN is provided directly by the battery(PPVAR_SYS)
	 * but use the same rules as power swap
	 */
	return pd_get_dual_role() == PD_DRP_TOGGLE_ON ? 1 : 0;
}

void pd_execute_data_swap(int port, int data_role)
{
	/* inform the host controller to change role */
	pd_send_host_event(PD_EVENT_DATA_SWAP);
}

void pd_check_pr_role(int port, int pr_role, int flags)
{
	/*
	 * If partner is dual-role power and dualrole toggling is on, consider
	 * if a power swap is necessary.
	 */
	if ((flags & PD_FLAGS_PARTNER_DR_POWER) &&
	    pd_get_dual_role() == PD_DRP_TOGGLE_ON) {
		/*
		 * If we are source and partner is externally powered,
		 * swap to become a sink.
		 */
		if ((flags & PD_FLAGS_PARTNER_EXTPOWER) &&
		    pr_role == PD_ROLE_SOURCE)
			pd_request_power_swap(port);
	}
}

void pd_check_dr_role(int port, int dr_role, int flags)
{
	/* if the partner is a DRP (e.g. laptop), try to switch to UFP */
	if ((flags & PD_FLAGS_PARTNER_DR_DATA) && dr_role == PD_ROLE_DFP)
		pd_request_data_swap(port);
}

/* ----------------- Vendor Defined Messages ------------------ */
const uint32_t vdo_idh = VDO_IDH(0, /* data caps as USB host */
				 0, /* data caps as USB device */
				 IDH_PTYPE_UNDEF, /* Undefined */
				 1, /* supports alt modes */
				 USB_VID_GOOGLE);

const uint32_t vdo_product = VDO_PRODUCT(CONFIG_USB_PID, CONFIG_USB_BCD_DEV);

/* When set true, we are in GFU mode */
static int gfu_mode;

static int svdm_response_identity(int port, uint32_t *payload)
{
	payload[VDO_I(IDH)] = vdo_idh;
	payload[VDO_I(CSTAT)] = VDO_CSTAT(0);
	payload[VDO_I(PRODUCT)] = vdo_product;
	return VDO_I(PRODUCT) + 1;
}

static int svdm_response_svids(int port, uint32_t *payload)
{
	payload[1] = VDO_SVID(USB_SID_DISPLAYPORT, 0);
	return 2;
}

/* Will only ever be a single mode for this device */
#define MODE_CNT 1
#define OPOS 1

const uint32_t vdo_dp_mode[MODE_CNT] =  {
	VDO_MODE_GOOGLE(MODE_GOOGLE_FU)
};

static int svdm_response_modes(int port, uint32_t *payload)
{
	if (PD_VDO_VID(payload[0]) != USB_SID_DISPLAYPORT)
		return 0; /* nak */

	memcpy(payload + 1, vdo_dp_mode, sizeof(vdo_dp_mode));
	return MODE_CNT + 1;
}

static int svdm_enter_mode(int port, uint32_t *payload)
{
	/* SID & mode request is valid */
	if ((PD_VDO_VID(payload[0]) != USB_VID_GOOGLE) ||
	    (PD_VDO_OPOS(payload[0]) != OPOS))
		return 0; /* will generate NAK */

	gfu_mode = 1;
	return 1;
}

static int svdm_exit_mode(int port, uint32_t *payload)
{
	gfu_mode = 0;
	return 1; /* Must return ACK */
}

static struct amode_fx dp_fx = {
	.status = NULL,
	.config = NULL,
};

const struct svdm_response svdm_rsp = {
	.identity = &svdm_response_identity,
	.svids = &svdm_response_svids,
	.modes = &svdm_response_modes,
	.enter_mode = &svdm_enter_mode,
	.amode = &dp_fx,
	.exit_mode = &svdm_exit_mode,
};

const int supported_modes_cnt = 0; /*ARRAY_SIZE(supported_modes); */

int pd_custom_vdm(int port, int cnt, uint32_t *payload,
		  uint32_t **rpayload)
{
	int cmd = PD_VDO_CMD(payload[0]);
	int rsize;

	if (PD_VDO_VID(payload[0]) != USB_VID_GOOGLE || !gfu_mode)
		return 0;

	*rpayload = payload;

	rsize = pd_custom_flash_vdm(port, cnt, payload);
	if (!rsize) {
		switch (cmd) {
		case VDO_CMD_PING_ENABLE:
			pd_ping_enable(0, payload[1]);
			rsize = 1;
			break;
		case VDO_CMD_CURRENT:
			/* return last measured current */
			payload[1] = 1;
			rsize = 2;
			break;
		case VDO_CMD_GET_LOG:
			rsize = 1;
			break;
		default:
			/* Unknown : do not answer */
			return 0;
		}
	}

	/* respond (positively) to the request */
	payload[0] |= VDO_SRC_RESPONDER;

	return rsize;
}


static int dp_flags;
/* DP Status VDM as returned by UFP */
static uint32_t dp_status;

static void svdm_safe_dp_mode(int port)
{
	/* make DP interface safe until configure */
#ifdef CONFIG_USBC_SS_MUX
	usb_mux_set(port, TYPEC_MUX_NONE, USB_SWITCH_CONNECT, 0);
	dp_flags = 0;
	dp_status = 0;
#endif
}

static int svdm_enter_dp_mode(int port, uint32_t mode_caps)
{
	/* Only enter mode if device is DFP_D capable */
	if (mode_caps & MODE_DP_SNK) {
		svdm_safe_dp_mode(port);
		return 0;
	}

	return -1;
}

static int svdm_dp_status(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, USB_SID_DISPLAYPORT);

	payload[0] = VDO(USB_SID_DISPLAYPORT, 1,
			 CMD_DP_STATUS | VDO_OPOS(opos));
	payload[1] = VDO_DP_STATUS(0, /* HPD IRQ  ... not applicable */
				   0, /* HPD level ... not applicable */
				   0, /* exit DP? ... no */
				   0, /* usb mode? ... no */
				   0, /* multi-function ... no */
				   (!!(dp_flags & DP_FLAGS_DP_ON)),
				   0, /* power low? ... no */
				   (!!(dp_flags & DP_FLAGS_DP_ON)));
	return 2;
};

static int svdm_dp_config(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, USB_SID_DISPLAYPORT);
#ifdef CONFIG_USBC_SS_MUX
	int mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status);
#endif
	int pin_mode = pd_dfp_dp_get_pin_mode(port, dp_status);

	if (!pin_mode)
		return 0;
#ifdef CONFIG_USBC_SS_MUX
	usb_mux_set(port, mf_pref ? TYPEC_MUX_DOCK : TYPEC_MUX_DP,
			  USB_SWITCH_CONNECT, pd_get_polarity(port));
#endif

	payload[0] = VDO(USB_SID_DISPLAYPORT, 1,
			 CMD_DP_CONFIG | VDO_OPOS(opos));
	payload[1] = VDO_DP_CFG(pin_mode,      /* pin mode */
				1,             /* DPv1.3 signaling */
				2);            /* UFP_U connected as UFP_D */
	return 2;
};

static void svdm_dp_post_config(int port)
{
	dp_flags |= DP_FLAGS_DP_ON;
	if (!(dp_flags & DP_FLAGS_HPD_HI_PENDING))
		return;
#if 0
	gpio_set_level(GPIO_USBC_DP_HPD, 1);
#endif
}

static void hpd_irq_deferred(void)
{
#if 0
	gpio_set_level(GPIO_USBC_DP_HPD, 1);
#endif
}
DECLARE_DEFERRED(hpd_irq_deferred);

static int svdm_dp_attention(int port, uint32_t *payload)
{
	return 1;
}

static void svdm_exit_dp_mode(int port)
{
	svdm_safe_dp_mode(port);
#if 0
	gpio_set_level(GPIO_USBC_DP_HPD, 0);
#endif
}

static int svdm_enter_gfu_mode(int port, uint32_t mode_caps)
{
	/* Always enter GFU mode */
	return 0;
}

static void svdm_exit_gfu_mode(int port)
{
}

static int svdm_gfu_status(int port, uint32_t *payload)
{
	/*
	 * This is called after enter mode is successful, send unstructured
	 * VDM to read info.
	 */
	pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_READ_INFO, NULL, 0);
	return 0;
}

static int svdm_gfu_config(int port, uint32_t *payload)
{
	return 0;
}

static int svdm_gfu_attention(int port, uint32_t *payload)
{
	return 0;
}

const struct svdm_amode_fx supported_modes[] = {
	{
		.svid = USB_SID_DISPLAYPORT,
		.enter = &svdm_enter_dp_mode,
		.status = &svdm_dp_status,
		.config = &svdm_dp_config,
		.post_config = &svdm_dp_post_config,
		.attention = &svdm_dp_attention,
		.exit = &svdm_exit_dp_mode,
	},
	{
		.svid = USB_VID_GOOGLE,
		.enter = &svdm_enter_gfu_mode,
		.status = &svdm_gfu_status,
		.config = &svdm_gfu_config,
		.attention = &svdm_gfu_attention,
		.exit = &svdm_exit_gfu_mode,
	}
};


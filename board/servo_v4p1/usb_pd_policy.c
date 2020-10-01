/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "chg_control.h"
#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "ioexpanders.h"
#include "pathsel.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "tcpm.h"
#include "timer.h"
#include "tusb1064.h"
#include "util.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "usb_pd_tcpm.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define DUT_PDO_FIXED_FLAGS (PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP |\
			     PDO_FIXED_COMM_CAP)

#define CHG_PDO_FIXED_FLAGS (PDO_FIXED_DATA_SWAP)

#define VBUS_UNCHANGED(curr, pend, new) (curr == new && pend == new)

/* Macros to config the PD role */
#define CONF_SET_CLEAR(c, set, clear) ((c | (set)) & ~(clear))
#define CONF_SRC(c) CONF_SET_CLEAR(c, \
				CC_DISABLE_DTS | CC_ALLOW_SRC, \
				CC_ENABLE_DRP | CC_SNK_WITH_PD | CC_SRC_WITH_PD)
#define CONF_SNK(c) CONF_SET_CLEAR(c, \
				CC_DISABLE_DTS, \
				CC_ALLOW_SRC | CC_ENABLE_DRP | CC_SNK_WITH_PD | CC_SRC_WITH_PD)
#define CONF_PDSNK(c) CONF_SET_CLEAR(c, \
				CC_DISABLE_DTS | CC_SNK_WITH_PD | CC_SRC_WITH_PD, \
				CC_ALLOW_SRC | CC_ENABLE_DRP)
#define CONF_PDSRC(c) CONF_SET_CLEAR(c, \
				CC_DISABLE_DTS | CC_SNK_WITH_PD | CC_SRC_WITH_PD | CC_ALLOW_SRC, \
				CC_ENABLE_DRP)
#define CONF_DRP(c) CONF_SET_CLEAR(c, \
				CC_DISABLE_DTS | CC_ALLOW_SRC | CC_ENABLE_DRP, \
				CC_SNK_WITH_PD | CC_SRC_WITH_PD )
#define CONF_PDDRP(c) CONF_SET_CLEAR(c, \
				CC_DISABLE_DTS | CC_ALLOW_SRC | CC_ENABLE_DRP | \
				CC_SNK_WITH_PD | CC_SRC_WITH_PD, \
				0)
#define CONF_SRCDTS(c) CONF_SET_CLEAR(c, \
				CC_ALLOW_SRC | CC_SRC_WITH_PD, \
				CC_ENABLE_DRP | CC_DISABLE_DTS | CC_SNK_WITH_PD )
#define CONF_SNKDTS(c) CONF_SET_CLEAR(c, \
				0, \
				CC_ALLOW_SRC | CC_ENABLE_DRP | CC_SRC_WITH_PD | \
				CC_DISABLE_DTS | CC_SNK_WITH_PD )
#define CONF_PDSNKDTS(c) CONF_SET_CLEAR(c, \
				CC_SNK_WITH_PD | CC_SRC_WITH_PD, \
				CC_ALLOW_SRC | CC_ENABLE_DRP | CC_DISABLE_DTS)
#define CONF_PDSRCDTS(c) CONF_SET_CLEAR(c, \
				CC_SNK_WITH_PD | CC_SRC_WITH_PD | CC_ALLOW_SRC, \
				CC_ENABLE_DRP | CC_DISABLE_DTS)
#define CONF_DRPDTS(c) CONF_SET_CLEAR(c, \
				CC_ALLOW_SRC | CC_ENABLE_DRP | CC_SRC_WITH_PD, \
				CC_DISABLE_DTS | CC_SNK_WITH_PD )

/* Macros to apply Rd/Rp to CC lines */
#define DUT_ACTIVE_CC_SET(r, flags) \
	gpio_set_flags(cc_config & CC_POLARITY ? \
				CONCAT2(GPIO_USB_DUT_CC2_, r) : \
				CONCAT2(GPIO_USB_DUT_CC1_, r), \
		       flags)
#define DUT_INACTIVE_CC_SET(r, flags) \
	gpio_set_flags(cc_config & CC_POLARITY ? \
				CONCAT2(GPIO_USB_DUT_CC1_, r) : \
				CONCAT2(GPIO_USB_DUT_CC2_, r), \
		       flags)
#define DUT_BOTH_CC_SET(r, flags) \
	do { \
		gpio_set_flags(CONCAT2(GPIO_USB_DUT_CC1_, r), flags); \
		gpio_set_flags(CONCAT2(GPIO_USB_DUT_CC2_, r), flags); \
	} while (0)

#define DUT_ACTIVE_CC_PU(r) DUT_ACTIVE_CC_SET(r, GPIO_OUT_HIGH)
#define DUT_INACTIVE_CC_PU(r) DUT_INACTIVE_CC_SET(r, GPIO_OUT_HIGH)
#define DUT_ACTIVE_CC_PD(r) DUT_ACTIVE_CC_SET(r, GPIO_OUT_LOW)
#define DUT_INACTIVE_CC_PD(r) DUT_INACTIVE_CC_SET(r, GPIO_OUT_LOW)
#define DUT_BOTH_CC_PD(r) DUT_BOTH_CC_SET(r, GPIO_OUT_LOW)
#define DUT_BOTH_CC_OPEN(r) DUT_BOTH_CC_SET(r, GPIO_INPUT)
#define DUT_ACTIVE_CC_OPEN(r) DUT_ACTIVE_CC_SET(r, GPIO_INPUT)
#define DUT_INACTIVE_CC_OPEN(r) DUT_INACTIVE_CC_SET(r, GPIO_INPUT)

/*
 * Dynamic PDO that reflects capabilities present on the CHG port. Allow for
 * multiple entries so that we can offer greater than 5V charging. The 1st
 * entry will be fixed 5V, but its current value may change based on the CHG
 * port vbus info. Subsequent entries are used for when offering vbus greater
 * than 5V.
 */
static const uint16_t pd_src_voltages_mv[] = {
		5000, 9000, 10000, 12000, 15000, 20000,
};
static uint32_t pd_src_chg_pdo[ARRAY_SIZE(pd_src_voltages_mv)];
static uint8_t chg_pdo_cnt;

const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000, 500, CHG_PDO_FIXED_FLAGS),
		PDO_BATT(4750, 21000, 15000),
		PDO_VAR(4750, 21000, 3000),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

/*
enum gpio_pulls {
	RA=0,
	RD=1,
	RPUSB=2,
	RP1A5=3,
	RP3A0=4,
	TX_DATA=5,
	PULL_COUNT=6,
};

struct gpio_drive {
	uint8_t output : 1;
	uint8_t drive  : 1;
};
*/

//static const uint8_t dut_gpio[PULL_COUNT /* Pulls */][2 /* Polarity */] = {
//	{ GPIO_USB_DUT_CC1_RA, GPIO_USB_DUT_CC2_RA },
//	{ GPIO_USB_DUT_CC1_RD, GPIO_USB_DUT_CC2_RD },
//	{ GPIO_USB_DUT_CC1_RPUSB, GPIO_USB_DUT_CC2_RPUSB },
//	{ GPIO_USB_DUT_CC1_RP1A5, GPIO_USB_DUT_CC2_RP1A5 },
//	{ GPIO_USB_DUT_CC1_RP3A0, GPIO_USB_DUT_CC2_RP3A0 },
//	{ GPIO_USB_DUT_CC1_TX_DATA, GPIO_USB_DUT_CC2_TX_DATA },
//};

//static struct gpio_drive dut_pulls[PULL_COUNT /* Pulls */][2 /* Polarity */];

struct vbus_prop {
	int mv;
	int ma;
};

static struct vbus_prop vbus[CONFIG_USB_PD_PORT_MAX_COUNT];
static int active_charge_port = CHARGE_PORT_NONE;
static enum charge_supplier active_charge_supplier;

/*
 * Make sure the below matches SERVOV4 initial
 * state otherwise you'll have a bad time (desync).
 */
static int cc_pull_stored = TYPEC_CC_RD;
/* Shadow what would be in TCPC register state. */
static int rp_value_stored = CONFIG_USB_PD_PULLUP;
/* Strongly suggest avoiding TYPEC_RP_RESERVED */
static int cc_config = SERVO_DEFAULT_CONFIG;

/* Voltage thresholds for no connect in DTS mode */
static int pd_src_vnc_dts[TYPEC_RP_RESERVED][2] = {
	{PD_SRC_3_0_VNC_MV, PD_SRC_1_5_VNC_MV},
	{PD_SRC_1_5_VNC_MV, PD_SRC_DEF_VNC_MV},
	{PD_SRC_3_0_VNC_MV, PD_SRC_DEF_VNC_MV},
};
/* Voltage thresholds for Ra attach in DTS mode */
static int pd_src_rd_threshold_dts[TYPEC_RP_RESERVED][2] = {
	{PD_SRC_3_0_RD_THRESH_MV, PD_SRC_1_5_RD_THRESH_MV},
	{PD_SRC_1_5_RD_THRESH_MV, PD_SRC_DEF_RD_THRESH_MV},
	{PD_SRC_3_0_RD_THRESH_MV, PD_SRC_DEF_RD_THRESH_MV},
};
/* Voltage thresholds for no connect in normal SRC mode */
static int pd_src_vnc[TYPEC_RP_RESERVED] = {
	PD_SRC_DEF_VNC_MV,
	PD_SRC_1_5_VNC_MV,
	PD_SRC_3_0_VNC_MV,
};
/* Voltage thresholds for Ra attach in normal SRC mode */
static int pd_src_rd_threshold[TYPEC_RP_RESERVED] = {
	PD_SRC_DEF_RD_THRESH_MV,
	PD_SRC_1_5_RD_THRESH_MV,
	PD_SRC_3_0_RD_THRESH_MV,
};

/* Saved value for the duration of faking PD disconnect */
static int fake_pd_disconnect_duration_us;


static int user_limited_max_mv = 20000;

static uint32_t max_supported_voltage(void)
{
	return user_limited_max_mv;
}

static int charge_port_is_active(void)
{
	return (active_charge_port == CHG) && (vbus[CHG].mv > 0);
}

static int is_charge_through_allowed(void)
{
	return charge_port_is_active() && (cc_config & CC_ALLOW_SRC);
}

static int is_charge_through_enabled(void)
{

	if(	pd_get_dual_role(DUT) == PD_DRP_FORCE_SOURCE ||
		pd_get_dual_role(DUT) == PD_DRP_TOGGLE_ON ||
		(pd_get_dual_role(DUT) == PD_DRP_FREEZE &&
			pd_get_power_role(DUT) == PD_ROLE_SOURCE)){
		return 1;
	}
	else
		// ON OFF FREEZE SINK SRC
		return 0;
}

static int is_pd_allowed(void)
{

	//if in doubt, pass pd_get_data_role(port).

	//TODO: HACKHACKHACK fix this
	int allow_pd=0;
	enum pd_dual_role_states current_drp;

	//This is dangerous (and not accurate)
	//Should use "either possible end state" in response
	enum pd_power_role current_power;

	current_drp = pd_get_dual_role(DUT);
	current_power = pd_get_power_role(DUT);
	// NOTE pd_get_data_role() is ABSENT! <Don'tCare>!

	switch(current_drp){
	case PD_DRP_FORCE_SINK:
		// If DTS and PD-as-SNK, enable PD
		if (!(cc_config & CC_DISABLE_DTS)
				&& (cc_config & CC_SNK_WITH_PD)
				)
			allow_pd = 1;
		
		// If DTS is disabled [[and PD-as-SNK]] enable PD
		if ((cc_config & CC_DISABLE_DTS)
				&& (cc_config & CC_SNK_WITH_PD)
				)
			allow_pd = 1;
	break;
	case PD_DRP_FORCE_SOURCE:
		if(cc_config && CC_SRC_WITH_PD)
			allow_pd = 1;
	break;
	case PD_DRP_TOGGLE_ON:
	case PD_DRP_TOGGLE_OFF:
	case PD_DRP_FREEZE:
		if ((current_power == PD_ROLE_SINK && cc_config & CC_SNK_WITH_PD) ||
			(current_power == PD_ROLE_SOURCE && cc_config & CC_SRC_WITH_PD)	)
		//if( cc_config & CC_SNK_WITH_PD &&
		//		cc_config & CC_SRC_WITH_PD)
			allow_pd = 1;
	break;
	}

	CPRINTS("PD allowed [%d] Power [%d] DRP [%d] CC_CONFIG [0x%x]", allow_pd, current_power, current_drp, cc_config);
	return allow_pd;
}


static int get_dual_role_of_src(void)
{

	enum pd_dual_role_states state;
	
	// This is to fix broken SNK->SRC PR_SWAP as SRCDTS
	// old code was intended for ServoV4, not V4p1.
	// ServoV4p1 has no "always on" VBUS source.
	// FORCE_SOURCE is inappropriate.
	

	/* While disconnected, toggle between src and sink */
	//PD_DRP_TOGGLE_ON,

	/* Stay in src until disconnect, then stay in sink forever */
	//PD_DRP_TOGGLE_OFF,

	/* Stay in current power role, don't switch. No auto-toggle support */
	//PD_DRP_FREEZE,

	/* Switch to sink */
	//PD_DRP_FORCE_SINK,

	/* Switch to source */
	//PD_DRP_FORCE_SOURCE,

	if (cc_config & CC_ENABLE_DRP)
	 	 	state = PD_DRP_TOGGLE_ON;
	else if (cc_config & CC_ALLOW_SRC)
	 	 	state = PD_DRP_FREEZE;
	 	 	//state = PD_DRP_TOGGLE_OFF;
	/*
	else if (charge_port_is_active())
	 	 	state = PD_DRP_FORCE_SOURCE; 	// This could be argued.
	 	 	state = PD_DRP_FREEZE;
	*/
	else 
			// NOOP -- should be force sink here
			// Treat this as "No VBUS SuzyQ"
	 	 	state = PD_DRP_FORCE_SOURCE;
			//state = PD_DRP_FORCE_SINK;
	
	return state;
}

static void dut_activate_charge(void)
{
	int allow_pd=0;
	/*
	 * Update to charge enable if charger still present and not
	 * already charging.
	 */

	// TODO: fix is_charge_through_enabled() detection/logic
	if (is_charge_through_allowed() &&
			!is_charge_through_enabled()) {

		CPRINTS("Enable DUT charge through: desired Role %d", get_dual_role_of_src());


		// This initial call MUST be DRP_FORCE_SOURCE to trigger clean state transition.
		//pd_set_dual_role(DUT, get_dual_role_of_src());
		pd_set_dual_role(DUT, PD_DRP_FORCE_SOURCE);
		//pd_set_dual_role(DUT, get_dual_role_of_src());


		// task_set_event(PD_PORT_TO_TASK_ID(DUT),
		//        PD_EVENT_POWER_STATE_CHANGE |
		// 	       PD_EVENT_UPDATE_DUAL_ROLE,
		//        0);

		//hook_call_deferred(&fix_role_state_data, 200 * MSEC);
		//usleep(2000);

		//pd_set_dual_role(DUT, get_dual_role_of_src());
		//pd_update_contract(DUT);

		/*
		 * If DRP role, don't set any CC pull resistor, the PD
		 * state machine will toggle and set the pull resistors
		 * when needed.
		 */

		//============ FORCE A DISCONNECT ============
			pd_power_supply_reset(DUT);

			/* Remove Rp/Rd on both CC lines */
			/* ROLE_CONTROL  Open (Disconnect or don’t care) */
			pd_comm_enable(DUT, 0);			
			pd_set_rp_rd(DUT, TYPEC_CC_OPEN, rp_value_stored);

			/*
			 * If just changing mode (cc keeps enabled), give some
			 * time for DUT to detach, use tErrorRecovery.
			 */
			usleep(PD_T_ERROR_RECOVERY);
		//============================================

		if (!(cc_config & CC_ENABLE_DRP))
			pd_set_host_mode(DUT, 1);


//		do_cc(cc_config);
		//We need to use do_cc to get a disconnect here
		// (Ideally need a cleaner transition)
		// See BELOW comment.

		/*
		 * Enable PD comm. The PD comm may be disabled during
		 * the power charge-through was detached.
		 */

		// TODO: We need a more graceful transition here.
		// PR_SWAP if PD-capable, FORCE_DETACH if not PD-capable
		// allow_pd

		/*
		if (cc_config & CC_SRC_WITH_PD)
			pd_comm_enable(DUT, 1);
		else
			pd_comm_enable(DUT, 0);
		*/

		allow_pd=is_pd_allowed();
		CPRINTS("dut_activate_charge: ALLOW PD IS %d",allow_pd);
		pd_comm_enable(DUT,allow_pd);

		pd_update_contract(DUT);
	}
}
DECLARE_DEFERRED(dut_activate_charge);

static void board_manage_dut_port(void)
{
	enum pd_dual_role_states preferred_drp;
	enum pd_dual_role_states current_drp;
	int allow_pd=0;

	/*
	 * This function is called by the CHG port whenever there has been a
	 * change in its vbus voltage or current. That change may necessitate
	 * that the DUT port present a different Rp value or renogiate its PD
	 * contract if it is connected.
	 */

	/* Assume the default value of Rd */
	preferred_drp = PD_DRP_FORCE_SINK;
	// TODO: This should be handled in get_dual_role_of_src()

	/* If VBUS charge through is available, mark as such. */
	if (is_charge_through_allowed())
		preferred_drp = get_dual_role_of_src();

	current_drp = pd_get_dual_role(DUT);

	if (current_drp != preferred_drp) {
		/* Update role. */
		if (preferred_drp == PD_DRP_FORCE_SINK) {
			/* We've lost charge through. Disable VBUS. */
			chg_power_select(CHG_POWER_OFF);
			dut_chg_en(0);

			/* Mark as SNK only. */
			pd_set_dual_role(DUT, PD_DRP_FORCE_SINK);
			pd_set_host_mode(DUT, 0);

			/*
			 * Disable PD comm. It matches the user expectation that
			 * unplugging the power charge-through makes servo v4 as
			 * a passive hub, without any PD support.
			 *
			 * There is an exception that servo v4 is explicitly set
			 * to have PD, like the "pnsnk" mode.
			 */
			// The above is incorrect. Depends on CC_DISABLE_DTS.
			// Does not match my user expectation.
			
			// Should check for PR_SWAP, if we swapped from PDSRCDTS
			// Treat this as an abrupt shutoff.

			/*
			if (!(cc_config & CC_DISABLE_DTS)
					&& (cc_config & CC_SNK_WITH_PD)
					)
				allow_pd = 1;
		
			// If DTS is disabled [[and PD-as-SNK]] enable PD
			if ((cc_config & CC_DISABLE_DTS)
					//&& (cc_config & CC_SNK_WITH_PD)
					)
				allow_pd = 1;
			*/

			allow_pd=is_pd_allowed();
			//chg_power_select(CHG_POWER_PP5000);
			CPRINTS("board_manage_dut_port: ALLOW PD IS %d",allow_pd);
			pd_comm_enable(DUT, allow_pd);

			//pd_comm_enable(DUT, (cc_config & CC_SNK_WITH_PD) ? 1 : 0);
		} else {
			/* Allow charge through after PD negotiate. */
			/* Cancel any pending function calls */
			hook_call_deferred(&dut_activate_charge_data, -1);
			hook_call_deferred(&dut_activate_charge_data, 2000 * MSEC);
		}
	}

	/*
	 * Update PD contract to reflect new available CHG
	 * voltage/current values.
	 */
	pd_update_contract(DUT);
}

static void update_ports(void)
{
	int pdo_index, src_index, snk_index, i;
	uint32_t pdo, max_ma, max_mv;

	/*
	 * CHG Vbus has changed states, update PDO that reflects CHG port
	 * state
	 */
	if (!charge_port_is_active()) {
		/* CHG Vbus has dropped, so become SNK. */
		chg_pdo_cnt = 0;

		// TODO: This is a workaround to prevent infinite HARD_RESETs
		// when booting ServoV4p1 with no PSU (disable PD on FORCE_SNK)
		
/*
		if(!is_charge_through_allowed() && !(cc_config & CC_SNK_WITH_PD)){
			CPRINTS("WORKAROUND TURNING OFF PD");
			pd_comm_enable(DUT, 0);
		}
*/

	} else {
		/* Advertise the 'best' PDOs at various discrete voltages */
		if (active_charge_supplier == CHARGE_SUPPLIER_PD) {
			src_index = 0;
			snk_index = -1;

			for (i = 0; i < ARRAY_SIZE(pd_src_voltages_mv); ++i) {
				/* Adhere to board voltage limits */
				if (pd_src_voltages_mv[i] >
				    max_supported_voltage())
					break;

				/* Find the 'best' PDO <= voltage */
				pdo_index =
				pd_find_pdo_index(pd_get_src_cap_cnt(CHG),
					pd_get_src_caps(CHG),
					pd_src_voltages_mv[i], &pdo);
				/* Don't duplicate PDOs */
				if (pdo_index == snk_index)
					continue;
				/* Skip battery / variable PDOs */
				if ((pdo & PDO_TYPE_MASK) != PDO_TYPE_FIXED)
					continue;

				snk_index = pdo_index;
				pd_extract_pdo_power(pdo, &max_ma, &max_mv);

				pd_src_chg_pdo[src_index] =
					PDO_FIXED_VOLT(max_mv) |
					PDO_FIXED_CURR(max_ma) ;

				if (src_index == 0) {
					// TODO: 1st PDO *should* always be 5V PDO.
					// But not always with bad DUT. Should re-index and re-map.
					// TODO: Add variable voltage PDO conversion.
					pd_src_chg_pdo[src_index] |= DUT_PDO_FIXED_FLAGS | \
						PDO_FIXED_UNCONSTRAINED;
				}
				src_index++;
			}
			chg_pdo_cnt = src_index;
		} else {
			/* 5V PDO */
			pd_src_chg_pdo[0] = PDO_FIXED_VOLT(PD_MIN_MV) |
				PDO_FIXED_CURR(vbus[CHG].ma) |
				DUT_PDO_FIXED_FLAGS |
				PDO_FIXED_UNCONSTRAINED;

			chg_pdo_cnt = 1;
		}
	}

	/* Call DUT port manager to update Rp and possible PD contract */
	board_manage_dut_port();
}

/*============== Hook functions ============*/
#if 1
#ifdef CONFIG_USB_PD_TCPMV1
static void tusb1064_tcpm_hook_connect(void)
{
	int port = TASK_ID_TO_PD_PORT(task_get_current());
	//int reg;
	int allow_pd=0;
	enum pd_data_role current_data;

	/* TODO: Leave gratuitous warning in until HOOK method is deprecated. */
	/* Investigate proper solution rearchitecting TCPMv1 */

	CPRINTS("I'M HOOKED ON A FEELING on port (%d)!",port);
	//mux_state_t muxptr* = &usb_muxes[port];

	if(port == CHG){
		CPRINTS("WHOOPS WRONG PORT: CHG");
		return;
	}

	current_data = pd_get_data_role(port);

	/* Handle various mux connect cases */
	// TODO: Put this in a mux driver (for UFP)!
	CPRINTS("HOOK data role is [%d] [st%d] [%s]", current_data, pd_get_task_state(port), pd_get_task_state_name(port));

#if 0
/* Hold off on this -- see if we can fix TCPMv1 */
	/* Needed because HOOK is called before data_role_set() */
	switch(pd_get_task_state(port)){
	case PD_STATE_SNK_DISCONNECTED_DEBOUNCE:
		current_data=PD_ROLE_UFP;
	break;
	case PD_STATE_SRC_DISCONNECTED_DEBOUNCE:
		current_data=PD_ROLE_DFP;
	break;
	default:
		current_data=PD_ROLE_DISCONNECTED;
	break;
	}
	pd_execute_data_swap(port, current_data);

	/*
	//tusb1064_set_mux()
	reg = REG_GENERAL_CTLSEL_USB3 | \
		((cc_config & CC_POLARITY)?  REG_GENERAL_FLIPSEL : 0);
	tusb1064_write_byte(I2C_PORT_MASTER, TUSB1064_REG_GENERAL, reg);
	*/
	
	// TODO: Add is_pd_allowed() call here to properly handle policy
	// HACKHACKHACK: we should have a callback... this returns too fast.
#endif
	allow_pd = is_pd_allowed();
	CPRINTS("HOOK: ALLOW PD IS %d",allow_pd);
	pd_comm_enable(DUT, allow_pd);

	return;
}
DECLARE_HOOK(HOOK_USB_PD_CONNECT, tusb1064_tcpm_hook_connect, HOOK_PRIO_DEFAULT);


static void tusb1064_tcpm_hook_disconnect(void)
{

	int port = TASK_ID_TO_PD_PORT(task_get_current());
	//int reg;

	//DO POWER RESET THING
	enum pd_dual_role_states preferred_drp;
	enum pd_dual_role_states current_drp;
	enum pd_power_role current_power;
	int allow_pd=0;
	enum pd_data_role current_data;

	/* TODO: Re-init mux properly until HOOK method is deprecated */
	/* Investigate proper solution rearchitecting TCPMv1 */
	init_tusb1064(1);


	CPRINTS("RESET A FEELING on port (%d)!",port);
	//mux_state_t muxptr* = &usb_muxes[port];

	if(port == CHG){
		CPRINTS("WHOOPS WRONG PORT: CHG");
		return;
	}


	current_data = pd_get_data_role(port);
	CPRINTS("RESET data role is [%d]", current_data);
	// TODO: Verify we **should** be PD_ROLE_DISCONNECTED if here..?

#if 0
/* Hold off on this -- see if we can fix TCPMv1 */
	/* Needed because HOOK is called before data_role_set() */
	switch(pd_get_task_state(port)){
	default:
		current_data=PD_ROLE_DISCONNECTED;
	break;
	}

	pd_execute_data_swap(port, current_data);

	/*
	 reg = REG_GENERAL_CTLSEL_DISABLE | \
	 	((cc_config & CC_POLARITY)?  REG_GENERAL_FLIPSEL : 0);
	 tusb1064_write_byte(I2C_PORT_MASTER, TUSB1064_REG_GENERAL, reg);
	*/
#endif
	/* CopyPasta to re-set Rp source on disconnect */
	// HACKHACKHACK: we should have a callback... this returns too fast.
	// TODO: WIP: HACKHACKHACK: Segregate this into a function

	current_drp = pd_get_dual_role(DUT);
	current_power = pd_get_power_role(DUT);
	preferred_drp = PD_DRP_FORCE_SINK;
	/* Assume the default value of Rd */
	// TODO: This should be handled in get_dual_role_of_src()

	if (is_charge_through_allowed()){
		preferred_drp = get_dual_role_of_src();

		if (current_power == PD_ROLE_SINK) {

		// This initial call MUST be DRP_FORCE_SOURCE to trigger clean state transition.
		pd_set_dual_role(DUT, PD_DRP_FORCE_SOURCE);
		//pd_set_dual_role(DUT, get_dual_role_of_src());

		//============ FORCE A DISCONNECT ============
			pd_power_supply_reset(DUT);

			/* Remove Rp/Rd on both CC lines */
			/* ROLE_CONTROL  Open (Disconnect or don’t care) */
			pd_comm_enable(DUT, 0);			
			pd_set_rp_rd(DUT, TYPEC_CC_OPEN, rp_value_stored);

			/*
			 * If just changing mode (cc keeps enabled), give some
			 * time for DUT to detach, use tErrorRecovery.
			 */
			usleep(PD_T_ERROR_RECOVERY);
		//============================================

		if (!(cc_config & CC_ENABLE_DRP))
			pd_set_rp_rd(DUT, TYPEC_CC_RP, rp_value_stored);


		allow_pd=is_pd_allowed();
		CPRINTS("HOOK RESET: ALLOW PD IS %d",allow_pd);
		pd_comm_enable(DUT,0);
		CPRINTS("RESET PD allowed [%d] Power [%d] DRP [%d] DRP-Pref [%d] CC_CONFIG [0x%x]", allow_pd, current_power, current_drp, preferred_drp, cc_config);

		/*
		 * Update PD contract to reflect new available CHG
		 * voltage/current values.
		 */

		pd_update_contract(DUT);
		}
	}

	board_manage_dut_port();
}
DECLARE_HOOK(HOOK_USB_PD_DISCONNECT, tusb1064_tcpm_hook_disconnect, HOOK_PRIO_DEFAULT);
#endif
#endif
/*============== End hooks =============*/

int board_set_active_charge_port(int charge_port)
{
	if (charge_port == DUT)
		return -1;

	active_charge_port = charge_port;
	update_ports();

	if (!charge_port_is_active())
		/* Don't negotiate > 5V, except in lockstep with DUT */
		pd_set_external_voltage_limit(CHG, PD_MIN_MV);

	return 0;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	if (port != CHG)
		return;

	active_charge_supplier = supplier;

	/* Update the voltage/current values for CHG port */
	vbus[CHG].ma = charge_ma;
	vbus[CHG].mv = charge_mv;
	update_ports();
}

__override uint8_t board_get_src_dts_polarity(int port)
{
	/*
	 * When servo configured as srcdts, the CC polarity is based
	 * on the flags.
	 */
	if (port == DUT)
		return !!(cc_config & CC_POLARITY);

	return 0;
}

int pd_tcpc_cc_nc(int port, int cc_volt, int cc_sel)
{
	int rp_index;
	int nc;

	//TODO: This needs to be rewritten to use dynamic rp_value_stored
	// per port in question. Not hardcoded single one.
	rp_index = rp_value_stored;

	/* Can never be called from CHG port as it's sink only */
	// TODO: Above statement is not true. SNK can still be NC.
	// PD_SNK_VA_MV (250mV) indicates vRd-Connect(min)
	// Use PD_SRC_DEF_RD_THRESH_MV (200mV) to be "easy in, hard out"
	if (port == CHG){
		if (cc_volt < PD_SRC_DEF_RD_THRESH_MV)
		//if (cc_volt < PD_SNK_VA_MV)
			return 1;
		else
			return 0;
	}	

	//TODO: this should be hypothetical "cc_pull_applied()"
	// Use ACTUAL non-atomic state, rather than atomic variable
	switch(cc_pull_stored){
	case TYPEC_CC_OPEN:
		/*
		 * If cc_pull_stored is "OPEN", then always return not connected. This
		 * case should ONLY be called after all Rp GPIO controls are Hi-Z'ed.
		 */
		nc = 1;
		break;
	case TYPEC_CC_RP:
		/*
		* If DTS & RD, use special override mappings in pd_set_rp_rd
		* In this case, DTS SRC has 3x polarity-dependent states.
		*/
		if(!(cc_config & CC_DISABLE_DTS))
			nc = cc_volt >= pd_src_vnc_dts[rp_index][
			cc_config & CC_POLARITY ? !cc_sel : cc_sel];
		else
			nc = cc_volt >= pd_src_vnc[rp_index];

			//TODO: This logic is confusing.
			// Basically, if POLARITY, invert selected CC.
		break;
	case TYPEC_CC_RD:
	case TYPEC_CC_RA:
	case TYPEC_CC_RA_RD:	
		// TODO: This is messy and needs cleanup	
		/*
		* If DTS & RD, use special override mappings in pd_set_rp_rd
		* In this case, DTS SNK forces Rd+Rd state.
		*/
		// TODO: WARNING this may break PR_SWAPs
		// TODO: Check this for Ra.
		nc = cc_volt < PD_SRC_DEF_RD_THRESH_MV;
		break;
	default:
		CPRINTS("C%d: cc_nc invalid pull [%d]",port,cc_pull_stored);
		nc = 0;
		break;
	}

	return nc;
}

int pd_tcpc_cc_ra(int port, int cc_volt, int cc_sel)
{
	int rp_index;
	int ra;

	//TODO: This needs to be rewritten to use dynamic rp_value_stored
	// per port in question. Not hardcoded single one.
	rp_index = rp_value_stored;

	/* Can never be called from CHG port as it's sink only */
	// TODO: Above statement is not true. SNK can still be NC.
	// PD_SRC_DEF_RD_THRESH_MV indicates Ra threshold (SNK)
	if (port == CHG){
		if (cc_volt < PD_SRC_DEF_RD_THRESH_MV)
			return 1;
		else
			return 0;
	}

	switch(cc_pull_stored){
	case TYPEC_CC_OPEN:
		/*
		 * If cc_pull_stored is "OPEN", then always return not Ra. This
		 * case should only happen after all Rp GPIO controls are tri-stated.
		 */
		ra = 0;
		break;
	case TYPEC_CC_RP:
		/*
		* If DTS & RD, use special override mappings in pd_set_rp_rd
		* In this case, DTS SRC has three polarity-dependent states.
		*/
		if(!(cc_config & CC_DISABLE_DTS))
			ra = cc_volt < pd_src_rd_threshold_dts[rp_index][
					cc_config & CC_POLARITY ? !cc_sel : cc_sel];
		else
			ra = cc_volt < pd_src_rd_threshold[rp_index];
		break;
	case TYPEC_CC_RD:
	case TYPEC_CC_RA:
	case TYPEC_CC_RA_RD:
		/*
		* If DTS & RD, use special override mappings in pd_set_rp_rd
		* In this case, DTS SNK forces Rd+Rd state.
		*/
		ra = cc_volt < PD_SRC_DEF_RD_THRESH_MV;
		// SINKs always show vRa if nothing is connected.
		// This is normal. Just deal with it.
		break;
	default:
		ra = 0;
		break;
	}

	return ra;
}

int pd_adc_read(int port, int cc)
{
	int mv = -2;
	bool secondary;
	// TODO: This needs to be rewritten to use dynamic
	// per port in question. Not hardcoded single one.


	if (port == CHG) {
		mv = adc_read_channel(cc ? ADC_CHG_CC2_PD : ADC_CHG_CC1_PD);
		//CPRINTS("ADC port CHG [%d] cc [%d] mv [%d]",port,cc,mv);
		return mv;
	}

	if (cc_config & CC_DETACH_FAR) {
		/*
		 * When emulating detach, fake the voltage on CC to 0 to avoid
		 * triggering some debounce logic.
		 *
		 * The servo v4 makes Rd/Rp open but the DUT may present Rd/Rp
		 * alternatively that makes the voltage on CC falls into some
		 * unexpected range and triggers the PD state machine switching
		 * between SNK_DISCONNECTED and SNK_DISCONNECTED_DEBOUNCE.
		 */
		switch(cc_pull_stored){
		case TYPEC_CC_OPEN:
		case TYPEC_CC_RA_RD:
		case TYPEC_CC_RD:
		case TYPEC_CC_RA:
			mv=-1;
			break;
		case TYPEC_CC_RP:
			mv=3301;
			break;
		default:
			mv=-2;
			break;
		}
		return mv;
	}

	// If [Inverted=1] and [CC1=0] xor [Inverted=0] and [CC2=1]
	// i.e. If "secondary CC" line

	/*
	*  inverted (=cc_config & polarity)
	*         0     1
	*      ---------------
	* cc 0 |  pri   s    |
	*    1 |  s     pri  |
	*      ---------------
	*/

	if( !!(cc_config & CC_POLARITY) ^ !!(cc) ) 
		secondary = true;
	else
		secondary = false;

	mv = adc_read_channel(cc ? ADC_DUT_CC2_PD : ADC_DUT_CC1_PD);

	// Override values as necessary to not break FSM
	// AND don't fake DTS SRC/SNK readings

	if (cc_config & CC_DISABLE_DTS) {	
		switch(cc_pull_stored){
		case TYPEC_CC_OPEN:
			mv=-1;
			break;
		case TYPEC_CC_RD:
		case TYPEC_CC_RA_RD:
			if(secondary)
				mv=-1;
			break;			
		case TYPEC_CC_RA:
			// This is audio accessory
			// We may not want to fake this.
			break;
		case TYPEC_CC_RP:
			if(secondary){
				if(cc_config & CC_EMCA_SERVO)
					mv=pd_src_rd_threshold[rp_value_stored]/2;
					//Roughly simulate eMarker vRa @ rp_value
				else
					mv=3301;
			}	
			break;
		default:
			mv=-2;
			break;
		}
	}
	
	//CPRINTS("ADC port DUT [%d] cc [%d] mv [%d]",port,cc,mv);
	return mv;
}

static int board_set_rp(int rp)
{

	/*
	* IMPORTANT NOTE:
	* Always set lines preferring vRd-Connect state.
	* This means set Rd before Rp.
	*
	* When SRC/Rp: BREAK Rp-Old before APPLY Rp-New
	* When SNK/Rd: APPLY Rd-New new before BREAK Rd-Old
	*/

	if (cc_config & CC_DETACH_FAR) {
		rp_value_stored = rp;
		return EC_SUCCESS;
	}

	if (cc_config & CC_DISABLE_DTS) {
		/*
		 * DTS mode is disabled, so only present the requested Rp value
		 * on CC1 (active) and leave all Rp/Rd resistors on CC2
		 * (inactive) disconnected.
		 */
		switch (rp) {
		case TYPEC_RP_USB:
			DUT_ACTIVE_CC_OPEN(RP1A5);
			DUT_ACTIVE_CC_OPEN(RP3A0);
			DUT_ACTIVE_CC_PU(RPUSB);

			DUT_ACTIVE_CC_OPEN(RA);
			DUT_ACTIVE_CC_OPEN(RD);
			break;
		case TYPEC_RP_1A5:
			DUT_ACTIVE_CC_OPEN(RPUSB);
			DUT_ACTIVE_CC_OPEN(RP3A0);
			DUT_ACTIVE_CC_PU(RP1A5);

			DUT_ACTIVE_CC_OPEN(RA);
			DUT_ACTIVE_CC_OPEN(RD);
			break;
		case TYPEC_RP_3A0:
			DUT_ACTIVE_CC_OPEN(RPUSB);
			DUT_ACTIVE_CC_OPEN(RP1A5);
			DUT_ACTIVE_CC_PU(RP3A0);

			DUT_ACTIVE_CC_OPEN(RA);
			DUT_ACTIVE_CC_OPEN(RD);
			break;
		case TYPEC_RP_RESERVED:
			/*
			 * This case can be used to force a detach event since
			 * all values are set to inputs above. Nothing else to
			 * set.
			 */
			// The above comment is mistaken.
			// TYPEC_CC_OPEN = OPEN (with any pull).
			// TYPEC_RP_RESERVED = Invalid and shall not be used.
		default:
			return EC_ERROR_INVAL;
		}

		/* Handle EMCA case */
		if (cc_config & CC_EMCA_SERVO) {
			DUT_INACTIVE_CC_PD(RA);

			DUT_INACTIVE_CC_OPEN(RP3A0);
			DUT_INACTIVE_CC_OPEN(RP1A5);
			DUT_INACTIVE_CC_OPEN(RPUSB);
			DUT_INACTIVE_CC_OPEN(RD);
		}
		else {
			DUT_INACTIVE_CC_OPEN(RP3A0);
			DUT_INACTIVE_CC_OPEN(RP1A5);
			DUT_INACTIVE_CC_OPEN(RPUSB);

			DUT_INACTIVE_CC_OPEN(RA);
			DUT_INACTIVE_CC_OPEN(RD);
		}
	} else {
		/* DTS mode is enabled. The rp parameter is used to select the
		 * Type C current limit to advertise. The combinations of Rp on
		 * each CC line is shown in the table below.
		 *
		 * CC values for Debug sources (DTS)
		 *
		 * Source type  Mode of Operation   CC1    CC2
		 * ---------------------------------------------
		 * DTS          Default USB Power   Rp3A0  Rp1A5
		 * DTS          USB-C @ 1.5 A       Rp1A5  RpUSB
		 * DTS          USB-C @ 3 A         Rp3A0  RpUSB
		 */
		switch (rp) {
		case TYPEC_RP_USB:
			DUT_ACTIVE_CC_OPEN(RP1A5);
			DUT_ACTIVE_CC_OPEN(RPUSB);
			DUT_INACTIVE_CC_OPEN(RP3A0);
			DUT_INACTIVE_CC_OPEN(RPUSB);
			
			DUT_ACTIVE_CC_PU(RP3A0);
			DUT_INACTIVE_CC_PU(RP1A5);

			DUT_ACTIVE_CC_OPEN(RA);
			DUT_ACTIVE_CC_OPEN(RD);
			DUT_INACTIVE_CC_OPEN(RA);
			DUT_INACTIVE_CC_OPEN(RD);
			break;
		case TYPEC_RP_1A5:
			DUT_ACTIVE_CC_OPEN(RP3A0);
			DUT_ACTIVE_CC_OPEN(RPUSB);
			DUT_INACTIVE_CC_OPEN(RP3A0);
			DUT_INACTIVE_CC_OPEN(RP1A5);

			DUT_ACTIVE_CC_PU(RP1A5);
			DUT_INACTIVE_CC_PU(RPUSB);

			DUT_ACTIVE_CC_OPEN(RA);
			DUT_ACTIVE_CC_OPEN(RD);
			DUT_INACTIVE_CC_OPEN(RA);
			DUT_INACTIVE_CC_OPEN(RD);
			break;
		case TYPEC_RP_3A0:
			DUT_ACTIVE_CC_OPEN(RP1A5);
			DUT_ACTIVE_CC_OPEN(RPUSB);
			DUT_INACTIVE_CC_OPEN(RP3A0);
			DUT_INACTIVE_CC_OPEN(RP1A5);

			DUT_ACTIVE_CC_PU(RP3A0);
			DUT_INACTIVE_CC_PU(RPUSB);

			DUT_ACTIVE_CC_OPEN(RA);
			DUT_ACTIVE_CC_OPEN(RD);
			DUT_INACTIVE_CC_OPEN(RA);
			DUT_INACTIVE_CC_OPEN(RD);
			break;
		case TYPEC_RP_RESERVED:
			/*
			 * This case can be used to force a detach event since
			 * all values are set to inputs above. Nothing else to
			 * set.
			 */
			// The above comment is mistaken.
			// TYPEC_CC_OPEN = OPEN (with any pull).
			// TYPEC_RP_RESERVED = Invalid and shall not be used.

		default:
			CPRINTS("ERR: set_rp called with invalid value [%d]", rp);
			return EC_ERROR_INVAL;
		}
	}
	/* Save new Rp value for DUT port */
	rp_value_stored = rp;

	return EC_SUCCESS;
}

int pd_set_rp_rd(int port, int cc_pull, int rp_value)
{

	/*
	* IMPORTANT NOTE:
	* Always set lines preferring vRd-Connect state.
	* This means set Rd before Rp.
	*
	* When SRC/Rp: BREAK Rp-Old before APPLY Rp-New
	* When SNK/Rd: APPLY Rd-New new before BREAK Rd-Old
	*/

	int rv = EC_SUCCESS;

	if (port == CHG)
		return EC_ERROR_UNIMPLEMENTED;

	// HACKHACKHACK: Fix this to be cached somehow.
	// Use a matrix and only alter deltas.

	#if 1
		/* By default disconnect all Rp/Rd resistors from both CC lines */
		/* Set Rd for CC1/CC2 to High-Z. */
		DUT_BOTH_CC_OPEN(RD);
		/* Set Ra for CC1/CC2 to High-Z. */
		DUT_BOTH_CC_OPEN(RA);
		// Should probably have a "unplug cable" and "unplug far end" option
		/* Set Rp for CC1/CC2 to High-Z. */
		DUT_BOTH_CC_OPEN(RP3A0);
		DUT_BOTH_CC_OPEN(RP1A5);
		DUT_BOTH_CC_OPEN(RPUSB);
		/* Set TX Hi-Z */
		//DUT_BOTH_CC_OPEN(TX_DATA);
		// TODO: This may kill PD comms inadvertently
	#endif

	/* CC is disabled for emulating detach. Don't change Rd/Rp. */
	if (cc_config & CC_DETACH_FAR)
	{
		//This is a "DUT side disconnect"
		//NOOP, let Rp fall through.
		rv = EC_SUCCESS;
	} else if (cc_pull == TYPEC_CC_OPEN || cc_config & CC_DETACH_NEAR) {
		//This is a "SERVO side disconnect"
		//TODO: Fix this if it breaks things
		if ((cc_config & CC_DISABLE_DTS) && (cc_config & CC_EMCA_SERVO))
				DUT_INACTIVE_CC_PD(RA);
		rv = EC_SUCCESS;
	} else if (cc_pull == TYPEC_CC_RP) {
		rv = board_set_rp(rp_value);
	} else if ((cc_pull == TYPEC_CC_RD) || \
		(cc_pull == TYPEC_CC_RA_RD) || (cc_pull == TYPEC_CC_RA)) {
		/*
		 * The DUT port uses a captive cable. It can present any term
		 * pullup/down simultaneously on any CC1 and CC2 pin.
		 *
		 * If DTS mode is enabled, then present Rd on both CC lines.
		 *
		 * However, if DTS mode is disabled, only present Rd on CC1
		 * based on polarity. Inactive is set to Ra by fake EMCA bit.
		 */
		 if (cc_config & CC_DISABLE_DTS){
		 /* If DTS is NOT supported */
			switch(cc_pull) {
			case TYPEC_CC_RD:
				DUT_ACTIVE_CC_PD(RD);
				break;
			case TYPEC_CC_RA_RD:
				/*
				 * TODO: Verify this EMCA DUT (TYPEC_CC_RA_RD)
				 * statement  works
				*/
				DUT_ACTIVE_CC_PD(RD);
				DUT_INACTIVE_CC_PD(RA);
				break;
			case TYPEC_CC_RA:
				/*
				 * TODO: Verify this audio (TYPEC_CC_RA)
				 * statement works
				 */
				DUT_ACTIVE_CC_PD(RA);
				break;
			default:
				return EC_ERROR_UNIMPLEMENTED;
			}

			if ((cc_config & CC_EMCA_SERVO))
				DUT_INACTIVE_CC_PD(RA);

		}
		else {
			/* If DTS IS supported */
			switch(cc_pull) {
			case TYPEC_CC_RD:
				// EMCA is ignored (DTS)
				DUT_BOTH_CC_PD(RD);
				break;
			case TYPEC_CC_RA_RD:
				// EMCA is ignored (DTS)
				DUT_BOTH_CC_PD(RD);
				break;
			case TYPEC_CC_RA:
				// EMCA is ignored (DTS)
				DUT_BOTH_CC_PD(RD);
				break;
			default:
				return EC_ERROR_UNIMPLEMENTED;
			}
		}

		rv = EC_SUCCESS;
	} else
		return EC_ERROR_UNIMPLEMENTED;

	rp_value_stored = rp_value;
	cc_pull_stored = cc_pull;

	return rv;
}

int board_select_rp_value(int port, int rp)
{
	if (port == CHG)
		return EC_ERROR_UNIMPLEMENTED;

	/*
	 * Update Rp value to indicate non-pd power available.
	 * Do not change pull direction though.
	 */
	//if ((rp != rp_value_stored) && (cc_pull_stored == TYPEC_CC_RP)) {
	// WARNING: above line just causes state desync!
	if (cc_pull_stored == TYPEC_CC_RP)
	{
		rp_value_stored = rp;
		return pd_set_rp_rd(port, cc_pull_stored, rp);
	}

	return EC_SUCCESS;
}

int charge_manager_get_source_pdo(const uint32_t **src_pdo, const int port)
{
	int pdo_cnt = 0;

	/*
	 * If CHG is providing VBUS, then advertise what's available on
	 *  the CHG port, otherwise we provide no power.
	 */
	if (charge_port_is_active()) {
		*src_pdo =  pd_src_chg_pdo;
		pdo_cnt = chg_pdo_cnt;
	}

	return pdo_cnt;
}

__override void pd_transition_voltage(int idx)
{
	timestamp_t deadline;
	uint32_t ma, mv;

	pd_extract_pdo_power(pd_src_chg_pdo[idx - 1], &ma, &mv);
	/* Is this a transition to a new voltage? */
	if (charge_port_is_active() && vbus[CHG].mv != mv) {
		/*
		 * Alter voltage limit on charge port, this should cause
		 * the port to select the desired PDO.
		 */
		pd_set_external_voltage_limit(CHG, mv);

		/* Wait for CHG transition */
		deadline.val = get_time().val + PD_T_PS_TRANSITION;
		CPRINTS("Waiting for CHG port transition");
		while (charge_port_is_active() &&
		       vbus[CHG].mv != mv &&
		       get_time().val < deadline.val)
			msleep(10);

		if (vbus[CHG].mv != mv) {
			CPRINTS("Missed CHG transition, resetting DUT");
			pd_power_supply_reset(DUT);
			return;
		}

		CPRINTS("CHG transitioned");
	}

	vbus[DUT].mv = vbus[CHG].mv;
	vbus[DUT].ma = vbus[CHG].ma;
}

int pd_set_power_supply_ready(int port)
{
	/* Port 0 can never provide vbus. */
	if (port == CHG)
		return EC_ERROR_INVAL;

	if (charge_port_is_active()) {
		/* Enable VBUS */
		chg_power_select(CHG_POWER_VBUS);
		dut_chg_en(1);

		if (vbus[CHG].mv != PD_MIN_MV)
			CPRINTS("ERROR, CHG port voltage %d != PD_MIN_MV",
				vbus[CHG].mv);

		vbus[DUT].mv = vbus[CHG].mv;
		vbus[DUT].ma = vbus[CHG].mv;
		pd_set_dual_role(DUT, get_dual_role_of_src());
	} else {
		vbus[DUT].mv = 0;
		vbus[DUT].ma = 0;
		dut_chg_en(0);
		pd_set_dual_role(DUT, PD_DRP_FORCE_SINK);
		return EC_ERROR_NOT_POWERED;
	}

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Port 0 can never provide vbus. */
	if (port == CHG)
		return;

	/* Disable VBUS */
	chg_power_select(CHG_POWER_OFF);
	dut_chg_en(0);

	/* DUT is lost, back to 5V limit on CHG */
	pd_set_external_voltage_limit(CHG, PD_MIN_MV);
}

int pd_snk_is_vbus_provided(int port)
{
	return gpio_get_level(port ? GPIO_USB_DET_PP_DUT :
				     GPIO_USB_DET_PP_CHG);
}

__override int pd_check_power_swap(int port)
{
	// TODO: Fix this for ServoV4p1
	/*
	 * [ServoV4p0] When only host VBUS is available, then servo_v4 is not setting
	 * PDO_FIXED_UNCONSTRAINED in the src_pdo sent to the DUT. When this bit
	 * is not set, the DUT will always attempt to swap its power role to
	 * SRC. Let servo_v4 have more control over its power role by always
	 * rejecting power swap requests from the DUT.
	 */
	// DONE.  See new code below.

	int ret=0;
	int err=0;
	enum pd_power_role current_power;
	enum pd_dual_role_states current_drp;

	/* Port 0 can never provide vbus. */
	if (port == CHG)
		return 0;

	/* Handle DRP policy options of DUT port */

	current_power = pd_get_power_role(port);
	current_drp = pd_get_dual_role(port);

	switch(current_power){
	case PD_ROLE_SOURCE:
			/*
		if (pd_get_dual_role(port) != PD_DRP_FORCE_SOURCE)
			ret=1;
		*/

		switch(current_drp){
		case PD_DRP_FORCE_SOURCE:
			ret=0;
		break;
		case PD_DRP_FORCE_SINK:
		case PD_DRP_TOGGLE_ON:
		case PD_DRP_TOGGLE_OFF:
		case PD_DRP_FREEZE:
			ret=1;
		break;
		default:
			err=1;
			ret=0;
		break;
		}
	break;
	case PD_ROLE_SINK:
		// TODO: Fix this to match which VBUS provider
		// (HOST or CHG) based on (HOST_OR_CHG_CTL) GPIO

		// charge_port_is_active() ?
		// No, that checks active_charge_port
		// This should probably check port state is SNK_RDY

		/*
		if (pd_snk_is_vbus_provided(CHG) &&
				(cc_config & CC_ALLOW_SRC) &&
				pd_get_dual_role(port) != PD_DRP_FORCE_SINK)
			ret=1;
		*/

		switch(pd_get_dual_role(port)){
		case PD_DRP_FORCE_SINK:
		case PD_DRP_TOGGLE_OFF:
			ret=0;
		break;
		case PD_DRP_FORCE_SOURCE:
		case PD_DRP_TOGGLE_ON:
		case PD_DRP_FREEZE:
			ret=1;
		break;
		default:
			err=1;
			ret=0;
		break;
		}
	break;
	default:
		err=1;
		ret=0;
	break;
	}
	if (err)
		CPRINTS("C%d: check PR_SWAP invalid power:%d drp:%d",port, current_power, current_drp);
	return ret;
}

__override int pd_check_data_swap(int port,
				  enum pd_data_role data_role)
{
	/*
	 * Servo should allow data role swaps to let DUT see the USB hub, but
	 * doing it on CHG port is a waste as its data lines is unconnected.
	 */
	if (port == CHG)
		return 0;

	return 1;
}

__override void pd_execute_data_swap(int port,
				     enum pd_data_role data_role)
{
	/*
	 * TODO(b/137887386): Turn on the fastboot/DFU path when data swap to
	 * DFP?
	 */
	// DONE


	/*
	* This is absolutely required to D/C USB2 between roles. If it is not
	* present, the USB2 switch may remain intact across swaps. This will
	* allow Try.SRC to break USB3 hub enumeration on some bad DUT.
	*
	* (P0)TODO:
	* 1. Bad DUT don't "unmux" USB during Try.SRC.
	* 2. This leaves USB data hub in USB3+2 => USB2 => USB3+2 state.
	* 3. This then causes ServoV4 hub to "get stuck" in USB2 mode.
	* 4. You are now unable to mount any devices at USB3 anymore.
	*
	* Workaround: use a callback + debounce timer between mux events. 
	*
	* FOLLOWUP: This is a SafeMode mux bug. Not our problem.          
	*/
	static enum pd_data_role last_role=PD_ROLE_DISCONNECTED;
	//static timestamp_t deadline;

	if (port == CHG)
		return;

	/*
	*  ServoV4p1 REV1
	*  (*default init)
	*
	*                             HOST HUB
	*    DUT HUB                     |
	*       ^                        v
	*       |             [USERVO_FASTBOOT_MUX_SEL]
	*       |               1   (EN_L=Gnd*)   0*
	*       |               |                 |
	*       |               v                 |
	*       1*              0                 v
	*    [FASTBOOT_DUTHUB_MUX_SEL]      uServo Type-A
	*            (EN_L=1*)
	*                ^
	*                |
	*                v
	*              DUT CB
	*/  

	/* Handle various mux connect cases */
	// TODO: Put this in a mux driver (for UFP)!
	// DONE


#if 0
	if (get_time() < deadline){
		// We were called again before the deferred call.
	}

			deadline = get_time() + 200*MSEC;

			/* Pop open, wait a delay, then try agaan. */
			/* Cancel any pending function calls */
			hook_call_deferred(&delay_dr_swap_data, -1);
			hook_call_deferred(&delay_dr_swap_data, 200 * MSEC);
#endif

	if (last_role != data_role) {
		last_role=data_role;
			/* Disable USB2 lines */
			gpio_set_level(GPIO_FASTBOOT_DUTHUB_MUX_EN_L, 1);
			/* Disable USB3 lines */
			usb_mux_set(port, USB_PD_MUX_NONE, USB_SWITCH_DISCONNECT,
			   pd_get_polarity(port));
			CPRINTS("C%d: Exec_DRS: Debounce Open:%d", port, data_role);
	}

	switch(data_role){
	case PD_ROLE_DFP:
		/* Enable USB2 lines */
		gpio_set_level(GPIO_FASTBOOT_DUTHUB_MUX_EN_L, 0);
		dut_to_host();
		/* Disable USB3 lines (since Fastboot can't) */
		CPRINTS("C%d: Exec_DRS: DFP: Killing USB3", port);
		usb_mux_set(port, USB_PD_MUX_NONE, USB_SWITCH_CONNECT,
		   pd_get_polarity(port));
	break;
	case PD_ROLE_UFP:
		/* Enable USB2 lines */
		gpio_set_level(GPIO_FASTBOOT_DUTHUB_MUX_EN_L, 0);
		uservo_to_host();
		/* Enable USB3 lines */
		CPRINTS("C%d: Exec_DRS: UFP: Enabling USB3", port);
		usb_mux_set(port, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT,
			   pd_get_polarity(port));
	break;
	case PD_ROLE_DISCONNECTED:
		/* Disable USB2 lines */
		gpio_set_level(GPIO_FASTBOOT_DUTHUB_MUX_EN_L, 1);
		/* Disable USB3 lines */
		usb_mux_set(port, USB_PD_MUX_NONE, USB_SWITCH_DISCONNECT,
		   pd_get_polarity(port));
		CPRINTS("C%d: Exec_DRS: D/C: data:%d", port, data_role);
		break;
	default:
		/* Panic */
		CPRINTS("C%d: Exec_DRS: Invalid: data:%d",port, data_role);
	}
}

__override void pd_check_pr_role(int port,
				 enum pd_power_role pr_role,
				 int flags)
{
	/*
	 * Don't define any policy to initiate power role swap.
	 *
	 * CHG port is SNK only. DUT port requires a user to switch its
	 * role by commands. So don't do anything implicitly.
	 */
}

__override void pd_check_dr_role(int port,
				 enum pd_data_role dr_role,
				 int flags)
{
	if (port == CHG)
		return;

	/* If DFP, try to switch to UFP, to let DUT see the USB hub. */
	if ((flags & PD_FLAGS_PARTNER_DR_DATA) && dr_role == PD_ROLE_DFP)
		pd_request_data_swap(port);
}

#if 0
__override mux_state_t get_mux_mode_to_set(int port)
{
	/* Override improper "DFP-only" logic from usb_common.c */

	mux_state_t temp=0;
	return temp;
}
#endif

/* ----------------- Vendor Defined Messages ------------------ */
/*
 * DP alt-mode config, user configurable.
 * Default is the mode disabled, supporting the C and D pin assignment,
 * multi-function preferred, and a plug.
 */
static int alt_dp_config = (ALT_DP_PIN_C | ALT_DP_PIN_D | ALT_DP_MF_PREF |
			    ALT_DP_PLUG);

/**
 * Get the pins based on the user config.
 */
static int alt_dp_config_pins(void)
{
	int pins = 0;

	if (alt_dp_config & ALT_DP_PIN_C)
		pins |= MODE_DP_PIN_C;
	if (alt_dp_config & ALT_DP_PIN_D)
		pins |= MODE_DP_PIN_D;
	return pins;
}

/**
 * Get the cable outlet value (plug or receptacle) based on the user config.
 */
static int alt_dp_config_cable(void)
{
	return (alt_dp_config & ALT_DP_PLUG) ? CABLE_PLUG : CABLE_RECEPTACLE;
}

const uint32_t vdo_idh = VDO_IDH(0, /* data caps as USB host */
				 1, /* data caps as USB device */
				 IDH_PTYPE_AMA, /* Alternate mode */
				 1, /* supports alt modes */
				 USB_VID_GOOGLE);

const uint32_t vdo_product = VDO_PRODUCT(CONFIG_USB_PID, CONFIG_USB_BCD_DEV);

const uint32_t vdo_ama = VDO_AMA(CONFIG_USB_PD_IDENTITY_HW_VERS,
				 CONFIG_USB_PD_IDENTITY_SW_VERS,
				 0, 0, 0, 0, /* SS[TR][12] */
				 0, /* Vconn power */
				 0, /* Vconn power required */
				 0, /* Vbus power required */
				 AMA_USBSS_U31_GEN1 /* USB SS support */);

static int svdm_response_identity(int port, uint32_t *payload)
{
	int dp_supported;
#if 0
	// TODO: Per USB-IF NAK should not be done here.
	// "Modes supported" should be a fixed item.
	// Move this to EnterMode check.
	dp_supported=true;
#else
	//HACKHACKHACK: This is a workaround for DP Compliance SafeMode bug
	// If SSUSB dies, this is the cause.
	dp_supported=!!(alt_dp_config & ALT_DP_ENABLE);
#endif

	if (dp_supported) {
		payload[VDO_I(IDH)] = vdo_idh;
		payload[VDO_I(CSTAT)] = VDO_CSTAT(0);
		payload[VDO_I(PRODUCT)] = vdo_product;
		payload[VDO_I(AMA)] = vdo_ama;
		return VDO_I(AMA) + 1;
	} else {
		return 0;
	}
}

static int svdm_response_svids(int port, uint32_t *payload)
{
	payload[1] = VDO_SVID(USB_SID_DISPLAYPORT, 0);
	return 2;
}

#define MODE_CNT 1
#define OPOS 1

/*
 * The Type-C demux TUSB1064 supports pin assignment C and D. Response the DP
 * capabilities with supporting all of them.
 */
uint32_t vdo_dp_mode[MODE_CNT];

static int svdm_response_modes(int port, uint32_t *payload)
{
	vdo_dp_mode[0] =
		VDO_MODE_DP(0,             /* UFP pin cfg supported: none */
			    alt_dp_config_pins(),  /* DFP pin */
			    1,             /* no usb2.0 signalling in AMode */
			    alt_dp_config_cable(), /* plug or receptacle */
			    MODE_DP_V13,   /* DPv1.3 Support, no Gen2 */
			    MODE_DP_SNK);  /* Its a sink only */


#if 0
	/* CCD uses the SBU lines; don't enable DP when dts-mode enabled */
	// TODO: This shouldn't be handled here.
	// It should be handled in EnterMode per USB-IF
	if (!(cc_config & CC_DISABLE_DTS))
		return 0; /* NAK */
#endif

	if (PD_VDO_VID(payload[0]) != USB_SID_DISPLAYPORT)
		return 0; /* NAK */

	memcpy(payload + 1, vdo_dp_mode, sizeof(vdo_dp_mode));
	return MODE_CNT + 1;
}

static int is_typec_dp_muxed(void)
{
	int reg;
	int val;

	val = tusb1064_read_byte(I2C_PORT_MASTER, \
			TUSB1064_REG_GENERAL, &reg);
	if (val)
		return 0;

	if (reg & REG_GENERAL_CTLSEL_ANYDP)
		val=1;
	else
		val=0;

	return val;
}

static void set_typec_mux(int pin_cfg)
{
	/*
	int val, reg;
	val = tusb1064_read_byte(I2C_PORT_MASTER, \
			TUSB1064_REG_GENERAL, &reg);
	if (val)
		return;
	reg &= ~REG_GENERAL_CTLSEL_MASK;
	*/
	const int port=DUT;

	switch (pin_cfg) {
	case 0:
		CPRINTS("PinCfg:off");
		//This command right here >:/
		// reg |= REG_GENERAL_CTLSEL_DISABLE;
		usb_mux_set(port, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT,
			pd_get_polarity(port));
		break;
	case MODE_DP_PIN_C:
		//reg |= REG_GENERAL_CTLSEL_4DP_LANES;
		usb_mux_set(port, USB_PD_MUX_DP_ENABLED, USB_SWITCH_CONNECT,
			pd_get_polarity(port));
		CPRINTS("PinCfg:C");
		break;
	case MODE_DP_PIN_D:
		//reg |= REG_GENERAL_CTLSEL_2DP_AND_USB3;
		usb_mux_set(port, USB_PD_MUX_DOCK, USB_SWITCH_CONNECT,
			pd_get_polarity(port));
		CPRINTS("PinCfg:D");
		break;
	default:
		CPRINTS("PinCfg not supported: %d", pin_cfg);
		return;
		break;
	}
	/*
	if (reg && (cc_config & CC_POLARITY))
		reg |= REG_GENERAL_FLIPSEL;
	else
		reg &= ~REG_GENERAL_FLIPSEL;

	val = tusb1064_write_byte(I2C_PORT_MASTER, TUSB1064_REG_GENERAL, reg);
	*/
	return;
}

static int get_hpd_level(void)
{
	if (alt_dp_config & ALT_DP_OVERRIDE_HPD)
		return (alt_dp_config & ALT_DP_HPD_LVL) != 0;
	else
		return gpio_get_level(GPIO_DP_HPD);
}

static int dp_status(int port, uint32_t *payload)
{
	int opos = PD_VDO_OPOS(payload[0]);
	int hpd = get_hpd_level();

	if (opos != OPOS)
		return 0;  /* NAK */

	payload[1] = VDO_DP_STATUS(
		0,                /* IRQ_HPD */
		hpd,              /* HPD_HI|LOW */
		0,                /* request exit DP */
		0,                /* request exit USB */
		(alt_dp_config & ALT_DP_MF_PREF) != 0,  /* MF pref */
		is_typec_dp_muxed(),
		0,                /* power low */
		hpd ? 0x2 : 0);

	return 2;
}

static int dp_config(int port, uint32_t *payload)
{
	if (PD_DP_CFG_DPON(payload[1]))
		set_typec_mux(PD_DP_CFG_PIN(payload[1]));

	return 1;
}

/* Whether alternate mode has been entered or not */
static int alt_mode;

static int svdm_enter_mode(int port, uint32_t *payload)
{
	/* SID & mode request is valid */
	if ((PD_VDO_VID(payload[0]) != USB_SID_DISPLAYPORT) ||
	    (PD_VDO_OPOS(payload[0]) != OPOS))
		return 0;  /* NAK */

	/* CCD uses the SBU lines; don't enable DP when dts-mode enabled */
	if (!(cc_config & CC_DISABLE_DTS)) {
		CPRINTS("WARNING: Tried to EnterMode DP with [CCD on AUX/SBU]");
		return 0; /* NAK */
	}
	else if (!(alt_dp_config & ALT_DP_ENABLE)) {
		CPRINTS("WARNING: Tried to EnterMode DP with [usbc dp off]");
		return 0; /* NAK */
	}

	alt_mode = OPOS;
	return 1;
}

int pd_alt_mode(int port, enum tcpm_transmit_type type, uint16_t svid)
{
	if (type != TCPC_TX_SOP)
		return 0;

	if (svid == USB_SID_DISPLAYPORT)
		return alt_mode;

	return 0;
}

static int svdm_exit_mode(int port, uint32_t *payload)
{
	if (PD_VDO_VID(payload[0]) == USB_SID_DISPLAYPORT)
		set_typec_mux(0);

	alt_mode = 0;

	return 1; /* Must return ACK */
}

static struct amode_fx dp_fx = {
	.status = &dp_status,
	.config = &dp_config,
};

const struct svdm_response svdm_rsp = {
	.identity = &svdm_response_identity,
	.svids = &svdm_response_svids,
	.modes = &svdm_response_modes,
	.enter_mode = &svdm_enter_mode,
	.amode = &dp_fx,
	.exit_mode = &svdm_exit_mode,
};

__override int pd_custom_vdm(int port, int cnt, uint32_t *payload,
		  uint32_t **rpayload)
{
	int cmd = PD_VDO_CMD(payload[0]);

	/* make sure we have some payload */
	if (cnt == 0)
		return 0;

	switch (cmd) {
	case VDO_CMD_VERSION:
		/* guarantee last byte of payload is null character */
		*(payload + cnt - 1) = 0;
		CPRINTF("ver: %s\n", (char *)(payload+1));
		break;
	case VDO_CMD_CURRENT:
		CPRINTF("Current: %dmA\n", payload[1]);
		break;
	}

	return 0;
}

__override const struct svdm_amode_fx supported_modes[] = {};
__override const int supported_modes_cnt = ARRAY_SIZE(supported_modes);

static void print_cc_mode(void)
{
	/* Get current CCD status */
	ccprintf("Policy flags:\n");
	ccprintf("    cc (@dut):    %s\n", cc_config & CC_DETACH_FAR ? "off" : "on");
	ccprintf("    cc (@cable):  %s\n", cc_config & CC_DETACH_NEAR ? "off" : "on");
	ccprintf("    polarity:     %s\n", cc_config & CC_POLARITY ? "cc2" : "cc1");
	ccprintf("    dts mode:     %s\n", cc_config & CC_DISABLE_DTS ? "off" : "on");
	ccprintf("    drp enabled:  %s\n", cc_config & CC_ENABLE_DRP ? "on" : "off");
	ccprintf("    chg allowed:  %s\n", cc_config & CC_ALLOW_SRC ? "on" : "off");
	ccprintf("    cable-eMark:  %s\n", cc_config & CC_EMCA_SERVO ? "emarked" : "non-emarked");
	ccprintf("    pd-as-src:    %s\n", cc_config & CC_SRC_WITH_PD ? "on" : "off");
	ccprintf("    pd-as-snk:    %s\n", cc_config & CC_SNK_WITH_PD  ? "on" : "off");
	ccprintf("State:\n");
	ccprintf("    pd enabled:   %s\n", pd_comm_is_enabled(DUT) ? "on" : "off");
	ccprintf("    chg mode:     %s\n", get_dut_chg_en() ? "on" : "off");

}


static void do_cc(int cc_config_new)
{
	int chargeable;
	int dualrole;
	int allow_pd;

	//TODO: This is clunky and calls itself
	// Clean this up to callback itself or something
	// HACKHACKHACK

	if (cc_config_new != cc_config) {

		/* Run if CC not previously detach */
		if (!(cc_config & CC_DETACH_FAR)) {
			/* Force detach */
			pd_power_supply_reset(DUT);

	//		/* Always set to 0 here so both CC lines are changed */
	//		cc_config &= ~(CC_DISABLE_DTS | CC_ALLOW_SRC);
	//Typo and logic?

			/* Remove Rp/Rd on both CC lines */
			/* ROLE_CONTROL  Open (Disconnect or don’t care) */
			pd_comm_enable(DUT, 0);			
			pd_set_rp_rd(DUT, TYPEC_CC_OPEN, rp_value_stored);

			/*
			 * If just changing mode (cc keeps enabled), give some
			 * time for DUT to detach, use tErrorRecovery.
			 */
			if (!(cc_config_new & CC_DETACH_FAR))
				usleep(PD_T_ERROR_RECOVERY);
		}

		if ((cc_config & ~cc_config_new) & CC_DISABLE_DTS) {
			/* DTS-disabled -> DTS-enabled */
			ccd_enable(1);
			ext_hpd_detection_enable(0);
		} else if ((cc_config_new & ~cc_config) & CC_DISABLE_DTS) {
			/* DTS-enabled -> DTS-disabled */
			ccd_enable(0);
			if (!(alt_dp_config & ALT_DP_OVERRIDE_HPD))
				ext_hpd_detection_enable(1);
		}

		/* Accept new cc_config value */
		cc_config = cc_config_new;

		if (!(cc_config & CC_DETACH_FAR)) {
			/* Can we source? */
			chargeable = is_charge_through_allowed();
			dualrole = chargeable ? get_dual_role_of_src() :
						PD_DRP_FORCE_SINK;
			pd_set_dual_role(DUT, dualrole);
			//usleep(200000);	//Give PD task time to tick


			/*
			 * If force_source or force_sink role, explicitly set
			 * the Rp or Rd resistors on CC lines.
			 *
			 * If DRP role, don't set any CC pull resistor, the PD
			 * state machine will toggle and set the pull resistors
			 * when needed.
			 */
			if (dualrole != PD_DRP_TOGGLE_ON)
				pd_set_host_mode(DUT, chargeable);

			/*
			 * For the normal lab use, emulating a sink has no PD
			 * comm, like a passive hub. For the PD FAFT use, we
			 * need to validate some PD behavior, so a flag
			 * CC_SNK_WITH_PD to force enabling PD comm.
			 */

			//TODO: This is wrong, we need "is_pd_allowed()" function
			//allow_pd = (cc_config & CC_SNK_WITH_PD) || CC_DISABLE_DTS || chargeable;
			allow_pd = is_pd_allowed();
			CPRINTS("do_cc: ALLOW PD IS %d",allow_pd);
			pd_comm_enable(DUT, allow_pd);
		}
	}
}

static int command_cc(int argc, char **argv)
{
	int cc_config_new = cc_config;

	if (argc < 2) {
		print_cc_mode();
		return EC_SUCCESS;
	}

	// Override #define for help strings since we overflow flash
	if (!strcasecmp(argv[1], "help")){
		CPRINTS("Usage: cc [off|on] or (pd)(src|snk|drp)(dts) or [emca|nonemca]"
			" [cc1|cc2]");
	}
	else if (!strcasecmp(argv[1], "off")) {
		cc_config_new |= CC_DETACH_FAR;
	} else if (!strcasecmp(argv[1], "on")) {
		cc_config_new &= ~CC_DETACH_FAR;
	} else {
		cc_config_new &= ~CC_DETACH_FAR;
		if (!strcasecmp(argv[1], "src"))
			cc_config_new = CONF_SRC(cc_config_new);
		else if (!strcasecmp(argv[1], "pdsrc"))
			cc_config_new = CONF_PDSRC(cc_config_new);	
		else if (!strcasecmp(argv[1], "snk"))
			cc_config_new = CONF_SNK(cc_config_new);	
		else if (!strcasecmp(argv[1], "pdsnk"))
			cc_config_new = CONF_PDSNK(cc_config_new);		
		else if (!strcasecmp(argv[1], "drp"))
			cc_config_new = CONF_DRP(cc_config_new);
		else if (!strcasecmp(argv[1], "pddrp"))
			cc_config_new = CONF_PDDRP(cc_config_new);
		else if (!strcasecmp(argv[1], "srcdts"))
			cc_config_new = CONF_SRCDTS(cc_config_new);
		else if (!strcasecmp(argv[1], "pdsrcdts"))
			cc_config_new = CONF_PDSRCDTS(cc_config_new);
		else if (!strcasecmp(argv[1], "snkdts"))
			cc_config_new = CONF_SNKDTS(cc_config_new);
		else if (!strcasecmp(argv[1], "pdsnkdts"))
			cc_config_new = CONF_PDSNKDTS(cc_config_new);
		else if (!strcasecmp(argv[1], "drpdts"))
			cc_config_new = CONF_DRPDTS(cc_config_new);
		else if (!strcasecmp(argv[1], "emca"))
			cc_config_new |= CC_EMCA_SERVO;
		else if (!strcasecmp(argv[1], "nonemca"))
			cc_config_new &= ~CC_EMCA_SERVO;
		else
			return EC_ERROR_PARAM2;
	}

	if (!strcasecmp(argv[2], "cc1"))
		cc_config_new &= ~CC_POLARITY;
	else if (!strcasecmp(argv[2], "cc2"))
		cc_config_new |= CC_POLARITY;
	else if (argc >= 3)
		return EC_ERROR_PARAM3;

	do_cc(cc_config_new);
	print_cc_mode();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(cc, command_cc,
			"[off|on|src|snk|pdsnk|drp|srcdts|snkdts|pdsnkdts|"
			"drpdts|emca|nonemca] [cc1|cc2]",
			"Servo_v4 DTS and CHG mode");

static void fake_disconnect_end(void)
{
	/* Reenable CC lines with previous dts and src modes */
	do_cc(cc_config & ~CC_DETACH_FAR);
}
DECLARE_DEFERRED(fake_disconnect_end);

static void fake_disconnect_start(void)
{
	/* Disable CC lines */
	do_cc(cc_config | CC_DETACH_FAR);

	hook_call_deferred(&fake_disconnect_end_data,
			   fake_pd_disconnect_duration_us);
}
DECLARE_DEFERRED(fake_disconnect_start);

static int cmd_fake_disconnect(int argc, char *argv[])
{
	int delay_ms, duration_ms;
	char *e;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	delay_ms = strtoi(argv[1], &e, 0);
	if (*e || delay_ms < 0)
		return EC_ERROR_PARAM1;
	duration_ms = strtoi(argv[2], &e, 0);
	if (*e || duration_ms < 0)
		return EC_ERROR_PARAM2;

	/* Cancel any pending function calls */
	hook_call_deferred(&fake_disconnect_start_data, -1);
	hook_call_deferred(&fake_disconnect_end_data, -1);

	fake_pd_disconnect_duration_us = duration_ms * MSEC;
	hook_call_deferred(&fake_disconnect_start_data, delay_ms * MSEC);

	ccprintf("Fake disconnect for %d ms starting in %d ms.\n",
		duration_ms, delay_ms);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fakedisconnect, cmd_fake_disconnect,
			"<delay_ms> <duration_ms>", NULL);

static int cmd_ada_srccaps(int argc, char *argv[])
{
	int i;
	const uint32_t * const ada_srccaps = pd_get_src_caps(CHG);

	for (i = 0; i < pd_get_src_cap_cnt(CHG); ++i) {
		uint32_t max_ma, max_mv;

		pd_extract_pdo_power(ada_srccaps[i], &max_ma, &max_mv);
		ccprintf("%d: %dmV/%dmA\n", i, max_mv, max_ma);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ada_srccaps, cmd_ada_srccaps,
			"",
			"Print adapter SrcCap");

static void chg_pd_disconnect(void)
{
	/* Clear charger PDO on CHG port disconnected. */
	if (pd_is_disconnected(CHG))
		pd_set_src_caps(CHG, 0, NULL);
}
DECLARE_HOOK(HOOK_USB_PD_DISCONNECT, chg_pd_disconnect, HOOK_PRIO_DEFAULT);

static int cmd_dp_action(int argc, char *argv[])
{
	int i;
	char *e;

	if (argc < 1)
		return EC_ERROR_PARAM_COUNT;

	if (argc == 1) {
		CPRINTS("DP alt-mode: %s",
			(alt_dp_config & ALT_DP_ENABLE) ? "enable" : "disable");
	}
	else if (!strcasecmp(argv[1], "enable") ||
			!strcasecmp(argv[1], "on")) {
		alt_dp_config |= ALT_DP_ENABLE;
	} else if (!strcasecmp(argv[1], "disable") ||
			!strcasecmp(argv[1], "off")) {
		alt_dp_config &= ~ALT_DP_ENABLE;
	} else if (!strcasecmp(argv[1], "pins")) {
		if (argc >= 3) {
			alt_dp_config &= ~(ALT_DP_PIN_C | ALT_DP_PIN_D);
			for (i = 0; i < 3; i++) {
				if (!argv[2][i])
					break;

				switch (argv[2][i]) {
				case 'c':
				case 'C':
					alt_dp_config |= ALT_DP_PIN_C;
					break;
				case 'd':
				case 'D':
					alt_dp_config |= ALT_DP_PIN_D;
					break;
				}
			}
		}
		CPRINTS("Pins: %s%s",
			(alt_dp_config & ALT_DP_PIN_C) ? "C" : "",
			(alt_dp_config & ALT_DP_PIN_D) ? "D" : "");
	} else if (!strcasecmp(argv[1], "mf")) {
		if (argc >= 3) {
			i = strtoi(argv[2], &e, 10);
			if (*e)
				return EC_ERROR_PARAM3;
			if (i)
				alt_dp_config |= ALT_DP_MF_PREF;
			else
				alt_dp_config &= ~ALT_DP_MF_PREF;
		}
		CPRINTS("MF pref: %d", (alt_dp_config & ALT_DP_MF_PREF) != 0);
	} else if (!strcasecmp(argv[1], "plug")) {
		if (argc >= 3) {
			i = strtoi(argv[2], &e, 10);
			if (*e)
				return EC_ERROR_PARAM3;
			if (i)
				alt_dp_config |= ALT_DP_PLUG;
			else
				alt_dp_config &= ~ALT_DP_PLUG;
		}
		CPRINTS("Plug or receptacle: %d",
			(alt_dp_config & ALT_DP_PLUG) != 0);
	} else if (!strcasecmp(argv[1], "hpd")) {
		if (argc >= 3) {
			if (!strncasecmp(argv[2], "ext", 3)) {
				alt_dp_config &= ~ALT_DP_OVERRIDE_HPD;
				ext_hpd_detection_enable(1);
			} else if (!strncasecmp(argv[2], "h", 1)) {
				alt_dp_config |= ALT_DP_OVERRIDE_HPD;
				alt_dp_config |= ALT_DP_HPD_LVL;
				/*
				 * Modify the HPD to high. Need to enable the
				 * external HPD signal monitoring. A monitor
				 * may send a IRQ at any time to notify DUT.
				 */
				ext_hpd_detection_enable(1);
				pd_send_hpd(DUT, hpd_high);
			} else if (!strncasecmp(argv[2], "l", 1)) {
				alt_dp_config |= ALT_DP_OVERRIDE_HPD;
				alt_dp_config &= ~ALT_DP_HPD_LVL;
				ext_hpd_detection_enable(0);
				pd_send_hpd(DUT, hpd_low);
			} else if (!strcasecmp(argv[2], "irq")) {
				pd_send_hpd(DUT, hpd_irq);
			}
		}
		CPRINTS("HPD source: %s",
			(alt_dp_config & ALT_DP_OVERRIDE_HPD) ? "overridden"
							      : "external");
		CPRINTS("HPD level: %d", get_hpd_level());
	}  else if (!strcasecmp(argv[1], "help")) {
		CPRINTS("Usage: usbc_action dp [enable<on>|disable<off>|hpd|mf|pins|"
			"plug]");
	} else {
		CPRINTS("Bad parameter: [%s]", argv[1]);
	}

	return EC_SUCCESS;
}

static int cmd_usbc_action(int argc, char *argv[])
{
	if (argc >= 2 && !strcasecmp(argv[1], "dp"))
		return cmd_dp_action(argc - 1, &argv[1]);

	if (argc != 2 && argc != 3)
		return EC_ERROR_PARAM_COUNT;

	/* TODO(b:140256624): drop *v command if we migrate to chg cmd. */
	if (!strcasecmp(argv[1], "5v")) {
		do_cc(CONF_SRC(cc_config));
		user_limited_max_mv = 5000;
		update_ports();
	} else if (!strcasecmp(argv[1], "12v")) {
		do_cc(CONF_SRC(cc_config));
		user_limited_max_mv = 12000;
		update_ports();
	} else if (!strcasecmp(argv[1], "20v")) {
		do_cc(CONF_SRC(cc_config));
		user_limited_max_mv = 20000;
		update_ports();
	} else if (!strcasecmp(argv[1], "dev")) {
		/* Set the limit back to original */
		user_limited_max_mv = 20000;
		do_cc(CONF_PDSNK(cc_config));
	} else if (!strcasecmp(argv[1], "pol0")) {
		do_cc(cc_config & ~CC_POLARITY);
	} else if (!strcasecmp(argv[1], "pol1")) {
		do_cc(cc_config | CC_POLARITY);
	} else if (!strcasecmp(argv[1], "drp")) {
		/* Toggle the DRP state, compatible with Plankton. */
		do_cc(cc_config ^ CC_ENABLE_DRP);
		CPRINTF("DRP = %d, host_mode = %d\n",
			!!(cc_config & CC_ENABLE_DRP),
			!!(cc_config & CC_ALLOW_SRC));
	} else if (!strcasecmp(argv[1], "chg")) {
		int sink_v;

		if (argc != 3)
			return EC_ERROR_PARAM2;

		sink_v = atoi(argv[2]);
		if (!sink_v)
			return EC_ERROR_PARAM2;

		user_limited_max_mv = sink_v * 1000;
		do_cc(CONF_SRC(cc_config));
		update_ports();
		/*
		 * TODO(b:140256624): servod captures 'chg SRC' keyword to
		 * recognize if this command is supported in the firmware.
		 * Drop this message if when we phase out the usbc_role control.
		 */
		ccprintf("CHG SRC %dmV\n", user_limited_max_mv);
	} else {
		return EC_ERROR_PARAM1;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(usbc_action, cmd_usbc_action,
			"5v|12v|20v|dev|pol0|pol1|drp|dp|chg x(x=voltage)",
			"Set Servo v4 type-C port state");

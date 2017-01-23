/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lid switch module for Chrome EC */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "motion_lid.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SWITCH, outstr)
#define CPRINTS(format, args...) cprints(CC_SWITCH, format, ## args)

#define LID_DEBOUNCE_US    (30 * MSEC)  /* Debounce time for lid switch */

/* if no X-macro is defined for LID switch GPIO, use GPIO_LID_OPEN as default */
#ifndef CONFIG_LID_SWITCH_GPIO_LIST
#define CONFIG_LID_SWITCH_GPIO_LIST LID_GPIO(GPIO_LID_OPEN)
#endif

static int debounced_lid_open;		/* Debounced lid state */
static int forced_lid_open;	/* Forced lid open */

/**
 * Get raw lid switch state.
 *
 * @return 1 if lid is open, 0 if closed.
 */
static int raw_lid_open(void)
{
#define LID_GPIO(gpio) || gpio_get_level(gpio)
	return (forced_lid_open CONFIG_LID_SWITCH_GPIO_LIST) ? 1 : 0;
#undef LID_GPIO
}

/**
 * Handle lid open.
 */
int is_lid_angle_sensors_ready(void);

static void lid_switch_open(void)
{
	/* See the notes below */
	const int lid_angle_closed_low = 4;
	/* Some units show large angle (e.g. 358) when lid is closed */
	const int lid_angle_closed_high = 356;

	if (debounced_lid_open) {
		CPRINTS("lid already open");
		return;
	}

	/*
	 * When a device with the lid closed is stacked on top of another,
	 * the bottom lid magnet may create magnetic field strong enough to
	 * cancel the top lid magnetic field, thus the device wakes up
	 * unintentionally by a false lid open event. This is
	 * indistinguishable from a real lid open event (unless the bottom
	 * lid magnet is so strong that it also triggers TABLET_MODE_L).
	 *
	 * To avoid this, we read a lid angle to check the lid is really open.
	 *
	 * lid_angle_closed_low has to be smaller than the angle at which
	 * LID_OPEN triggers and larger than the angle of the closed lid.
	 */
	if (is_lid_angle_sensors_ready()) {
		int lid_angle;
		CPRINTS("Calculating lid_angle");
		motion_lid_calc(1);
		lid_angle = motion_lid_get_angle();
		CPRINTS("lid_angle=%d", lid_angle);
		if (lid_angle < lid_angle_closed_low ||
				lid_angle_closed_high < lid_angle) {
			/* Angles are too small. Lid doesn't look opened. */
			CPRINTS("false lid open");
			return;
		}
	}

	CPRINTS("lid open");
	debounced_lid_open = 1;
	hook_notify(HOOK_LID_CHANGE);
	host_set_single_event(EC_HOST_EVENT_LID_OPEN);
}

/**
 * Handle lid close.
 */
static void lid_switch_close(void)
{
	if (!debounced_lid_open) {
		CPRINTS("lid already closed");
		return;
	}

	CPRINTS("lid close");
	debounced_lid_open = 0;
	hook_notify(HOOK_LID_CHANGE);
	host_set_single_event(EC_HOST_EVENT_LID_CLOSED);
}

test_mockable int lid_is_open(void)
{
	return debounced_lid_open;
}

/**
 * Lid switch initialization code
 */
static void lid_init(void)
{
	if (raw_lid_open())
		debounced_lid_open = 1;

	/* Enable interrupts, now that we've initialized */
#define LID_GPIO(gpio) gpio_enable_interrupt(gpio);
	CONFIG_LID_SWITCH_GPIO_LIST
#undef LID_GPIO
}
DECLARE_HOOK(HOOK_INIT, lid_init, HOOK_PRIO_INIT_LID);

/**
 * Handle debounced lid switch changing state.
 */
static void lid_change_deferred(void)
{
	const int new_open = raw_lid_open();

	/* If lid hasn't changed state, nothing to do */
	if (new_open == debounced_lid_open)
		return;

	if (new_open)
		lid_switch_open();
	else
		lid_switch_close();
}
DECLARE_DEFERRED(lid_change_deferred);

void lid_interrupt(enum gpio_signal signal)
{
	/* Reset lid debounce time */
	hook_call_deferred(&lid_change_deferred_data, LID_DEBOUNCE_US);
}

static int command_lidopen(int argc, char **argv)
{
	lid_switch_open();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(lidopen, command_lidopen,
			NULL,
			"Simulate lid open");

static int command_lidclose(int argc, char **argv)
{
	lid_switch_close();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(lidclose, command_lidclose,
			NULL,
			"Simulate lid close");

static int command_lidstate(int argc, char **argv)
{
	ccprintf("lid state: %s\n", debounced_lid_open ? "open" : "closed");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(lidstate, command_lidstate,
			NULL,
			"Get state of lid");

/**
 * Host command to enable/disable lid opened.
 */
static int hc_force_lid_open(struct host_cmd_handler_args *args)
{
	const struct ec_params_force_lid_open *p = args->params;

	/* Override lid open if necessary */
	forced_lid_open = p->enabled ? 1 : 0;

	/* Make this take effect immediately; no debounce time */
	hook_call_deferred(&lid_change_deferred_data, 0);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FORCE_LID_OPEN, hc_force_lid_open,
		     EC_VER_MASK(0));

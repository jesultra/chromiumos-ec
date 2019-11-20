/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NEW thermal engine module for Chrome EC. This is a completely different
 * implementation from the original version that shipped on Link.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "hooks.h"
#include "host_command.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)

struct fan_step {
	int on1;
	int off1;
	int on2;
	int off2;
	int rpm0;
	int rpm1;
};

const struct fan_step fan_table[] = {
	{.on1 = 30,  .off1 =  0, .on2 = 32,  .off2 =  0, .rpm0 =    0, .rpm1 =    0},
	{.on1 = 62,  .off1 = 54, .on2 = 60,  .off2 = 58, .rpm0 = 4200, .rpm1 = 4700},
	{.on1 = 68,  .off1 = 61, .on2 = 66,  .off2 = 63, .rpm0 = 4400, .rpm1 = 4900},
	{.on1 = 75,  .off1 = 64, .on2 = 72,  .off2 = 69, .rpm0 = 4600, .rpm1 = 5100},
	{.on1 = 81,  .off1 = 73, .on2 = 80,  .off2 = 76, .rpm0 = 4800, .rpm1 = 5300},
	{.on1 = 88,  .off1 = 83, .on2 = 83,  .off2 = 81, .rpm0 = 5200, .rpm1 = 5700},
	{.on1 = 98,  .off1 = 90, .on2 = 95,  .off2 = 89, .rpm0 = 5600, .rpm1 = 6100},
	{.on1 = 100, .off1 = 91, .on2 = 100, .off2 = 90, .rpm0 = 6000, .rpm1 = 6500},
};

#define NUM_FAN_LEVELS ARRAY_SIZE(fan_table)

/*****************************************************************************/
/* EC-specific thermal controls */

test_mockable_static void smi_sensor_failure_warning(void)
{
	CPRINTS("can't read any temp sensors!");
	host_set_single_event(EC_HOST_EVENT_THERMAL);
}

int fan_table_to_rpm(int fan, int *temps)
{
	static int current_level;
	static int prev_temp1, prev_temp2;
	static int new_rpm;
	int i;

	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. decreasing path. (check the off point)
	 *  2. increasing path. (check the on point)
	 *  3. invariant path. (return the current RPM)
	 */
	if (temps[TEMP_SENSOR_1] < prev_temp1 ||
		 temps[TEMP_SENSOR_2] < prev_temp2) {
		for (i = current_level; i >= 0; i--) {
			if (temps[TEMP_SENSOR_1] <= fan_table[i].off1 &&
					temps[TEMP_SENSOR_2] <= fan_table[i].off2)
				current_level = i - 1;
			else
				break;
		}
	} else if (temps[TEMP_SENSOR_1] > prev_temp1 ||
			    temps[TEMP_SENSOR_2] > prev_temp2) {
		for (i = current_level; i < NUM_FAN_LEVELS; i++) {
			if (temps[TEMP_SENSOR_1] >= fan_table[i].on1 &&
					temps[TEMP_SENSOR_2] >= fan_table[i].on2)
				current_level = i + 1;
			else
				break;
		}
	}

	if (current_level < 0)
		current_level = 0;

	prev_temp1 = temps[TEMP_SENSOR_1];
	prev_temp2 = temps[TEMP_SENSOR_2];

	switch (fan) {
	case FAN_CH_0:
		new_rpm = fan_table[current_level].rpm0;
		break;
	case FAN_CH_1:
		new_rpm = fan_table[current_level].rpm1;
		break;
	default:
		break;
	}

	return new_rpm;
}

static void thermal_control(void)
{
	int i, t, rv;
	int num_sensors_read;
	int temp_fan_configured;
	int temps_all[TEMP_SENSOR_COUNT];
	int new_rpm;
	int fan;

	/* Get ready to count things */
	num_sensors_read = 0;
	temp_fan_configured = 0;
	new_rpm = 0;

	/* go through all the sensors */
	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {

		/* read one */
		rv = temp_sensor_read(i, &t);
		temps_all[i] = K_TO_C(t);

		if (rv != EC_SUCCESS)
			continue;
		else
			num_sensors_read++;

		temp_fan_configured = 1;
	}

	if (!num_sensors_read) {
		/*
		 * Trigger a SMI event if we can't read any sensors.
		 *
		 * In theory we could do something more elaborate like forcing
		 * the system to shut down if no sensors are available after
		 * several retries.  This is a very unlikely scenario -
		 * particularly on LM4-based boards, since the LM4 has its own
		 * internal temp sensor.  It's most likely to occur during
		 * bringup of a new board, where we haven't debugged the I2C
		 * bus to the sensors; forcing a shutdown in that case would
		 * merely hamper board bringup.
		 *
		 * If in G3, then there is no need trigger an SMI event since
		 * the AP is off and this can be an expected state if
		 * temperature sensors are powered by a power rail that's only
		 * on if the AP is out of G3. Note this could be 'ANY_OFF' as
		 * well, but that causes the thermal unit test to fail.
		 */
		if (!chipset_in_state(CHIPSET_STATE_HARD_OFF))
			smi_sensor_failure_warning();
		return;
	}

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		if (temp_fan_configured) {
			for (fan = 0; fan < fan_get_count(); fan++) {
				fan_set_rpm_mode(FAN_CH(fan), 1);
				new_rpm = fan_table_to_rpm(fan, (int *)temps_all);
				fan_set_rpm_target(FAN_CH(fan), new_rpm);
			}
		}
	}
}

/* Wait until after the sensors have been read */
DECLARE_HOOK(HOOK_SECOND, thermal_control, HOOK_PRIO_TEMP_SENSOR_DONE);

static void board_fan_pwm_stop(void)
{
	int fan;

	for (fan = 0; fan < fan_get_count(); fan++)
		fan_set_rpm_target(FAN_CH(fan), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_fan_pwm_stop, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_fan_pwm_stop, HOOK_PRIO_DEFAULT);


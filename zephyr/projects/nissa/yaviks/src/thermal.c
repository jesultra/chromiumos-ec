/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "include/console.h"
#include "fan.h"
#include "hooks.h"
#include "host_command.h"
#include "tablet_mode.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"
#include "util.h"


#define LV(X) DT_CHILD(DT_INST(0, cros_ec_fan_steps), level_##X)

#define TEMP_CPU TEMP_SENSOR_ID(DT_NODELABEL(temp_cpu))
#define TEMP_5V TEMP_SENSOR_ID(DT_NODELABEL(temp_5v_regulator))
#define TEMP_CHARGER TEMP_SENSOR_ID(DT_NODELABEL(temp_charger))

struct fan_step {
	/*
	 * Sensor 1~3 trigger point, set -1 if we're not using this
	 * sensor to determine fan speed.
	 */
	int8_t on[TEMP_SENSOR_COUNT];
	/*
	 * Sensor 1~3 trigger point, set -1 if we're not using this
	 * sensor to determine fan speed.
	 */
	int8_t off[TEMP_SENSOR_COUNT];
	/* Fan rpm */
	uint16_t rpm[FAN_CH_COUNT];
};

static struct fan_step fan_step_table[FAN_STEPS_COUNT];


static void board_fansteps_init(void)
{
	
	fan_step_table[0].on[0] = DT_PROP_BY_IDX(LV(0), temp_on, 0);
	fan_step_table[0].off[0] = DT_PROP_BY_IDX(LV(0), temp_off, 0);
	fan_step_table[0].on[1] = DT_PROP_BY_IDX(LV(0), temp_on, 1);
	fan_step_table[0].off[1] = DT_PROP_BY_IDX(LV(0), temp_off, 1);
	fan_step_table[0].on[2] = DT_PROP_BY_IDX(LV(0), temp_on, 2);
	fan_step_table[0].off[2] = DT_PROP_BY_IDX(LV(0), temp_off, 2);
	fan_step_table[0].rpm[0] = DT_PROP_BY_IDX(LV(0), rpm_target, 0);
	
	fan_step_table[1].on[0] = DT_PROP_BY_IDX(LV(1), temp_on, 0);
	fan_step_table[1].off[0] = DT_PROP_BY_IDX(LV(1), temp_off, 0);
	fan_step_table[1].on[1] = DT_PROP_BY_IDX(LV(1), temp_on, 1);
	fan_step_table[1].off[1] = DT_PROP_BY_IDX(LV(1), temp_off, 1);
	fan_step_table[1].on[2] = DT_PROP_BY_IDX(LV(1), temp_on, 2);
	fan_step_table[1].off[2] = DT_PROP_BY_IDX(LV(1), temp_off, 2);
	fan_step_table[1].rpm[0] = DT_PROP_BY_IDX(LV(1), rpm_target, 0);
	
	fan_step_table[2].on[0] = DT_PROP_BY_IDX(LV(2), temp_on, 0);
	fan_step_table[2].off[0] = DT_PROP_BY_IDX(LV(2), temp_off, 0);
	fan_step_table[2].on[1] = DT_PROP_BY_IDX(LV(2), temp_on, 1);
	fan_step_table[2].off[1] = DT_PROP_BY_IDX(LV(2), temp_off, 1);
	fan_step_table[2].on[2] = DT_PROP_BY_IDX(LV(2), temp_on, 2);
	fan_step_table[2].off[2] = DT_PROP_BY_IDX(LV(2), temp_off, 2);
	fan_step_table[2].rpm[0] = DT_PROP_BY_IDX(LV(2), rpm_target, 0);
	
	fan_step_table[3].on[0] = DT_PROP_BY_IDX(LV(3), temp_on, 0);
	fan_step_table[3].off[0] = DT_PROP_BY_IDX(LV(3), temp_off, 0);
	fan_step_table[3].on[1] = DT_PROP_BY_IDX(LV(3), temp_on, 1);
	fan_step_table[3].off[1] = DT_PROP_BY_IDX(LV(3), temp_off, 1);
	fan_step_table[3].on[2] = DT_PROP_BY_IDX(LV(3), temp_on, 2);
	fan_step_table[3].off[2] = DT_PROP_BY_IDX(LV(3), temp_off, 2);
	fan_step_table[3].rpm[0] = DT_PROP_BY_IDX(LV(3), rpm_target, 0);
	
	fan_step_table[4].on[0] = DT_PROP_BY_IDX(LV(4), temp_on, 0);
	fan_step_table[4].off[0] = DT_PROP_BY_IDX(LV(4), temp_off, 0);
	fan_step_table[4].on[1] = DT_PROP_BY_IDX(LV(4), temp_on, 1);
	fan_step_table[4].off[1] = DT_PROP_BY_IDX(LV(4), temp_off, 1);
	fan_step_table[4].on[2] = DT_PROP_BY_IDX(LV(4), temp_on, 2);
	fan_step_table[4].off[2] = DT_PROP_BY_IDX(LV(4), temp_off, 2);
	fan_step_table[4].rpm[0] = DT_PROP_BY_IDX(LV(4), rpm_target, 0);
	
	fan_step_table[5].on[0] = DT_PROP_BY_IDX(LV(5), temp_on, 0);
	fan_step_table[5].off[0] = DT_PROP_BY_IDX(LV(5), temp_off, 0);
	fan_step_table[5].on[1] = DT_PROP_BY_IDX(LV(5), temp_on, 1);
	fan_step_table[5].off[1] = DT_PROP_BY_IDX(LV(5), temp_off, 1);
	fan_step_table[5].on[2] = DT_PROP_BY_IDX(LV(5), temp_on, 2);
	fan_step_table[5].off[2] = DT_PROP_BY_IDX(LV(5), temp_off, 2);
	fan_step_table[5].rpm[0] = DT_PROP_BY_IDX(LV(5), rpm_target, 0);
	
	fan_step_table[6].on[0] = DT_PROP_BY_IDX(LV(6), temp_on, 0);
	fan_step_table[6].off[0] = DT_PROP_BY_IDX(LV(6), temp_off, 0);
	fan_step_table[6].on[1] = DT_PROP_BY_IDX(LV(6), temp_on, 1);
	fan_step_table[6].off[1] = DT_PROP_BY_IDX(LV(6), temp_off, 1);
	fan_step_table[6].on[2] = DT_PROP_BY_IDX(LV(6), temp_on, 2);
	fan_step_table[6].off[2] = DT_PROP_BY_IDX(LV(6), temp_off, 2);
	fan_step_table[6].rpm[0] = DT_PROP_BY_IDX(LV(6), rpm_target, 0);

}
DECLARE_HOOK(HOOK_INIT, board_fansteps_init, HOOK_PRIO_DEFAULT);


int fan_table_to_rpm(int fan, int *temp)
{
	/* current fan level */
	static int current_level;
	/* previous sensor temperature */
	static int prev_tmp[TEMP_SENSOR_COUNT];
	int i;
	int new_rpm = 0;
	/*
	 * Compare the current and previous temperature, we have
	 * the three paths :
	 *  1. decreasing path. (check the release point)
	 *  2. increasing path. (check the trigger point)
	 *  3. invariant path. (return the current RPM)
	 */
	if (temp[TEMP_CPU] < prev_tmp[TEMP_CPU] ||
	    temp[TEMP_5V] < prev_tmp[TEMP_5V] ||
	    temp[TEMP_CHARGER] < prev_tmp[TEMP_CHARGER]) {
		for (i = current_level; i > 0; i--) {
			if (temp[TEMP_CPU] < fan_step_table[i].off[TEMP_CPU] &&
			    temp[TEMP_5V] < fan_step_table[i].off[TEMP_5V] &&
			    temp[TEMP_CHARGER] < fan_step_table[i].off[TEMP_CHARGER]) {
				current_level = i - 1;
			} else
				break;
		}
	} else if (temp[TEMP_CPU] > prev_tmp[TEMP_CPU] ||
		   temp[TEMP_5V] > prev_tmp[TEMP_5V] ||
		    temp[TEMP_CHARGER] > prev_tmp[TEMP_CHARGER]) {
		for (i = current_level; i < FAN_STEPS_COUNT; i++) {
			if (temp[TEMP_CPU] > fan_step_table[i].on[TEMP_CPU] ||
			    (temp[TEMP_5V] > fan_step_table[i].on[TEMP_5V] &&
				  temp[TEMP_CHARGER] > fan_step_table[i].on[TEMP_CHARGER])) {
				current_level = i + 1;
			} else
				break;
		}
	}
	if (current_level < 0)
		current_level = 0;
	if (current_level >= FAN_STEPS_COUNT)
		current_level = FAN_STEPS_COUNT - 1;
	for (i = 0; i < TEMP_SENSOR_COUNT; ++i)
		prev_tmp[i] = temp[i];
	new_rpm = fan_step_table[current_level].rpm[fan];
	
	ccprintf("cpu:%d, 5v:%d, charger:%d, rmp:%d\n", temp[TEMP_CPU], temp[TEMP_5V], temp[TEMP_CHARGER], new_rpm);
	
	return new_rpm;
}

void board_override_fan_control(int fan, int *temp)
{
	if (chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND)) {
		fan_set_rpm_mode(fan, 1);
		fan_set_rpm_target(fan, fan_table_to_rpm(fan, temp));
	}
}
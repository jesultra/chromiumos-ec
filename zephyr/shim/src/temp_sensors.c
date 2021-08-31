/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "temp_sensor.h"
#include "temp_sensor/temp_sensor.h"
#include "adc.h"
#include "temp_sensor/thermistor.h"
// .read = DT_ENUM_TOKEN(node_id, get_temp_func), \
// Set this to being my function in shim/thermistor.c

#if DT_NODE_EXISTS(DT_PATH(named_temp_sensors))
static int thermistor_get_temp(
	const struct temp_sensor_t *sensor,
			       int *temp_ptr)
{
	int mv;
	mv = adc_read_channel(sensor->idx);
	if (mv < 0)
		return EC_ERROR_UNKNOWN;
	return 0;
}
#endif /* named_temp_sensors */


#define GET_THERMISTOR(node_id) & (struct thermistor_info) {.scaling_factor = DT_PROP(node_id, scaling_factor), .num_pairs = DT_PROP(node_id, num_pairs), .data = NULL}


#define TEMP_THERMISTOR(node_id)                               \
	[ZSHIM_TEMP_SENSOR_ID(node_id)] = {                    \
		.name = DT_LABEL(node_id),                     \
		.read = &thermistor_get_temp,                  \
		.idx = ZSHIM_ADC_ID(DT_PHANDLE(node_id, adc)), \
		.type = TEMP_SENSOR_TYPE_BOARD,                \
		.thermistor = GET_THERMISTOR(DT_PHANDLE(node_id, thermistor)), \
	},


#if DT_NODE_EXISTS(DT_PATH(named_temp_sensors))
const struct temp_sensor_t temp_sensors[] = {
	DT_FOREACH_CHILD(DT_PATH(named_temp_sensors), TEMP_THERMISTOR)
};
#endif /* named_temp_sensors */

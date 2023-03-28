/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpc_sensor.h"

#include <stddef.h>

#include <include/fpsensor.h>
#include <include/fpsensor_state.h>

#if !defined(CONFIG_FP_SENSOR_FPC1025) &&     \
	!defined(CONFIG_FP_SENSOR_FPC1035) && \
	!defined(CONFIG_FP_SENSOR_FPC1145)
#error "Sensor type not defined!"
#endif

int fpc_sensor_maintenance(uint16_t *error_state)
{
#if !defined(HAVE_PRIVATE)
	return EC_ERROR_INVAL;
#else
	int rv;
	fp_sensor_info_t sensor_info;
	timestamp_t start = get_time();

	if (error_state == NULL)
		return EC_ERROR_INVAL;

	rv = fp_sensor_maintenance(fp_buffer, &sensor_info);
	CPRINTS("Maintenance took %d ms", time_since32(start) / MSEC);

	if (rv != 0) {
		/*
		 * Failure can occur if any of the fingerprint detection zones
		 * are covered (i.e., finger is on sensor).
		 */
		CPRINTS("Failed to run maintenance: %d", rv);
		return EC_ERROR_HW_INTERNAL;
	}

	*error_state |= FP_ERROR_DEAD_PIXELS(sensor_info.num_defective_pixels);
	CPRINTS("num_defective_pixels: %d", sensor_info.num_defective_pixels);

	return EC_SUCCESS;
#endif
}

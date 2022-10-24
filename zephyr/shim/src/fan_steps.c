/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_fan_steps

LOG_MODULE_REGISTER(fan_shim, LOG_LEVEL_ERR);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of cros-ec,fan_steps should be defined.");

#define FAN_STEPS(node_id, i)                                                   \
	const struct fan_temps node_id##_temps = {                               \
		.temp_on = DT_PROP_BY_IDX(node_id, temp_on, i),          \
		.temps_off = DT_PROP_BY_IDX(node_id, temp_off, i),                                 \
	};                                                                     \
	const struct fan_rpm node_id##_rpm = {                                 \
		.rpm_target = DT_PROP(node_id, rpm_target),              \
	};

#define FAN_INST(node_id)                \
	[node_id] = {                    \
		.temps = &node_id##_temps, \
		.rpm = &node_id##_rpm,   \
	},

DT_INST_FOREACH_CHILD(0, FAN_STEPS)



/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * CREATE_MOTION_SENSOR is the macro to create an entry in
 * motion_sensors array. The macro gets node id of sensor description
 * in device tree and creates the corresponding motion_sense_t entry for
 * motion_sensors array.
 *
 * Here, we call CREATE_MOTION_SENSOR for each BMA255 instance
 * (compatible = "cros-ec,drv-bmi255")
 * in device tree to create motion_sensor_t entry for the instance.
 */

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT			cros_ec_drv_bma255

#define MOTION_SENSOR_BMA255(n)						\
		CREATE_MOTION_SENSOR(DT_PARENT(DT_DRV_INST(n)),		\
			MOTIONSENSE_CHIP_BMA255,			\
			MOTIONSENSE_TYPE_ACCEL,				\
			SENSOR_I2C_SPI_ADDR_FLAGS(DT_DRV_INST(n)),	\
			bma2x2_accel_drv,				\
			BMA255_ACCEL_MIN_FREQ,				\
			BMA255_ACCEL_MAX_FREQ)				\

/* Create a motion_sense_t entry for each BMA255 instance in device tree */
DT_INST_FOREACH_STATUS_OKAY(MOTION_SENSOR_BMA255)

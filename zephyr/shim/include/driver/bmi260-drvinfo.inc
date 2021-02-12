/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * CREATE_MOTION_SENSOR is the macro to create an entry in
 * motion_sensors array. The macro gets node id of sensor description
 * in device tree and creates the corresponding motion_sense_t entry for
 * motion_sensors array.
 */

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT			cros_ec_drv_bmi260_accel

/*
 * Here, we call CREATE_MOTION_SENSOR for each BMI260_accel
 * instance (compatible = "cros-ec,drv-bmi260-accel")
 * in device tree to create motion_sensor_t entry for the instance.
 */
#define MOTION_SENSOR_BMI260_ACCEL(n)					\
		CREATE_MOTION_SENSOR(DT_PARENT(DT_DRV_INST(n)),		\
			MOTIONSENSE_CHIP_BMI260,			\
			MOTIONSENSE_TYPE_ACCEL,				\
			SENSOR_I2C_SPI_ADDR_FLAGS(DT_DRV_INST(n)),	\
			bmi260_drv,					\
			BMI_ACCEL_MIN_FREQ,				\
			BMI_ACCEL_MAX_FREQ)

/* Create motion sensor for each BMI260 accel instance */
DT_INST_FOREACH_STATUS_OKAY(MOTION_SENSOR_BMI260_ACCEL)

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT			cros_ec_drv_bmi260_gyro

/*
 * Here, we call CREATE_MOTION_SENSOR for each BMI260_gyro
 * instance (compatible = "cros-ec,drv-bmi260-gyro")
 * in device tree to create motion_sensor_t entry for the instance.
 */
#define MOTION_SENSOR_BMI260_GYRO(n)					\
		CREATE_MOTION_SENSOR(DT_PARENT(DT_DRV_INST(n)),		\
			MOTIONSENSE_CHIP_BMI260,			\
			MOTIONSENSE_TYPE_GYRO,				\
			SENSOR_I2C_SPI_ADDR_FLAGS(DT_DRV_INST(n)),	\
			bmi260_drv,					\
			BMI_GYRO_MIN_FREQ,				\
			BMI_GYRO_MAX_FREQ)

/* Create motion sensor for each BMI260 gyro instance */
DT_INST_FOREACH_STATUS_OKAY(MOTION_SENSOR_BMI260_GYRO)

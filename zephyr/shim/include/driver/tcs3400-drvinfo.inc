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
#define DT_DRV_COMPAT			cros_ec_drv_tcs3400_clear

/*
 * Here, we call CREATE_MOTION_SENSOR for each TCS3400_clear
 * instance (compatible = "cros-ec,drv-tcs3400-clear")
 * in device tree to create motion_sensor_t entry for the instance.
 */
#define MOTION_SENSOR_TCS3400_CLEAR(n)					\
		CREATE_MOTION_SENSOR(DT_PARENT(DT_DRV_INST(n)),		\
			MOTIONSENSE_CHIP_TCS3400,			\
			MOTIONSENSE_TYPE_LIGHT,				\
			SENSOR_I2C_SPI_ADDR_FLAGS(DT_DRV_INST(n)),	\
			tcs3400_drv,					\
			TCS3400_LIGHT_MIN_FREQ,				\
			TCS3400_LIGHT_MAX_FREQ)


/* create motion sensor for each TCS3400_CLEAR instance */
DT_INST_FOREACH_STATUS_OKAY(MOTION_SENSOR_TCS3400_CLEAR)

/*
 * Create motion_sensor_t for each TCS3400_RGB instance with
 * CREATE_MOTION_SENSOR
 */
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT			cros_ec_drv_tcs3400_rgb

/*
 * Here, we call CREATE_MOTION_SENSOR for each TCS3400_rgb
 * instance (compatible = "cros-ec,drv-tcs3400-rgb")
 * in device tree to create motion_sensor_t entry for the instance.
 */
#define MOTION_SENSOR_TCS3400_RGB(n)					\
		CREATE_MOTION_SENSOR(DT_PARENT(DT_DRV_INST(n)),		\
			MOTIONSENSE_CHIP_TCS3400,			\
			MOTIONSENSE_TYPE_LIGHT_RGB,			\
			SENSOR_I2C_SPI_ADDR_FLAGS(DT_DRV_INST(n)),	\
                        tcs3400_rgb_drv, 0, 0)

/* create motion sensor for each TCS3400_RGB instance */
DT_INST_FOREACH_STATUS_OKAY(MOTION_SENSOR_TCS3400_RGB)

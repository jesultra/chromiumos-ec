/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * A driver should create driver/<chip>-drvinfo.inc to create an entry in
 * "motion_sensors" array that is used by motion sense task.
 *
 * This file includes the .inc file and is used by motionsense_sensrs.c to
 * create the mostion_sensors array.
 */

#ifndef __CROS_EC_SENSOR_DRV_LIST_H
#define __CROS_EC_SENSOR_DRV_LIST_H

/* supported sensor driver list */
#ifdef CONFIG_ACCEL_BMA255
#include "driver/bma255-drvinfo.inc"
#endif
#ifdef CONFIG_ACCELGYRO_BMI260
#include "driver/bmi260-drvinfo.inc"
#endif
#ifdef CONFIG_ALS_TCS3400
#include "driver/tcs3400-drvinfo.inc"
#endif

#endif /* __CROS_EC_SENSOR_DRV_LIST_H */

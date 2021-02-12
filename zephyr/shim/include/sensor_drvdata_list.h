/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


/*
 * A driver which requires own driver data should create the corresponding
 * driver/drvdata-<chip>.inc. The file has how to crate the driver data.
 * the driver data can be shared among the entries in motion_sensors array.
 *
 * This file includes the .inc file and required public headers
 * for the driver.
 */

#ifndef __CROS_EC_SENSOR_DRVDATA_LIST_H
#define __CROS_EC_SENSOR_DRVDATA_LIST_H

/* supported sensor driver data list */
#ifdef CONFIG_ACCEL_BMA255
#include "driver/accel_bma2x2_public.h"
#include "driver/drvdata-bma255.inc"
#endif
#ifdef CONFIG_ACCELGYRO_BMI260
#include "driver/accelgyro_bmi_common_public.h"
#include "driver/accelgyro_bmi260_public.h"
#include "driver/drvdata-bmi260.inc"
#endif
#ifdef CONFIG_ALS_TCS3400
#include "driver/als_tcs3400_public.h"
#include "driver/drvdata-tcs3400.inc"
#endif

#endif /* __CROS_EC_SENSOR_DRVDATA_LIST_H */

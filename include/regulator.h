/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_REGULATOR_H
#define __CROS_EC_REGULATOR_H

#include "common.h"

/**
 * TODO(pihsun): Documentation
 */
int board_regulator_get_info(uint32_t index, char *name,
			     uint32_t *voltage_count, uint32_t *voltages_uV);
int board_regulator_enable(uint32_t index, uint8_t enable);
int board_regulator_is_enabled(uint32_t index, uint8_t *enabled);
int board_regulator_set_voltage(uint32_t index, uint32_t min_uV,
				uint32_t max_uV);
int board_regulator_get_voltage(uint32_t index, uint32_t *voltage_uV);

#endif /* !defined(__CROS_EC_REGULATOR_H) */

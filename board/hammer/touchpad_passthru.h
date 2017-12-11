/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_HAMMER_TOUCHPAD_PASSTHRU
#define __CROS_HAMMER_TOUCHPAD_PASSTHRU

#include <stdint.h>

#define ROWS		24
#define COLS		14

#if 1
#define FRAME_SIZE	(ROWS * COLS)
#else
#define FRAME_SIZE	(13)
#endif

struct touchpad_passthru_report {
	uint16_t magic;
	uint8_t frame[FRAME_SIZE];
} __packed;

void touchpad_passthru_generate_event(void);

#endif /* __CROS_HAMMER_TOUCHPAD_PASSTHRU */

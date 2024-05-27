/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "gpio.h"
#include "touchpad.h"

#if defined(CONFIG_USB_DC_HID_KEYBOARD) || defined(CONFIG_USBD_HID_KEYBOARD)
static const struct ec_response_keybd_config bland_kb = {
#ifdef CONFIG_USB_DC_HID_KEYBOARD
	.num_top_row_keys = CONFIG_USB_DC_KEYBOARD_NUM_TOP_ROW_KEYS,
#else
	.num_top_row_keys = CONFIG_USBD_KEYBOARD_NUM_TOP_ROW_KEYS,
#endif
	.action_keys = {
		TK_BACK,
		TK_REFRESH,
		TK_FULLSCREEN,
		TK_OVERVIEW,
		TK_BRIGHTNESS_DOWN,
		TK_BRIGHTNESS_UP,
		TK_MICMUTE,
		TK_VOL_MUTE,
		TK_VOL_DOWN,
		TK_VOL_UP,
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return &bland_kb;
}
#endif
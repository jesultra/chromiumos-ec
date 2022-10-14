/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Tune the MP2940 IMVP9.1 parameters for brya */
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "hooks.h"
#include "mp2940.h"
#include "util.h"
#include "cbi.h"

const static struct mp2940_reg_val rail_a_aux[] = {
	{ 0x25,	0x0092 },
	{ 0x38,	0x8C67 },
	{ 0xc0,	0x90E0 },
	{ 0xe6, 0x0030 },	
	{ 0xee,	0x192C },
	{ 0xf6,	0xBDD0 },
};
const static struct mp2940_reg_val_byte rail_b_aux[] = {
	{ 0xc1,	0x01 },
};
static int mp2940_patch(int argc, const char **argv)
{
	int status = EC_SUCCESS;
	ccprintf("MP2940 PATCH:\n 1. Power OFF the device\n 2. press power");
	ccprintf("button to power MP2940 ON\n 3. Use EC command `imvp9`\n");
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;
	ccprintf("%s: attempting to tune PMIC\n", __func__);
	if (!strcasecmp(argv[1], "aux")) {
		status = mp2940_tune(rail_a_aux,
				     ARRAY_SIZE(rail_a_aux),
				     rail_b_aux,
				     ARRAY_SIZE(rail_b_aux));
	} else {
		ccprintf("ERROR: param1 has to be one of aux\n");
		return EC_ERROR_PARAM1;
	}
	if (status != EC_SUCCESS) {
		ccprintf("%s: could not update all settings\n", __func__);
		return status;
	}
	ccprintf("%s: IMVP9 update done\n", __func__);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(imvp_aux, mp2940_patch,
			"aux",
			"Tune imvp9.1 for differtent scenarios");

static void mp2940_on_startup(void)
{
	static int chip_updated;
	uint32_t sku_id;
	int status = !EC_SUCCESS;

	if (chip_updated)
		return;

	sku_id = get_sku_id();

	if (sku_id == 0x30001 || sku_id == 0x30002) {
		status = mp2940_tune(rail_a_aux,
				     ARRAY_SIZE(rail_a_aux),
				     rail_b_aux,
				     ARRAY_SIZE(rail_b_aux));
	} else {
		ccprintf("SKU: 0x%08x not be 15w/28w setting", sku_id);
	}

	chip_updated = 1;

	if (status != EC_SUCCESS) {
		ccprintf("%s: could not update all settings\n", __func__);
	} else
	ccprintf("%s: IMVP9 aux update done\n", __func__);
}

DECLARE_HOOK(HOOK_CHIPSET_STARTUP, mp2940_on_startup,
	     HOOK_PRIO_FIRST);

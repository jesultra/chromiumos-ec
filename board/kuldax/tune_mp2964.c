/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Tune the MP2964 IMVP9.1 parameters for brya */
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "hooks.h"
#include "mp2964.h"
#include "util.h"
#include "cbi.h"
const static struct mp2964_reg_val rail_a[] = {
	{ MP2964_MFR_ALT_SET,  0xe081 },	/* ALERT_DELAY = 200ns */
};
const static struct mp2964_reg_val rail_b[] = {
	{ MP2964_MFR_ALT_SET,  0xe081 },	/* ALERT_DELAY = 200ns */
};
const static struct mp2964_reg_val rail_a_28wES_to_28wQS[] = {
	{ MP2964_MFR_VOUT_TRIM,		0xF000 },
	{ MP2964_MFR_IMON_SNS_OFFS,	0x03A0 },
	{ MP2964_VIN_ON,			0x002C},
	{ MP2964_VIN_OFF,			0x0028},
	{ MP2964_MFR_TRANS_FAST,	0x2BC3 },
	{ MP2964_MFR_CONFIG2,		0x0115},	
	{ MP2964_MFR_SLOPE_SR_DCM,	0x0002 },
	{ MP2964_MFR_PWR_INDUCTOR_GAIN,	0x001E },
	{ MP2964_MFR_OCP_OVP_DAC_LIMIT, 0x8DB0 },
	{ MP2964_MFR_SVID_CFG,		0x717D },
	{ MP2964_VENDOR_ID_PRODUCT_ID,	0X2564 },
	{ MP2964_PRODUCT_DATA_CODE,	0x3602 },
	{ MP2964_LOT_CODE_VR,		0x0003 },
	{ MP2964_SR_FAST_TOLERANCE,	0x300A },
	{ MP2964_MFR_PSI_TRIM4,		0x10E4 },
	{ MP2964_MFR_PSI_TRIM1,		0x1CA5 },
	{ MP2964_MFR_VIN_OV_UV_LIMIT,	0x2AB8 },
	{ MP2964_MFR_IMON_SVID1,	0x0093 },
	{ MP2964_MFR_IMON_SVID3,	0x0293 },
	{ MP2964_MFR_IMON_SVID5,	0x01A5 },
	{ MP2964_MFR_AUXIMON_SVID,	0x005B },
};
const static struct mp2964_reg_val rail_b_28wES_to_28wQS[] = {
	{ MP2964_MFR_VOUT_TRIM,		0x0EFF },
	{ MP2964_MFR_IMON_SNS_OFFS,	0x0359 },
	{ MP2964_MFR_TRANS_FAST,	0x2BC3 },
	{ MP2964_MFR_CONFIG2,		0x00C0 },
	{ MP2964_MFR_PWR_INDUCTOR_GAIN,	0x001E },
	{ MP2964_MFR_OCP_OVP_DAC_LIMIT,	0x46B0 },
	{ MP2964_MFR_OCP_SET,		0x0CA3 },
	{ MP2964_SR_FAST_TOLERANCE,	0x300A },
};
const static struct mp2964_reg_val rail_a_15wES_to_15wQS[] = {
	{ MP2964_MFR_VOUT_TRIM,		0x0FF0 },
	{ MP2964_MFR_IMON_SNS_OFFS,	0x0383 },
	{ MP2964_VIN_ON,			0x002C },
	{ MP2964_VIN_OFF,			0x0028 },	
	{ MP2964_MFR_TRANS_FAST,	0x2BC3 },
	{ MP2964_MFR_CONFIG2,		0x0055 },
	{ MP2964_MFR_PWR_INDUCTOR_GAIN,	0x001E },
	{ MP2964_MFR_SVID_CFG,		0x717D },
	{ MP2964_PRODUCT_DATA_CODE,	0x3601 },
	{ MP2964_LOT_CODE_VR,		0x0003 },
	{ MP2964_SR_FAST_TOLERANCE,	0x300A },
	{ MP2964_MFR_PSI_TRIM4,		0x1104 },
	{ MP2964_MFR_VIN_OV_UV_LIMIT,	0x2AB8 },
	{ MP2964_MFR_IMON_SVID2,	0x029E },
	{ MP2964_MFR_IMON_SVID6,	0x00B7 },
};
const static struct mp2964_reg_val rail_b_15wES_to_15wQS[] = {
	{ MP2964_MFR_CONFIG2,		0x00C0 },
	{ MP2964_MFR_PWR_INDUCTOR_GAIN,	0x001E },
	{ MP2964_SR_FAST_TOLERANCE,		0x300A },
	{ MP2964_MFR_PSI_TRIM4,		0x1505 },
};
static int mp2964_patch(int argc, const char **argv)
{
	int status = EC_SUCCESS;
	ccprintf("MP2964 PATCH:\n 1. Power OFF the device\n 2. press power");
	ccprintf("button to power MP2964 ON\n 3. Use EC command `imvp9`\n");
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;
	ccprintf("%s: attempting to tune PMIC\n", __func__);
	if (!strcasecmp(argv[1], "P1")) {
		status = mp2964_tune(rail_a, ARRAY_SIZE(rail_a),
				     rail_b, ARRAY_SIZE(rail_b));
	} else if (!strcasecmp(argv[1], "28to15")) {
		status = mp2964_tune(rail_a_28wES_to_28wQS,
				     ARRAY_SIZE(rail_a_28wES_to_28wQS),
				     rail_b_28wES_to_28wQS,
				     ARRAY_SIZE(rail_b_28wES_to_28wQS));
	} else if (!strcasecmp(argv[1], "15to15")) {
		status = mp2964_tune(rail_a_15wES_to_15wQS,
				     ARRAY_SIZE(rail_a_15wES_to_15wQS),
				     rail_b_15wES_to_15wQS,
				     ARRAY_SIZE(rail_b_15wES_to_15wQS));
	} else {
		ccprintf("ERROR: param1 has to be one of P1|15to15|28to15\n");
		return EC_ERROR_PARAM1;
	}
	if (status != EC_SUCCESS) {
		ccprintf("%s: could not update all settings\n", __func__);
		return status;
	}
	ccprintf("%s: IMVP9 update done\n", __func__);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(imvp9, mp2964_patch,
			"P1|15to15|28to28",
			"Tune imvp9.1 for differtent scenarios");

static void mp2964_on_startup(void)
{
	static int chip_updated;
	uint32_t sku_id;
	int status = !EC_SUCCESS;

	if (chip_updated)
		return;

	sku_id = get_sku_id();

	if (sku_id == 0x30001) {
	/*  15w to 15w update*/
		status = mp2964_tune(rail_a_15wES_to_15wQS,
				     ARRAY_SIZE(rail_a_15wES_to_15wQS),
				     rail_b_15wES_to_15wQS,
				     ARRAY_SIZE(rail_b_15wES_to_15wQS));
	} else if (sku_id == 0x30002) {
	/* 28w to 28w update*/
		status = mp2964_tune(rail_a_28wES_to_28wQS,
				     ARRAY_SIZE(rail_a_28wES_to_28wQS),
				     rail_b_28wES_to_28wQS,
				     ARRAY_SIZE(rail_b_28wES_to_28wQS));
	} else {
		ccprintf("SKU: 0x%08x not be 15w/28w setting", sku_id);
	}

	chip_updated = 1;

	if (status != EC_SUCCESS) {
		ccprintf("%s: could not update all settings\n", __func__);
	} else
	ccprintf("%s: IMVP9 update done\n", __func__);
}

DECLARE_HOOK(HOOK_CHIPSET_STARTUP, mp2964_on_startup,
	     HOOK_PRIO_FIRST);
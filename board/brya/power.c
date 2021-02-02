/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "timer.h"

#include "power/icelake.h"
#include "power/intel_x86.h"

/*
 * Brya board specific delays.
 */

#define VCCST_PWRGD_DELAY_MS	2
#define PCH_PWROK_DELAY_MS	2
#define SYS_PWROK_DELAY_MS	45

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

#ifdef CONFIG_BRINGUP
#define GPIO_SET_LEVEL(signal, value) \
	gpio_set_level_verbose(CC_CHIPSET, signal, value)
#else
#define GPIO_SET_LEVEL(signal, value) \
	gpio_set_level(signal, value)
#endif

/*
 * Brya uses a power sequencer chip to drive DSW_PWROK to the AP.
 */
__override void intel_x86_dsw_pwrok_pass_thru(void)
{
}

static void ap_off(void)
{
	GPIO_SET_LEVEL(GPIO_VCCST_PWRGD_OD, 0);
	GPIO_SET_LEVEL(GPIO_PCH_PWROK, 0);
	GPIO_SET_LEVEL(GPIO_EC_PCH_SYS_PWROK, 0);
}

/*
 * We have asserted VCCST_PWRGO_OD, now wait for the IMVP9.1
 * to assert IMVP9_VRRDY_OD.
 *
 * Returns state of VRRDY.
 */

static int wait_for_vrrdy(void)
{
	int timeout_ms = 50;
	int vrrdy;

	for (; timeout_ms > 0; --timeout_ms) {
		vrrdy = gpio_get_level(GPIO_IMVP9_VRRDY_OD);
		if (vrrdy != 0)
			return 1;
		msleep(1);
	}
	return 0;
}

/*
 * The relationship between these signals is described in
 * Intel PDG #627205 rev. 0.81.
 *
 * tCPU16: >= 0
 *	VCCST_PWRGD to PCH_PWROK
 * tPLT05: >= 0
 *	SYS_ALL_PWRGD to SYS_PWROK
 *	PCH_PWROK to SYS_PWROK
 */

__override void all_sys_pwrgd_pass_thru(void)
{
	int sys_pg;
	int vccst_pg;
	int pch_pok;
	int sys_pok;

	sys_pg = gpio_get_level(GPIO_SEQ_EC_ALL_SYS_PG);
	CPRINTS("SEQ_EC_ALL_SYS_PG is %d", sys_pg);
	if (sys_pg == 0) {
		ap_off();
		return;
	}

	/* SEQ_EC_ALL_SYS_PG is asserted, enable VCCST_PWRGD_OD. */

	vccst_pg = gpio_get_level(GPIO_VCCST_PWRGD_OD);
	if (vccst_pg == 0) {
		msleep(VCCST_PWRGD_DELAY_MS);
		GPIO_SET_LEVEL(GPIO_VCCST_PWRGD_OD, 1);
	}

	/* Enable PCH_PWROK, gated by VRRDY. */

	pch_pok = gpio_get_level(GPIO_PCH_PWROK);
	if (pch_pok == 0) {
		if (wait_for_vrrdy() == 0) {
			CPRINTS("Timed out waiting for VRRDY, "
				"shutting AP off!");
			ap_off();
			return;
		}
		msleep(PCH_PWROK_DELAY_MS);
		GPIO_SET_LEVEL(GPIO_PCH_PWROK, 1);
	}

	/* Enable PCH_SYS_PWROK. */

	sys_pok = gpio_get_level(GPIO_EC_PCH_SYS_PWROK);
	if (sys_pok == 0) {
		msleep(SYS_PWROK_DELAY_MS);
		/* Check if we lost power while waiting. */
		sys_pg = gpio_get_level(GPIO_SEQ_EC_ALL_SYS_PG);
		if (sys_pg == 0) {
			CPRINTS("SEQ_EC_ALL_SYS_PG deasserted, "
				"shutting AP off!");
			ap_off();
			return;
		}
		GPIO_SET_LEVEL(GPIO_EC_PCH_SYS_PWROK, 1);
		/* PCH will now release PLT_RST */
	}
}

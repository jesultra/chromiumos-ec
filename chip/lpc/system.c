/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : hardware specific implementation */

//#include "bkpdata.h"
//#include "clock.h"
//#include "console.h"
//#include "cpu.h"
//#include "cros_version.h"
//#include "flash.h"
//#include "gpio_chip.h"
//#include "hooks.h"
//#include "host_command.h"
//#include "panic.h"
//#include "registers.h"
#include "system.h"
//#include "task.h"
//#include "util.h"
//#include "watchdog.h"

void __no_hibernate(uint32_t seconds, uint32_t microseconds)
{
}

void __enter_hibernate(uint32_t seconds, uint32_t microseconds)
	__attribute__((weak, alias("__no_hibernate")));

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
#ifdef CONFIG_HOSTCMD_PD
	/* Inform the PD MCU that we are going to hibernate. */
	host_command_pd_request_hibernate();
	/* Wait to ensure exchange with PD before hibernating. */
	crec_msleep(100);
#endif

	/* Flush console before hibernating */
	cflush();

	if (board_hibernate)
		board_hibernate();

	/* chip specific standby mode */
	__enter_hibernate(seconds, microseconds);
}

uint32_t chip_read_reset_flags(void)
{
	return 0;//bkpdata_read_reset_flags();
}

void chip_save_reset_flags(uint32_t flags)
{
	//bkpdata_write_reset_flags(flags);
}

void chip_pre_init(void)
{
}

void system_pre_init(void)
{
}

void system_reset(int flags)
{
	for (;;);
}

int system_set_scratchpad(uint32_t value)
{
	return EC_ERROR_INVAL;
}

int system_get_scratchpad(uint32_t *value)
{
	return EC_ERROR_INVAL;
}

const char *system_get_chip_vendor(void)
{
	return "stm";
}

const char *system_get_chip_name(void)
{
	return STRINGIFY(CHIP_VARIANT);
}

const char *system_get_chip_revision(void)
{
	return "";
}

int system_get_chip_unique_id(uint8_t **id)
{
	return 0;
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	return EC_ERROR_INVAL;
}

int system_set_bbram(enum system_bbram_idx idx, uint8_t value)
{
	return EC_ERROR_INVAL;
}

int system_is_reboot_warm(void)
{
	return false;
}



/*****************************************************************************
 *
 * Below functions should be moved into their own separate files.
 *
 */

void watchdog_init(void)
{
}

void watchdog_reload(void)
{
}

int crec_flash_pre_init(void)
{
	return 0;
}

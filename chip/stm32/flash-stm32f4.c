/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "common.h"
#include "flash.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"

/*****************************************************************************/
/* Physical layer APIs */
/*
 * 8 "erase" sectors : 16KB/16KB/16KB/16KB/64KB/128KB/128KB/128KB
 */
struct ec_flash_bank const flash_bank_array[] = {
	{
		.sector_nb = 4,
		.sector_size = SIZE_16KB,
		.sector_erase_size = SIZE_16KB
	},
	{
		.sector_nb = 1,
		.sector_size = SIZE_64KB,
		.sector_erase_size = SIZE_64KB,
	},
	{
		.sector_nb = (CONFIG_FLASH_SIZE - SIZE_128KB) / SIZE_128KB,
		.sector_size = SIZE_128KB,
		.sector_erase_size = SIZE_128KB,
	},
};

/*****************************************************************************/
/* Physical layer APIs */

int flash_physical_get_protect(int bank)
{
	return !(STM32_OPTB_WP & STM32_OPTB_nWRP(bank));
}

uint32_t flash_physical_get_protect_flags(void)
{
	uint32_t flags = 0;

	/* Read all-protected state from our shadow copy */
	if ((STM32_OPTB_WP & STM32_OPTB_nWRP_ALL) == 0)
		flags |= EC_FLASH_PROTECT_ALL_NOW;

	return flags;
}

uint32_t flash_physical_get_valid_flags(void)
{
	return EC_FLASH_PROTECT_RO_AT_BOOT |
	       EC_FLASH_PROTECT_RO_NOW |
	       EC_FLASH_PROTECT_ALL_NOW;
}

uint32_t flash_physical_get_writable_flags(uint32_t cur_flags)
{
	uint32_t ret = 0;

	/* If RO protection isn't enabled, its at-boot state can be changed. */
	if (!(cur_flags & EC_FLASH_PROTECT_RO_NOW))
		ret |= EC_FLASH_PROTECT_RO_AT_BOOT;

	/*
	 * If entire flash isn't protected at this boot, it can be enabled if
	 * the WP GPIO is asserted.
	 */
	if (!(cur_flags & EC_FLASH_PROTECT_ALL_NOW) &&
	    (cur_flags & EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_ALL_NOW;

	return ret;
}

int flash_physical_restore_state(void)
{
	return 0;
}

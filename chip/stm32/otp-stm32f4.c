/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* OTP implementation for STM32F411 */

#include "common.h"
#include "console.h"
#include "flash.h"
#include "otp.h"
#include "registers.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_FLASH, outstr)
#define CPRINTF(format, args...) cprintf(CC_FLASH, format, ## args)

/*
 * Read an OTP bank
 *
 * CONFIG_OTP_SIZE bytes are read.
 *
 * @param bank          Bank to read.
 * @param size	        Number of bytes to read.
 * @param data          Destination buffer for data.  Must be 32-bit aligned.
 */
int otp_read(uint8_t bank, int size, char *data)
{
	int i;
	uint32_t *payload = (uint32_t *)data;
	if (bank >= STM32_OTP_BLOCK_NB)
		return EC_ERROR_PARAM1;
	if (size >= STM32_OTP_BLOCK_SIZE)
		return EC_ERROR_PARAM2;
	if (size % sizeof(uint32_t) != 0)
		return EC_ERROR_PARAM2;
	for (i = 0; i < size/sizeof(uint32_t); i++)
		payload[i] = REG32(STM32_OTP_BLOCK_DATA(bank, i));
	return EC_SUCCESS;
}

/*
 * Write an OTP bank
 *
 * @param bank          Bank to write.
 * @param size	        Number of bytes to write.
 * @param data          Destination buffer for data.  Must be 32-bit aligned.
 */
int otp_write(uint8_t bank, int size, const char *data)
{
	if (bank >= STM32_OTP_BLOCK_NB)
		return EC_ERROR_PARAM1;
	if (size >= STM32_OTP_BLOCK_SIZE)
		return EC_ERROR_PARAM2;
	if (size % sizeof(uint32_t) != 0)
		return EC_ERROR_PARAM2;
	return flash_physical_write(STM32_OTP_BLOCK_DATA(bank, 0) -
			CONFIG_FLASH_BASE, size, data);
}

/*
 * Check if an OTP bank is protected.
 *
 * @param bank          Bank to protect.
 * @return non-zero if that bank is read only.
 */
int otp_get_protect(uint8_t bank)
{
	uint32_t lock;
	lock = REG32(STM32_OTP_LOCK(bank));
	return ((lock & STM32_OPT_LOCK_MASK(bank)) == 0);
}

/*
 * Set a particular OTP banck as read only.
 *
 * @param bank          Bank to protect.
 */
int otp_set_protect(uint8_t bank)
{
	int rv;
	uint32_t lock;
	if (otp_get_protect(bank))
		return EC_SUCCESS;

	rv = flash_physical_read(
		STM32_OTP_LOCK(bank) - CONFIG_FLASH_BASE,
		sizeof(uint32_t), (char *)&lock);
	if (rv)
		return rv;
	lock &= ~STM32_OPT_LOCK_MASK(bank)
	rv = flash_physical_write(
		STM32_OTP_LOCK(bank) - CONFIG_FLASH_BASE,
		sizeof(uint32_t), (char *)&lock);
	if (rv)
		return rv;
	return EC_SUCCESS;
}


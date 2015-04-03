/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
#define CONFIG_FLASH_BASE       0x08000000
#define CONFIG_FLASH_PHYSICAL_SIZE (512 * 1024)
#define CONFIG_FLASH_SIZE       CONFIG_FLASH_PHYSICAL_SIZE

/* 3 regison type: 16K, 64K and 128K */
#define SIZE_16KB (16 * 1024)
#define SIZE_64KB (64 * 1024)
#define SIZE_128KB (128 * 1024)
#define CONFIG_FLASH_REGION_TYPE 3
#define CONFIG_FLASH_MULTIPLE_REGION \
	(5 + (CONFIG_FLASH_SIZE - SIZE_128KB) / SIZE_128KB)

/* minimum write size for 3.3V. 1 for 1.8V */
#define CONFIG_FLASH_WRITE_SIZE_1800 1
#define CONFIG_FLASH_WS_DIV_1800 16000000
#define CONFIG_FLASH_WRITE_SIZE_3300 4
#define CONFIG_FLASH_WS_DIV_3300 30000000

/* No page mode on STM32F, so no benefit to larger write sizes */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE CONFIG_FLASH_WRITE_SIZE

#define CONFIG_RAM_BASE         0x20000000
#define CONFIG_RAM_SIZE         0x00020000

/* Size of one firmware image in flash */
#define CONFIG_FW_IMAGE_SIZE    (128 * 1024)

#define CONFIG_FW_RO_OFF        0
#define CONFIG_FW_RO_SIZE       CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_RW_OFF        CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_RW_SIZE       CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_WP_RO_OFF     CONFIG_FW_RO_OFF
#define CONFIG_FW_WP_RO_SIZE    CONFIG_FW_IMAGE_SIZE

/*
 * PSTATE is not yet supported.
 */
#undef CONFIG_FLASH_PSTATE

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 85

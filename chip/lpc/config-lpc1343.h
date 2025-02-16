/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
#define CONFIG_FLASH_SIZE_BYTES 0x00008000 /* 32 kB */
#define CONFIG_FLASH_BANK_SIZE 0x800 /* 2 kB */
#define CONFIG_FLASH_ERASE_SIZE 0x800 /* 2 KB */
#define CONFIG_FLASH_WRITE_SIZE 0x8 /* 64 bits */

/* Ideal write size in page-mode */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 0x100 /* 256 (32 double words) */

/*
 * SRAM1 (192kB) at 0x20000000
 * SRAM2 (64kB) at 0x20030000
 * so they are contiguous.
 */
#define CONFIG_RAM_BASE 0x10000000
#define CONFIG_RAM_SIZE 0x00002000 /* 8 kB */

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 58

/* Number of DMA channels supported (8 channels each for DMA1 and DMA2) */
/*#define DMAC_COUNT 16*/

/* DFU Address */
/*#define STM32_DFU_BASE 0x0bf90000*/

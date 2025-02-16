/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* CPU core BFD configuration */
#include "core/cortex-m/config_core.h"

/* Default to UART 1 for EC console */
#define CONFIG_UART_CONSOLE 1

/* Number of I2C ports, can be overridden in variant */
#define I2C_PORT_COUNT 1

#if defined(CHIP_VARIANT_LPC1343)
#include "config-lpc1343.h"
#else
#error "Unsupported chip variant"
#endif

#define CONFIG_PROGRAM_MEMORY_BASE 0x00000000

/* Memory-mapped internal flash */
#define CONFIG_INTERNAL_STORAGE
#define CONFIG_MAPPED_STORAGE

/* Program is run directly from storage */
#define CONFIG_MAPPED_STORAGE_BASE CONFIG_PROGRAM_MEMORY_BASE

/* Compute the rest of the flash params from these */
#include "config_std_internal_flash.h"

/* Additional special purpose regions (USB RAM and other special SRAMs) */
#undef CONFIG_CHIP_MEMORY_REGIONS

/* System stack size */
#define CONFIG_STACK_SIZE 768

/* Idle task stack size */
#define IDLE_TASK_STACK_SIZE 256

/* Smaller task stack size */
#define SMALLER_TASK_STACK_SIZE 384

/* Default task stack size */
#define TASK_STACK_SIZE 512

/* Larger task stack size, for hook task */
#define LARGER_TASK_STACK_SIZE 640

/* Even bigger */
#define VENTI_TASK_STACK_SIZE 768
#define ULTRA_TASK_STACK_SIZE 1056
#define TRENTA_TASK_STACK_SIZE 1184

/*
 * Console stack size. For test builds, the console is used to interact with
 * the test, and insufficient stack size causes console stack overflow after
 * running the on-device tests.
 */
#define CONSOLE_TASK_STACK_SIZE 4096

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS 500
#define HOOK_TICK_INTERVAL (HOOK_TICK_INTERVAL_MS * MSEC)

/* Use DMA */
#undef CONFIG_DMA_CROS

/* STM32 features RTC (optional feature) */
#undef CONFIG_RTC

/* Number of peripheral request signals per DMA channel */
/*#define STM32_DMA_PERIPHERALS_PER_CHANNEL 4*/

/*
 * Use DMA for UART transmit for all platforms.  DMA for UART receive is
 * enabled on a per-chip basis because it doesn't seem to work reliably on
 * STM32F (see crosbug.com/p/24141).
 */
#undef CONFIG_UART_TX_DMA

/* Chip needs to do custom pre-init */
#define CONFIG_CHIP_PRE_INIT

#define GPIO_NAME_BY_PIN(port, index) #port #index
#define GPIO_PIN(port, index) LPC_GPIO_BASE(port), BIT(index)
#define GPIO_PIN_MASK(p, m) .port = LPC_GPIO_BASE(p), .mask = (m)

#endif /* __CROS_EC_CONFIG_CHIP_H */

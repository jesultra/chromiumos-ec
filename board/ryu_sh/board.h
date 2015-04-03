/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ryu sensor hub configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 84 MHz CPU/AHB/APB2 clock frequency (APB1 = 42 Mhz) */
#define CPU_CLOCK 84000000
#define CONFIG_VOLTAGE_CORE_1800
#define CONFIG_FLASH_WRITE_SIZE CONFIG_FLASH_WRITE_SIZE_1800
#define CONFIG_FLASH_WS_DIV CONFIG_FLASH_WS_DIV_1800

#define CONFIG_CMD_FLASH
#undef CONFIG_FLASH_ERASE_SUPPORT
#define CONFIG_FLASH_ERASE_SUPPORT 3

/* the UART console is on USART1 (PA9/PA10) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1

/* By default, enable all console messages  */
#define CC_DEFAULT     CC_ALL

/* Optional features */
#undef CONFIG_EXTPOWER
#undef CONFIG_HIBERNATE
#define CONFIG_ACCELGYRO_LSM6DS0
#define CONFIG_STM_HWTIMER32
#define CONFIG_DMA_HELP
#define CONFIG_I2C
/* #define CONFIG_I2C_DEBUG */
#undef  CONFIG_LID_SWITCH
#undef CONFIG_CMD_POWER_AP
#define CONFIG_POWER_COMMON
#define CONFIG_CHIPSET_ECDRIVEN
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_VBOOT_HASH

#define CONFIG_UART_TX_DMA
#undef CONFIG_UART_RX_DMA
#define CONFIG_UART_TX_DMA_CH STM32_DMAS_USART1_TX
#define CONFIG_UART_RX_DMA_CH STM32_DMAS_USART1_RX
#define CONFIG_UART_TX_REQ_CH STM32_REQ_USART1_TX
#define CONFIG_UART_RX_REQ_CH STM32_REQ_USART1_RX

/* Use a bigger console output buffer */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 8192


/* I2C ports configuration */
#define I2C_PORT_MASTER 1
#define I2C_PORT_SLAVE  0
#define I2C_PORT_ACCEL I2C_PORT_MASTER
#define I2C_PORT_COMPASS I2C_PORT_MASTER
/* #define CONFIG_I2C_DEBUG_PASSTHRU */

/* slave address for host commands */
#ifdef HAS_TASK_HOSTCMD
#define CONFIG_HOSTCMD_I2C_SLAVE_ADDR 0x3e
#endif

/*
 * Write protect is active high, but given WP line is not implemented,
 * the memory is not write protected.
 */
#define CONFIG_WP_ACTIVE_HIGH

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_WATCHDOG 11

enum power_signal {
	ECDRIVEN_SUSPEND_ASSERTED,

	/* Number of power signals */
	POWER_SIGNAL_COUNT
};

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */

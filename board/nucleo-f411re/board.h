/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nucleo-F411RE development board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 84 MHz CPU/AHB/APB2 clock frequency (APB1 = 42 Mhz) */
#define CPU_CLOCK 84000000
#define CONFIG_VOLTAGE_CORE_3300
#define CONFIG_FLASH_WRITE_SIZE CONFIG_FLASH_WRITE_SIZE_3300
#define CONFIG_FLASH_WS_DIV CONFIG_FLASH_WS_DIV_3300


/* the UART console is on USART2 (PA2/PA3) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2

/* Optional features */
#define CONFIG_STM_HWTIMER32
#define CONFIG_WATCHDOG_HELP
#define CONFIG_TASK_PROFILING
#undef CONFIG_HIBERNATE
#undef CONFIG_LID_SWITCH

#undef CONFIG_ADC
#define CONFIG_DMA_HELP
#define CONFIG_I2C

#define CONFIG_UART_TX_DMA
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 8192
#undef CONFIG_UART_RX_DMA
#define CONFIG_UART_TX_DMA_CH STM32_DMAS_USART2_TX
#define CONFIG_UART_RX_DMA_CH STM32_DMAS_USART2_RX
#define CONFIG_UART_TX_REQ_CH STM32_REQ_USART2_TX
#define CONFIG_UART_RX_REQ_CH STM32_REQ_USART2_RX

#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO

#define CONFIG_CMD_FLASH

/* I2C ports configuration */
#define CONFIG_I2C_DEBUG
#define I2C_PORT_MASTER 0
#define I2C_PORT_SLAVE 1        /* needed for DMAC macros (ugh) */
#define I2C_PORT_ACCEL I2C_PORT_MASTER

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_WATCHDOG 11

#include "gpio_signal.h"

/* ADC signal */
enum adc_channel {
	ADC1_0 = 0,
	ADC1_1,
	ADC1_4,
	ADC1_8,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */

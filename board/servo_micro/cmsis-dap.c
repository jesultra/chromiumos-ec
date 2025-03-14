/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock_chip.h"
#include "cmsis-dap.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"

static const uint32_t DEFAULT_JTAG_CLOCK_HZ = 100000;
static const uint32_t OVERHEAD_CLOCK_CYCLES = 50;

static uint16_t jtag_half_period_count;

/* Busy-wait half a JTAG clock cycle. */
void cmsis_dap_half_clock_delay(void)
{
	/* Calculate the future timer value, that we want to wait for. */
	uint16_t until = STM32_TIM_CNT(JTAG_TIMER) + jtag_half_period_count;

	/*
	 * Busy-wait until counter is past the value (taking care around
	 * wrapping).
	 */
	while (((int16_t)(STM32_TIM_CNT(JTAG_TIMER) - until)) < 0)
		;
}

void cmsis_dap_enable_jtag_pins(void)
{
	/* Disconnect DUT_SPI1 lines from PA0/1 */
	gpio_set_level(GPIO_SPI1_MUX_SEL, true);

	/* Turn PA0/1 into GPIO rather than UART */
	gpio_set_flags(GPIO_UART3_TX_SERVO_JTAG_TCK, GPIO_OUT_HIGH);
	gpio_set_flags(GPIO_UART3_RX_JTAG_BUFFER_TO_SERVO_TDO, GPIO_INPUT);

	/* Configure buffers for output */
	gpio_set_level(GPIO_SERVO_JTAG_TMS_DIR, true);
	gpio_set_level(GPIO_SERVO_JTAG_TDI_DIR, true);
	gpio_set_level(GPIO_SERVO_JTAG_TRST_DIR, true);

	/* Configure signals feeding into above buffers */
	gpio_set_flags(GPIO_SERVO_JTAG_TMS, GPIO_OUT_HIGH);
	gpio_set_flags(GPIO_SERVO_JTAG_TDI, GPIO_OUT_HIGH);
	gpio_set_flags(GPIO_SERVO_JTAG_TRST_L, GPIO_OUT_HIGH);

	/* Enable JTAG buffers */
	gpio_set_level(GPIO_JTAG_BUFOUT_EN_L, false);
	gpio_set_level(GPIO_JTAG_BUFIN_EN_L, false);
}

void cmsis_dap_enable_swd_pins(void)
{
	cmsis_dap_enable_jtag_pins();
}

void cmsis_dap_disable_jtag_swd_pins(void)
{
	/* Disable JTAG buffers */
	gpio_set_level(GPIO_JTAG_BUFOUT_EN_L, true);
	gpio_set_level(GPIO_JTAG_BUFIN_EN_L, true);

	/* Turn PA0/1 into GPIO rather than UART */
	gpio_set_flags(GPIO_UART3_TX_SERVO_JTAG_TCK, GPIO_ALTERNATE);
	gpio_set_flags(GPIO_UART3_RX_JTAG_BUFFER_TO_SERVO_TDO, GPIO_ALTERNATE);

	/* Re-connect DUT_SPI1 lines to PA0/1 */
	/* TODO: Probably should restore state as before enabling JTAG */
	gpio_set_level(GPIO_SPI1_MUX_SEL, false);
}

void cmsis_dap_swdio_input(void)
{
	gpio_set_flags(GPIO_SERVO_JTAG_TMS, GPIO_INPUT);
	gpio_set_level(GPIO_SERVO_JTAG_TMS_DIR, false);
}

void cmsis_dap_swdio_output(bool level)
{
	gpio_set_level(GPIO_SERVO_JTAG_TMS_DIR, true);
	gpio_set_flags(GPIO_SERVO_JTAG_TMS,
		       level ? GPIO_OUT_HIGH : GPIO_OUT_LOW);
}

int cmsis_dap_set_period(uint32_t new_clock_hz)
{
	uint32_t new_half_period_count =
		clock_get_timer_freq() / new_clock_hz / 2;

	/*
	 * At this point, new_half_period_count contains the number of
	 * timer clock cycles for a half JTAG clock period.  This will
	 * be used in a wait loop in the bit banging logic.
	 *
	 * Empirically, it has been stablished that at least 50 timer
	 * clock cycles are used by execution of GPIO manipulations
	 * involved in clock toggling and data shifting, so we
	 * subtract that from the number of cycles that will be
	 * "burned" in each clock phase while generating the waveform.
	 */
	if (new_half_period_count <= OVERHEAD_CLOCK_CYCLES) {
		/*
		 * Requested speed as at or above the limit, run with no
		 * delay at all.
		 */
		new_half_period_count = 0;
	} else {
		new_half_period_count -= OVERHEAD_CLOCK_CYCLES;
	}

	if (new_half_period_count >= 0x8000) {
		return EC_ERROR_INVAL;
	} else {
		jtag_half_period_count = new_half_period_count;
		return EC_SUCCESS;
	}
}

static void cmsis_dap_period_init(void)
{
	/* Enable TIMER7 for precise JTAG bit-banging. */
	__hw_timer_enable_clock(JTAG_TIMER, 1);
	STM32_TIM_CR1(JTAG_TIMER) = STM32_TIM_CR1_CEN;

	jtag_half_period_count =
		clock_get_timer_freq() / DEFAULT_JTAG_CLOCK_HZ / 2 -
		OVERHEAD_CLOCK_CYCLES;
}
DECLARE_HOOK(HOOK_INIT, cmsis_dap_period_init, HOOK_PRIO_DEFAULT);

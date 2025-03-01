/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock_chip.h"
#include "cmsis-dap.h"
#include "common.h"
#include "consumer.h"
#include "gpio.h"
#include "panic.h"
#include "producer.h"
#include "queue.h"
#include "queue_policies.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "usb-stream.h"

static const uint32_t DEFAULT_JTAG_CLOCK_HZ = 100000;
static const uint32_t OVERHEAD_CLOCK_CYCLES = 50;

static uint16_t jtag_half_period_count;

int jtag_pins[JTAG_INVALID] = {
	GPIO_CN7_1, /* TCLK */
	GPIO_CN7_7, /* TMS */
	GPIO_CN7_3, /* TDI */
	GPIO_CN7_5, /* TDO */
	GPIO_CN7_16, /* TRSTn */
};
static int saved_pin_flags[JTAG_INVALID];

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
	for (size_t i = 0; i < JTAG_INVALID; i++) {
		saved_pin_flags[i] = gpio_get_flags(jtag_pins[i]);
	}

	gpio_set_flags(jtag_pins[JTAG_TMS], GPIO_OUT_LOW);
	gpio_set_flags(jtag_pins[JTAG_TDI], GPIO_OUT_LOW);
	gpio_set_flags(jtag_pins[JTAG_TCLK], GPIO_OUT_HIGH);
	gpio_set_flags(jtag_pins[JTAG_TRSTn], GPIO_ODR_HIGH | GPIO_PULL_UP);
	gpio_set_flags(jtag_pins[JTAG_TDO], GPIO_INPUT | GPIO_PULL_UP);
}

void cmsis_dap_disable_jtag_pins(void)
{
	for (size_t i = 0; i < JTAG_INVALID; i++) {
		gpio_set_flags(jtag_pins[i], saved_pin_flags[i]);
	}
}

static int command_jtag_set_pins(int argc, const char **argv)
{
	int new_pins[JTAG_INVALID];

	if (argc < 7)
		return EC_ERROR_PARAM_COUNT;

	for (int i = 0; i < JTAG_INVALID; i++) {
		new_pins[i] = gpio_find_by_name(argv[2 + i]);
		if (new_pins[i] == GPIO_COUNT)
			return EC_ERROR_PARAM2 + i;
		/* Check if same pin listed twice. */
		for (int j = 0; j < i; j++) {
			if (new_pins[i] == new_pins[j]) {
				ccprintf("Error: Pin %s listed twice\n",
					 gpio_list[jtag_pins[i]].name);
				return EC_ERROR_PARAM2 + i;
			}
		}
	}

	/*
	 * No errors parsing command line, now disconnect any ongoing
	 * CMSIS-DAP, and apply the new pin settings.
	 */
	cmsis_dap_reinit();

	for (int i = 0; i < JTAG_INVALID; i++)
		jtag_pins[i] = new_pins[i];

	return EC_SUCCESS;
}

static int command_jtag(int argc, const char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[1], "set-pins"))
		return command_jtag_set_pins(argc, argv);
	return 0;
}
DECLARE_CONSOLE_COMMAND_FLAGS(jtag, command_jtag, "",
			      "set-pins <TCLK> <TMS> <TDI> <TDO> <TRSTn>",
			      CMD_FLAG_RESTRICTED);

static void cmsis_dap_period_init(void)
{
	jtag_half_period_count =
		clock_get_timer_freq() / DEFAULT_JTAG_CLOCK_HZ / 2 -
		OVERHEAD_CLOCK_CYCLES;
}
DECLARE_HOOK(HOOK_INIT, cmsis_dap_period_init, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_REINIT, cmsis_dap_period_init, HOOK_PRIO_DEFAULT);

/*
 * Run CMSIS-DAP reinit hook before DEFAULT such as if the CMSIS-DAP task is
 * blocked in any cmsis_dap_xxx() methods in gpio.c or i2c.c, they will be
 * unwound before the hook in these files are executed to reset their state, and
 * such that CMSIS-DAP restores JTAG pins to pre-connection state BEFORE gpio.c
 * resets every pin to factory default.
 */
DECLARE_HOOK(HOOK_REINIT, cmsis_dap_reinit, HOOK_PRIO_PRE_DEFAULT);

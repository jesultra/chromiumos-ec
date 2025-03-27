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

int cmsis_dap_set_period(uint32_t new_clock_hz)
{
	return 0;
}

/* Busy-wait half a JTAG clock cycle. */
void cmsis_dap_half_clock_delay(void)
{
}

void cmsis_dap_enable_swd_pins(void)
{
	gpio_set_flags(GPIO_SAM3X_SWCLK, GPIO_OUT_HIGH);
	gpio_set_flags(GPIO_SAM3X_SWDIO, GPIO_OUT_HIGH);
}

void cmsis_dap_disable_jtag_swd_pins(void)
{
	gpio_set_flags(GPIO_SAM3X_SWCLK, GPIO_INPUT);
	gpio_set_flags(GPIO_SAM3X_SWDIO, GPIO_INPUT);
}

void cmsis_dap_swdio_input(void)
{
	gpio_set_flags(GPIO_SAM3X_SWDIO, GPIO_INPUT);
}

void cmsis_dap_swdio_output(bool level)
{
	if (level)
		gpio_set_flags(GPIO_SAM3X_SWDIO, GPIO_OUT_HIGH);
	else
		gpio_set_flags(GPIO_SAM3X_SWDIO, GPIO_OUT_LOW);
}

/*
 * Run CMSIS-DAP reinit hook before DEFAULT such as if the CMSIS-DAP task is
 * blocked in any cmsis_dap_xxx() methods in gpio.c or i2c.c, they will be
 * unwound before the hook in these files are executed to reset their state, and
 * such that CMSIS-DAP restores JTAG pins to pre-connection state BEFORE gpio.c
 * resets every pin to factory default.
 */
DECLARE_HOOK(HOOK_REINIT, cmsis_dap_reinit, HOOK_PRIO_PRE_DEFAULT);

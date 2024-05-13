/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "zephyr/kernel.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

extern volatile int pen_chg_time;
extern volatile int pen_stp_time;
extern volatile int pen_err_time;
extern int pen_charge_status;
extern void pen_charge(void);

FAKE_VOID_FUNC(x_ec_interrupt);
FAKE_VOID_FUNC(lsm6dso_interrupt);
FAKE_VOID_FUNC(lis2dw12_interrupt);

static int interrupt_id;

static void *kyogre_pen_charge_setup(void)
{
	hook_notify(HOOK_INIT);
	return NULL;
}

void pen_fault_interrupt(enum gpio_signal signal)
{
	interrupt_id = 1;
}

ZTEST_SUITE(main_pen_charge, NULL, kyogre_pen_charge_setup, NULL, NULL, NULL);

ZTEST(main_pen_charge, test_main_pen_charge)
{
	/* pen_fault_interrupt test*/
	const struct device *pen_fault_gpio =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(pen_fault_od), gpios));
	const gpio_port_pins_t pen_fault_pin =
		DT_GPIO_PIN(DT_NODELABEL(pen_fault_od), gpios);

	zassert_ok(gpio_emul_input_set(pen_fault_gpio, pen_fault_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(pen_fault_gpio, pen_fault_pin, 1), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(interrupt_id, 1, "interrupt_id=%d", interrupt_id);

	/* pen_charge count down test */
	pen_err_time = 600;
	pen_chg_time = 0;
	pen_stp_time = 0;
	pen_charge();
	zassert_equal(pen_err_time, 599, "pen_err_time=%d", pen_err_time);

	pen_err_time = 0;
	pen_chg_time = 43200;
	pen_stp_time = 1;
	pen_charge();
	zassert_equal(pen_chg_time, 43199, "pen_chg_time=%d", pen_chg_time);

	pen_err_time = 0;
	pen_chg_time = 0;
	pen_stp_time = 2;
	pen_charge();
	zassert_equal(pen_stp_time, 1, "pen_stp_time=%d", pen_stp_time);

	/* pen_charge status transition test */
	/*   STATUS_ERROR  1                 */
	/*   STATUS_CHARGE 2                 */
	/*   STATUS_STOP   3                 */
	pen_err_time = 1;
	pen_chg_time = 0;
	pen_stp_time = 0;
	pen_charge();
	zassert_equal(pen_charge_status, 1, "pen charge status=%d",
		      pen_charge_status);
	pen_charge();
	zassert_equal(pen_charge_status, 2, "pen charge status=%d",
		      pen_charge_status);

	pen_err_time = 0;
	pen_chg_time = 1;
	pen_stp_time = 1;
	pen_charge();
	zassert_equal(pen_charge_status, 2, "pen charge status=%d",
		      pen_charge_status);
	pen_charge();
	zassert_equal(pen_charge_status, 3, "pen charge status=%d",
		      pen_charge_status);
	pen_charge();
	zassert_equal(pen_charge_status, 2, "pen charge status=%d",
		      pen_charge_status);
}

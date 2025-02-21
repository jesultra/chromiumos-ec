/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power_events.h"
#include "board_led.h"
#include "charge_manager.h"
#include "chipset.h"
#include "dirks.h"
#include "emul/tcpc/emul_tcpci.h"
#include "extpower.h"
#include "hooks.h"
#include "keyboard_protocol.h"
#include "led_common.h"
#include "nissa_hdmi.h"
#include "system.h"
#include "tcpm/tcpci.h"
#include "typec_control.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

extern const struct ec_response_keybd_config nereid_kb_legacy;

FAKE_VOID_FUNC(nissa_configure_hdmi_rails);
FAKE_VOID_FUNC(nissa_configure_hdmi_vcc);
FAKE_VALUE_FUNC(int, cbi_get_board_version, uint32_t *);

FAKE_VALUE_FUNC(int, ppc_is_sourcing_vbus, int);
FAKE_VALUE_FUNC(int, ppc_vbus_source_enable, int, int);
FAKE_VOID_FUNC(pd_set_vbus_discharge, int, int);
FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int);
FAKE_VALUE_FUNC(int, ppc_is_vbus_present, int);
FAKE_VOID_FUNC(syv682x_interrupt, enum gpio_signal)

FAKE_VALUE_FUNC(int, chipset_in_state, int);
FAKE_VALUE_FUNC(int, set_color_power, enum led_color, int);

uint8_t board_get_charger_chip_count(void)
{
	return 2;
}

static void test_before(void *fixture)
{
	RESET_FAKE(nissa_configure_hdmi_rails);
	RESET_FAKE(nissa_configure_hdmi_vcc);
	RESET_FAKE(cbi_get_board_version);

	RESET_FAKE(ppc_is_sourcing_vbus);
	RESET_FAKE(ppc_vbus_source_enable);
	RESET_FAKE(pd_set_vbus_discharge);
	RESET_FAKE(ppc_vbus_sink_enable);
	RESET_FAKE(ppc_is_vbus_present);
	RESET_FAKE(syv682x_interrupt);

	RESET_FAKE(chipset_in_state);
	RESET_FAKE(set_color_power);
}

ZTEST_SUITE(dirks, NULL, NULL, test_before, NULL, NULL);

static int cbi_get_board_version_1(uint32_t *version)
{
	*version = 1;
	return 0;
}

static int cbi_get_board_version_2(uint32_t *version)
{
	*version = 2;
	return 0;
}

ZTEST(dirks, test_hdmi_power)
{
	/* Board version less than 2 configures both */
	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_1;
	nissa_configure_hdmi_power_gpios();
	zassert_equal(nissa_configure_hdmi_vcc_fake.call_count, 1);
	zassert_equal(nissa_configure_hdmi_rails_fake.call_count, 1);

	/* Later versions only enable core rails */
	cbi_get_board_version_fake.custom_fake = cbi_get_board_version_2;
	nissa_configure_hdmi_power_gpios();
	zassert_equal(nissa_configure_hdmi_vcc_fake.call_count, 1);
	zassert_equal(nissa_configure_hdmi_rails_fake.call_count, 2);
}

ZTEST(dirks, test_extpower_is_present)
{
	/* AC is always present */
	zassert_true(extpower_is_present());
}

ZTEST(dirks, test_ppc_interrupt)
{
	const struct gpio_dt_spec *usb_c0_int =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_fault_l);

	hook_notify(HOOK_INIT);
	zassert_ok(gpio_emul_input_set(usb_c0_int->port, usb_c0_int->pin, 1),
		   NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(usb_c0_int->port, usb_c0_int->pin, 0),
		   NULL);
	zassert_equal(syv682x_interrupt_fake.call_count, 1);
}

ZTEST(dirks, test_board_vbus_source_enabled)
{
	/* check C0 */
	board_vbus_source_enabled(0);
	zassert_equal(ppc_is_sourcing_vbus_fake.call_count, 1);

	/* Errors cause early return */
	ppc_is_sourcing_vbus_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(board_vbus_source_enabled(0), EC_ERROR_UNKNOWN);

	/* Invalid port always return 0 */
	zassert_equal(board_vbus_source_enabled(1), 0);
}

ZTEST(dirks, test_pd_power_supply_reset)
{
	ppc_is_sourcing_vbus_fake.return_val = 1;

	/* Disables sourcing and discharges VBUS on active port */
	pd_power_supply_reset(0);
	zassert_equal(ppc_vbus_source_enable_fake.call_count, 1);
	zassert_equal(ppc_vbus_source_enable_fake.arg0_val, 0);
	zassert_equal(ppc_vbus_source_enable_fake.arg1_val, 0);
	zassert_equal(pd_set_vbus_discharge_fake.call_count, 1);
	zassert_equal(pd_set_vbus_discharge_fake.arg0_val, 0);
	zassert_equal(pd_set_vbus_discharge_fake.arg1_val, 1);

	/* Invalid port does nothing */
	pd_power_supply_reset(1);
	zassert_equal(ppc_is_sourcing_vbus_fake.call_count, 1);
}

ZTEST(dirks, test_pd_set_power_supply_ready)
{
	zassert_ok(pd_set_power_supply_ready(0));
	/* Disable charging */
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 1);
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_val, 0);
	zassert_false(ppc_vbus_sink_enable_fake.arg1_val);
	/* Disabled VBUS discharge */
	zassert_equal(pd_set_vbus_discharge_fake.call_count, 1);
	zassert_equal(pd_set_vbus_discharge_fake.arg0_val, 0);
	zassert_false(pd_set_vbus_discharge_fake.arg1_val);
	/* Enabled sourcing */
	zassert_equal(ppc_vbus_source_enable_fake.call_count, 1);
	zassert_equal(ppc_vbus_source_enable_fake.arg0_val, 0);
	zassert_true(ppc_vbus_source_enable_fake.arg1_val);

	/* Errors cause early return */
	ppc_vbus_source_enable_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(pd_set_power_supply_ready(0), EC_ERROR_UNKNOWN);

	ppc_vbus_sink_enable_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(pd_set_power_supply_ready(0), EC_ERROR_UNKNOWN);

	zassert_equal(pd_set_power_supply_ready(31), EC_ERROR_INVAL);
}

ZTEST(dirks, test_pd_snk_is_vbus_provided)
{
	/* pd_snk_is_vbus_provided just delegates to ppc_is_vbus_present */
	pd_snk_is_vbus_provided(0);
	zassert_equal(ppc_is_vbus_present_fake.call_count, 1);

	/* Invalid port */
	zassert_equal(pd_snk_is_vbus_provided(1), 0);
}

#define TEST_DELAY_MS 1
#define LED_CPU_DELAY_MS (2000 + TEST_DELAY_MS)
#define LED_PULSE_TICK_US 40
#define LED_PULSE_US 2000

static int sys_state;

static int chipset_in_state_mock(int state_mask)
{
	return (state_mask & sys_state) == sys_state;
}

ZTEST(dirks, test_led_shutdown)
{
	/* LED off when shutdown */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	zassert_equal(set_color_power_fake.call_count, 0);
	k_sleep(K_MSEC(LED_CPU_DELAY_MS));
	zassert_equal(set_color_power_fake.call_count, 1);
	zassert_equal(set_color_power_fake.arg0_val, LED_OFF);
	zassert_equal(set_color_power_fake.arg1_val, 0);
}

ZTEST(dirks, test_led_suspend)
{
	/* WHITE LED breathing when suspend */
	hook_notify(HOOK_CHIPSET_SUSPEND);
	zassert_equal(set_color_power_fake.call_count, 0);
	k_sleep(K_MSEC(LED_CPU_DELAY_MS));
	zassert_equal(set_color_power_fake.call_count, 1);
	zassert_equal(set_color_power_fake.arg0_val, LED_WHITE);
	zassert_equal(set_color_power_fake.arg1_val, 0);
	k_sleep(K_MSEC(LED_PULSE_TICK_US));
	zassert_equal(set_color_power_fake.call_count, 2);
	zassert_equal(set_color_power_fake.arg0_val, LED_WHITE);
	zassert_equal(set_color_power_fake.arg1_val, 2);
	k_sleep(K_MSEC(LED_PULSE_TICK_US));
	zassert_equal(set_color_power_fake.call_count, 3);
	zassert_equal(set_color_power_fake.arg0_val, LED_WHITE);
	zassert_equal(set_color_power_fake.arg1_val, 4);
}

ZTEST(dirks, test_led_resume)
{
	/* WHITE LED always on when chipset is on */
	hook_notify(HOOK_CHIPSET_RESUME);
	zassert_equal(set_color_power_fake.call_count, 1);
	zassert_equal(set_color_power_fake.arg0_val, LED_WHITE);
	zassert_equal(set_color_power_fake.arg1_val, 100);
}

ZTEST(dirks, test_led_init)
{
	/* If chipset state is off at init, Turn white led off */
	sys_state = CHIPSET_STATE_ANY_OFF;
	chipset_in_state_fake.custom_fake = chipset_in_state_mock;

	hook_notify(HOOK_INIT);
	zassert_equal(set_color_power_fake.call_count, 1);
	zassert_equal(set_color_power_fake.arg0_val, LED_WHITE);
	zassert_equal(set_color_power_fake.arg1_val, 0);

	/* If chipset state is on at init, Turn white led on */
	sys_state = CHIPSET_STATE_ON;
	chipset_in_state_fake.custom_fake = chipset_in_state_mock;

	hook_notify(HOOK_INIT);
	zassert_equal(set_color_power_fake.call_count, 2);
	zassert_equal(set_color_power_fake.arg0_val, LED_WHITE);
	zassert_equal(set_color_power_fake.arg1_val, 100);
}

ZTEST(dirks, test_led_brightness_range)
{
	uint8_t brightness[EC_LED_COLOR_COUNT] = { 0 };

	/* Verify LED set to OFF */
	led_set_brightness(EC_LED_ID_POWER_LED, brightness);
	zassert_equal(set_color_power_fake.call_count, 1);
	zassert_equal(set_color_power_fake.arg0_val, LED_OFF);
	zassert_equal(set_color_power_fake.arg1_val, 0);

	/* Verify LED colors defined are reflected in the brightness
	 * array.
	 */
	led_get_brightness_range(EC_LED_ID_POWER_LED, brightness);
	zassert_equal(brightness[EC_LED_COLOR_RED], 100);
	zassert_equal(brightness[EC_LED_COLOR_WHITE], 100);

	led_set_brightness(EC_LED_ID_POWER_LED, brightness);
	zassert_equal(set_color_power_fake.call_count, 2);
	zassert_equal(set_color_power_fake.arg0_val, LED_WHITE);
	zassert_equal(set_color_power_fake.arg1_val, 100);

	memset(brightness, 0, sizeof(brightness));

	brightness[EC_LED_COLOR_RED] = 50;
	led_set_brightness(EC_LED_ID_POWER_LED, brightness);
	zassert_equal(set_color_power_fake.call_count, 3);
	zassert_equal(set_color_power_fake.arg0_val, LED_RED);
	zassert_equal(set_color_power_fake.arg1_val, 50);
}

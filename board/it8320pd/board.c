/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* IT8380 development board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "clock.h"
#include "common.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "ec2i_chip.h"
#include "extpower.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "intc.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "lpc.h"
#include "pi3usb9281.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#ifdef CONFIG_USB_SWITCH_PI3USB9281
struct pi3usb9281_config pi3usb9281_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_MUX,
		.mux_lock = NULL,
	},
	{
		.i2c_port = I2C_PORT_USB_MUX,
		.mux_lock = NULL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9281_chips) ==
	     CONFIG_USB_SWITCH_PI3USB9281_CHIP_COUNT);
#endif

#ifdef CONFIG_USB_MUX_PI3USB30532
struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = 0xa8,
		.driver = &pi3usb30532_usb_mux_driver,
	},
	{
		.port_addr = 0x20,
		.driver = &ps8740_usb_mux_driver,
	}
};
#endif

/* Send host event up to AP */
void pd_send_host_event(int mask)
{

}

/**
 * Reset PD MCU
 */
void board_reset_pd_mcu(void)
{
	gpio_set_level(GPIO_PD_RST_L, 0);
	usleep(100);
	gpio_set_level(GPIO_PD_RST_L, 1);
}

void pd_mcu_interrupt(enum gpio_signal signal)
{
#ifdef HAS_TASK_PDCMD
	/* Exchange status with PD MCU to determine interrupt cause */
	host_command_pd_send_status(0);
#endif
}

void vbus0_evt(enum gpio_signal signal)
{
	/* VBUS present GPIO is inverted */
#ifdef CONFIG_USB_CHARGER
	usb_charger_vbus_change(0, !gpio_get_level(signal));
	task_wake(TASK_ID_PD_C0);
#endif
}

void vbus1_evt(enum gpio_signal signal)
{
/* VBUS present GPIO is inverted */
#ifdef CONFIG_USB_CHARGER
	usb_charger_vbus_change(1, !gpio_get_level(signal));
	task_wake(TASK_ID_PD_C1);
#endif
}

void usb0_evt(enum gpio_signal signal)
{
#ifdef CONFIG_USB_CHARGER
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12, 0);
#endif
}

void usb1_evt(enum gpio_signal signal)
{
#ifdef CONFIG_USB_CHARGER
	task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12, 0);
#endif
}

int board_get_battery_soc(void)
{
	/*msmart return batt_soc;*/
	return 50;
}

int board_set_active_charge_port(int charge_port)
{
#ifdef CONFIG_USB_POWER_DELIVERY
	/* charge port is a realy physical port */
	int is_real_port = (charge_port >= 0 &&
			    charge_port < CONFIG_USB_PD_PORT_COUNT);
	/* check if we are source vbus on that port */
	int source = gpio_get_level(charge_port == 0 ? GPIO_USB_C0_5V_EN :
						       GPIO_USB_C1_5V_EN);

	if (is_real_port && source) {
		CPRINTS("Skip enable p%d", charge_port);
		return EC_ERROR_INVAL;
	}

	CPRINTS("New chg p%d", charge_port);

	if (charge_port == CHARGE_PORT_NONE) {
		/* Disable both ports */
		gpio_set_level(GPIO_USB_C0_CHARGE_EN_L, 1);
		gpio_set_level(GPIO_USB_C1_CHARGE_EN_L, 1);
	} else {
		/* Make sure non-charging port is disabled */
		gpio_set_level(charge_port ? GPIO_USB_C0_CHARGE_EN_L :
					     GPIO_USB_C1_CHARGE_EN_L, 1);
		/* Enable charging port */
		gpio_set_level(charge_port ? GPIO_USB_C1_CHARGE_EN_L :
					     GPIO_USB_C0_CHARGE_EN_L, 0);
	}
#endif
	return EC_SUCCESS;
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param charge_ma     Desired charge limit (mA).
 */
void board_set_charge_limit(int charge_ma)
{
#ifdef CONFIG_CHARGER_V2
	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT));
#endif
}

#include "gpio_list.h"

/*
 * PWM channels. Must be in the exactly same order as in enum pwm_channel.
 * There total three 16 bits clock prescaler registers for all pwm channels,
 * so use the same frequency and prescaler register setting is required if
 * number of pwm channel greater than three.
 */
const struct pwm_t pwm_channels[] = {
	{7, 0, 30000, PWM_PRESCALER_C4},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

const struct fan_t fans[] = {
	{.flags = FAN_USE_RPM_MODE,
	 .rpm_min = 1500,
	 .rpm_start = 1500,
	 .rpm_max = 6500,
	/*
	 * index of pwm_channels, not pwm output channel.
	 * pwm output channel is member "channel" of pwm_t.
	 */
	 .ch = 0,
	 .pgood_gpio = -1,
	 .enable_gpio = -1,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == CONFIG_FANS);

/*
 * PWM HW channelx binding tachometer channelx for fan control.
 * Four tachometer input pins but two tachometer modules only,
 * so always binding [TACH_CH_TACH0A | TACH_CH_TACH0B] and/or
 * [TACH_CH_TACH1A | TACH_CH_TACH1B]
 */
const struct fan_tach_t fan_tach[] = {
	{TACH_CH_NULL,  -1, -1, -1},
	{TACH_CH_NULL,  -1, -1, -1},
	{TACH_CH_NULL,  -1, -1, -1},
	{TACH_CH_NULL,  -1, -1, -1},
	{TACH_CH_NULL,  -1, -1, -1},
	{TACH_CH_NULL,  -1, -1, -1},
	{TACH_CH_NULL,  -1, -1, -1},
	{TACH_CH_TACH0A, 2, 50, 30},
};
BUILD_ASSERT(ARRAY_SIZE(fan_tach) == PWM_HW_CH_TOTAL);

/* PNPCFG settings */
const struct ec2i_t pnpcfg_settings[] = {
	/* Select logical device 06h(keyboard) */
	{HOST_INDEX_LDN, LDN_KBC_KEYBOARD},
	/* Set IRQ=01h for logical device */
	{HOST_INDEX_IRQNUMX, 0x01},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},

	/* Select logical device 05h(mouse) */
	{HOST_INDEX_LDN, LDN_KBC_MOUSE},
	/* Set IRQ=0Ch for logical device */
	{HOST_INDEX_IRQNUMX, 0x0C},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},

	/* Select logical device 11h(PM1 ACPI) */
	{HOST_INDEX_LDN, LDN_PMC1},
	/* Set IRQ=00h for logical device */
	{HOST_INDEX_IRQNUMX, 0x00},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},

	/* Select logical device 12h(PM2) */
	{HOST_INDEX_LDN, LDN_PMC2},
	/* I/O Port Base Address 200h/204h */
	{HOST_INDEX_IOBAD0_MSB, 0x02},
	{HOST_INDEX_IOBAD0_LSB, 0x00},
	{HOST_INDEX_IOBAD1_MSB, 0x02},
	{HOST_INDEX_IOBAD1_LSB, 0x04},
	/* Set IRQ=00h for logical device */
	{HOST_INDEX_IRQNUMX, 0x00},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},

	/* Select logical device 0Fh(SMFI) */
	{HOST_INDEX_LDN, LDN_SMFI},
	/* H2RAM LPC I/O cycle Dxxx */
	{HOST_INDEX_DSLDC6, 0x00},
	/* Enable H2RAM LPC I/O cycle */
	{HOST_INDEX_DSLDC7, 0x01},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},

	/* Select logical device 17h(PM3) */
	{HOST_INDEX_LDN, LDN_PMC3},
	/* I/O Port Base Address 80h */
	{HOST_INDEX_IOBAD0_MSB, 0x00},
	{HOST_INDEX_IOBAD0_LSB, 0x80},
	{HOST_INDEX_IOBAD1_MSB, 0x00},
	{HOST_INDEX_IOBAD1_LSB, 0x00},
	/* Set IRQ=00h for logical device */
	{HOST_INDEX_IRQNUMX, 0x00},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},
	/* Select logical device 10h(RTCT) */
	{HOST_INDEX_LDN, LDN_RTCT},
	/* P80L Begin Index */
	{HOST_INDEX_DSLDC4, P80L_P80LB},
	/* P80L End Index */
	{HOST_INDEX_DSLDC5, P80L_P80LE},
	/* P80L Current Index */
	{HOST_INDEX_DSLDC6, P80L_P80LC},
#ifdef CONFIG_UART_HOST
	/* Select logical device 2h(UART2) */
	{HOST_INDEX_LDN, LDN_UART2},
	/*
	 * I/O port base address is 2F8h.
	 * Host can use LPC I/O port 0x2F8 ~ 0x2FF to access UART2.
	 * See specification 7.24.4 for more detial.
	 */
	{HOST_INDEX_IOBAD0_MSB, 0x02},
	{HOST_INDEX_IOBAD0_LSB, 0xF8},
	/* IRQ number is 3 */
	{HOST_INDEX_IRQNUMX, 0x03},
	/*
	 * Interrupt Request Type Select
	 * bit1, 0: IRQ request is buffered and applied to SERIRQ.
	 *       1: IRQ request is inverted before being applied to SERIRQ.
	 * bit0, 0: Edge triggered mode.
	 *       1: Level triggered mode.
	 */
	{HOST_INDEX_IRQTP, 0x02},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(pnpcfg_settings) == EC2I_SETTING_COUNT);

/* Initialize board. */
static void board_init(void)
{
	/*
	 * Default no low power idle for EVB,
	 * use console command "sleepmask" to enable it if necessary.
	 */
	disable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);
	/*
	 * The GPIOH.5/6 may be used for flashing purposes if WP pin
	 * is deasserted. The clock of this module needs to be enabled.
	 * So we disable the clock when WP pin is asserted,
	 * this can help to reduce power consumption.
	 */
#ifdef CONFIG_WP_ACTIVE_HIGH
	if (gpio_get_level(GPIO_WP))
		clock_disable_peripheral(CGC_OFFSET_USB, 0, 0);
	else
		clock_enable_peripheral(CGC_OFFSET_USB, 0, 0);
#else
	if (!gpio_get_level(GPIO_WP_L))
		clock_disable_peripheral(CGC_OFFSET_USB, 0, 0);
	else
		clock_enable_peripheral(CGC_OFFSET_USB, 0, 0);
#endif
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	/* Convert to mV (3000mV/1024). */
		{"ADC_VBUSSA",	3000, 1024, 0, 0}, /*GPI0*/
		{"ADC_VBUSSB", 3000, 1024, 0, 1}, /*GPI1*/
		{"ADC_VBUS",  3000, 1024, 0, 2},
		{"AMON_BMON", 3000, 1024, 0, 3},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 35,
	.debounce_down_us = 5 * MSEC,
	.debounce_up_us = 40 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

/*
 * I2C channels (A, B, and C) are using the same timing registers (00h~07h)
 * at default.
 * In order to set frequency independently for each channels,
 * We use timing registers 09h~0Bh, and the supported frequency will be:
 * 50KHz, 100KHz, 400KHz, or 1MHz.
 */
/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
/*
	{"battery", 2, 100, GPIO_I2C_C_SCL, GPIO_I2C_C_SDA},
	{"usb-mux", 0, 400, GPIO_I2C_A_SCL, GPIO_I2C_A_SDA},
	{"pd-mcu",  1, 400, GPIO_I2C_B_SCL, GPIO_I2C_B_SDA},
*/
	{"battery", 2, 100, GPIO_I2C_C_SCL, GPIO_I2C_C_SDA},
	{"usb-mux", 0, 400, GPIO_I2C_A_SCL, GPIO_I2C_A_SDA},
	{"pd-mcu",  2, 400, GPIO_I2C_C_SCL, GPIO_I2C_C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#ifdef CONFIG_USB_POWER_DELIVERY
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	{2, CONFIG_TCPC_I2C_BASE_ADDR},
	{2, CONFIG_TCPC_I2C_BASE_ADDR + 2},
};
#endif

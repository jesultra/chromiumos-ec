#include "gpio.h"
#include "gpio_chip.h"
#include "registers.h"
#include "system.h"

#include <stdint.h>

volatile uint32_t *iocon[4][12] = {
	{
		&LPC_IOCON_RESET_PIO0_0,
		&LPC_IOCON_PIO0_1,
		&LPC_IOCON_PIO0_2,
		&LPC_IOCON_PIO0_3,
		&LPC_IOCON_PIO0_4,
		&LPC_IOCON_PIO0_5,
		&LPC_IOCON_PIO0_6,
		&LPC_IOCON_PIO0_7,
		&LPC_IOCON_PIO0_8,
		&LPC_IOCON_PIO0_9,
		&LPC_IOCON_SWCLK_PIO0_10,
		&LPC_IOCON_R_PIO0_11,
	},
	{
		&LPC_IOCON_R_PIO1_0,
		&LPC_IOCON_R_PIO1_1,
		&LPC_IOCON_R_PIO1_2,
		&LPC_IOCON_SWDIO_PIO1_3,
		&LPC_IOCON_PIO1_4,
		&LPC_IOCON_PIO1_5,
		&LPC_IOCON_PIO1_6,
		&LPC_IOCON_PIO1_7,
		&LPC_IOCON_PIO1_8,
		&LPC_IOCON_PIO1_9,
		&LPC_IOCON_PIO1_10,
		&LPC_IOCON_PIO1_11,
	},
	{
		&LPC_IOCON_PIO2_0,
		&LPC_IOCON_PIO2_1,
		&LPC_IOCON_PIO2_2,
		&LPC_IOCON_PIO2_3,
		&LPC_IOCON_PIO2_4,
		&LPC_IOCON_PIO2_5,
		&LPC_IOCON_PIO2_6,
		&LPC_IOCON_PIO2_7,
		&LPC_IOCON_PIO2_8,
		&LPC_IOCON_PIO2_9,
		&LPC_IOCON_PIO2_10,
		&LPC_IOCON_PIO2_11,
	},
	{
		&LPC_IOCON_PIO3_0,
		&LPC_IOCON_PIO3_1,
		&LPC_IOCON_PIO3_2,
		&LPC_IOCON_PIO3_3,
		&LPC_IOCON_PIO3_4,
		&LPC_IOCON_PIO3_5,
		0,
		0,
		0,
		0,
		0,
		0,
	},
};

volatile uint32_t *get_ioconfig_reg(uint32_t gpio_base, int pin) {
	return iocon[(gpio_base - LPC_GPIO_BASE(0)) / (LPC_GPIO_BASE(1) - LPC_GPIO_BASE(0))][pin];
}

void gpio_pre_init(void)
{
	/* Enable clock to IOCON */
	LPC_SYSCFG_SYSAHBCLKCTRL |= BIT(16);

	const struct gpio_info *g = gpio_list;
	int is_warm = system_is_reboot_warm();

	/* Set all GPIOs to defaults */
	for (int i = 0; i < GPIO_COUNT; i++, g++) {
		int flags = g->flags;

		if (flags & GPIO_DEFAULT)
			continue;

		/*
		 * If this is a warm reboot, don't set the output levels or
		 * we'll shut off the AP.
		 */
		if (is_warm)
			flags &= ~(GPIO_LOW | GPIO_HIGH);

		/* Set up GPIO based on flags */
		gpio_set_flags_by_mask(g->port, g->mask, flags);
	}
}

int gpio_get_flags_by_mask(uint32_t port, uint32_t mask)
{
	/* TODO */
	return 0;
}
	
void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	for (int bit = 0; bit < 12; bit++) {
		if (!(mask & BIT(bit)))
			continue;
		volatile uint32_t *iocon = get_ioconfig_reg(port, bit);
		uint32_t val = *iocon;
		
		/* Set up pullup / pulldown */
		if (flags & GPIO_PULL_UP) {
			val &= ~LPC_IOCON_MODE_MASK;
			val |= LPC_IOCON_MODE_PULLUP;
		} else if (flags & GPIO_PULL_DOWN) {
			val &= ~LPC_IOCON_MODE_MASK;
			val |= LPC_IOCON_MODE_PULLDOWN;
		}
		// TODO: alternate functions val &= ~LPC_IOCON_FUNC_MASK;

		/*
		 * Select open drain first, so that we don't glitch
		 * the signal when changing the line to an output.
		 */
		if (flags & GPIO_OPEN_DRAIN)
			val |= LPC_IOCON_OD;
		*iocon = val;
	}

	if (flags & GPIO_OUTPUT) {
		/*
		 * DATA register serves both as input and output.  So
		 * we have to set direction to output before setting
		 * the output level.
		 */
		LPC_GPIO_DIR(port) |= mask;
		
		if (flags & GPIO_HIGH)
			LPC_GPIO_DATA(port, mask) = mask;
		else if (flags & GPIO_LOW)
			LPC_GPIO_DATA(port, mask) = 0;
	} else if (flags & GPIO_INPUT) {
		LPC_GPIO_DIR(port) &= ~mask;
	}

	for (int bit = 0; bit < 12; bit++) {
		if (!(mask & BIT(bit)))
			continue;
		volatile uint32_t *iocon = get_ioconfig_reg(port, bit);

		/*
		 * De-select open drain last, so that we don't glitch
		 * the signal when changing the line to no longer be
		 * an output.
		 */
		if (!(flags & GPIO_OPEN_DRAIN)) {
			*iocon = *iocon & ~LPC_IOCON_OD;
		}
	}
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	LPC_GPIO_DATA(gpio_list[signal].port, gpio_list[signal].mask) =
		value ? gpio_list[signal].mask : 0;
}

int gpio_get_level(enum gpio_signal signal)
{
	return !!(LPC_GPIO_DATA(gpio_list[signal].port, 0) &
		  gpio_list[signal].mask);
}


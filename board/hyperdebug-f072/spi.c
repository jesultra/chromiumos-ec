/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* HyperDebug SPI logic and console commands */

#include "board_util.h"
#include "builtin/assert.h"
#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "console.h"
#include "dma.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "spi.h"
#include "stm32-dma.h"
#include "timer.h"
#include "usb_spi.h"
#include "util.h"

/*
 * This flag indicates that the SPI request is for a TPM, that is, as the
 * fourth and final command byte is written to the SPI bus, the controller
 * must pay attention to the simultaneous output from the SPI device.  If the
 * byte has the value 0x01, it means that the transaction can immediately
 * proceed (read or write), of the value is 0x00, then the controller must
 * continue polling, until a value 0x01 is received, before proceeding.
 *
 * Care must be taken that this bit does not overlap with any of the
 * "standard" bits declared in chip/stm32/usb_spi.h
 */
#define FLASH_FLAG_TPM_POS 27
#define FLASH_FLAG_TPM (0x1U << FLASH_FLAG_TPM_POS)

/*
 * This flag requests that HyperDebug should wait for a "ready pulse" on a
 * particular pin, before proceeding with the TPM transaction.  (For reads,
 * the wait is between header and data, for writes, the wait is after the data
 * transfer.)
 *
 * Care must be taken that this bit does not overlap with any of the
 * "standard" bits declared in chip/stm32/usb_spi.h
 */
#define FLASH_FLAG_TPM_WAIT_FOR_READY_POS 26
#define FLASH_FLAG_TPM_WAIT_FOR_READY \
	(0x1U << FLASH_FLAG_TPM_WAIT_FOR_READY_POS)

/*
 * List of SPI devices that can be controlled via USB.
 *
 * SPI1 and SPI2 use PCLK (27.5 MHz) as base frequency.
 * QSPI uses either SYSCLK (110 MHz) or MSI (variable) as base frequency.
 *
 * Divisors below result in default SPI clock of approx. 430 kHz for all
 */
struct spi_device_t spi_devices[] = {
	{ .name = "SPI2",
	  .port = 1,
	  .div = 5,
	  .gpio_cs = GPIO_P1_11,
	  .usb_flags = USB_SPI_ENABLED | USB_SPI_CUSTOM_SPI_DEVICE |
		       USB_SPI_CUSTOM_SPI_DEVICE_FULL_DUPLEX_SUPPORTED },
	{ .name = "SPI1",
	  .port = 0,
	  .div = 5,
	  .gpio_cs = GPIO_P1_12,
	  .usb_flags = USB_SPI_ENABLED | USB_SPI_CUSTOM_SPI_DEVICE |
		       USB_SPI_CUSTOM_SPI_DEVICE_FULL_DUPLEX_SUPPORTED },
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

static int spi_device_default_gpio_cs[ARRAY_SIZE(spi_devices)];
static int spi_device_default_div[ARRAY_SIZE(spi_devices)];
static int spi_device_ready_pin[ARRAY_SIZE(spi_devices)];

uint32_t spi_clock(void)
{
	return clock_get_freq();
}

/*
 * Find spi device by name or by number.  Returns an index into spi_devices[],
 * or on error a negative value.
 */
static int find_spi_by_name(const char *name)
{
	int i;
	char *e;
	i = strtoi(name, &e, 0);

	if (!*e && i < spi_devices_used)
		return i;

	for (i = 0; i < spi_devices_used; i++) {
		if (!strcasecmp(name, spi_devices[i].name))
			return i;
	}

	/* SPI device not found */
	return -1;
}

static void print_spi_info(int index)
{
	uint32_t bits_per_second;

	{
		// Other SPIs have prescaler by power of two 2, 4, 8, ..., 256.
		bits_per_second = spi_clock() / (2 << spi_devices[index].div);
	}

	ccprintf("  %d %s %d bps\n", index, spi_devices[index].name,
		 bits_per_second);

	/* Flush console to avoid truncating output */
	cflush();
}

/*
 * Get information about one or all SPI ports.
 */
static int command_spi_info(int argc, const char **argv)
{
	int i;

	/* If a SPI target is specified, print only that one */
	if (argc == 3) {
		int index = find_spi_by_name(argv[2]);
		if (index < 0) {
			ccprintf("SPI device not found\n");
			return EC_ERROR_PARAM2;
		}

		print_spi_info(index);
		return EC_SUCCESS;
	}

	/* Otherwise print them all */
	for (i = 0; i < spi_devices_used; i++) {
		print_spi_info(i);
	}

	return EC_SUCCESS;
}

static int command_spi_set_speed(int argc, const char **argv)
{
	int index;
	uint32_t desired_speed;
	char *e;
	if (argc < 5)
		return EC_ERROR_PARAM_COUNT;

	index = find_spi_by_name(argv[3]);
	if (index < 0)
		return EC_ERROR_PARAM3;

	desired_speed = strtoi(argv[4], &e, 0);
	if (*e)
		return EC_ERROR_PARAM4;

	{
		int divisor = 7;
		/*
		 * Find the smallest divisor that result in a speed not faster
		 * than what was requested.
		 */
		while (divisor > 0) {
			if (spi_clock() / (2 << (divisor - 1)) >
			    desired_speed) {
				/* One step further would make the clock too
				 * fast, stop here. */
				break;
			}
			divisor--;
		}

		/*
		 * Re-initialize spi controller to apply the new clock divisor.
		 */
		spi_enable(&spi_devices[index], 0);
		spi_devices[index].div = divisor;
		spi_enable(&spi_devices[index], 1);
	}

	return EC_SUCCESS;
}

static int command_spi_set_cs(int argc, const char **argv)
{
	int index;
	int desired_gpio_cs;
	if (argc < 5)
		return EC_ERROR_PARAM_COUNT;

	index = find_spi_by_name(argv[3]);
	if (index < 0)
		return EC_ERROR_PARAM3;

	if (!strcasecmp(argv[4], "default")) {
		desired_gpio_cs = spi_device_default_gpio_cs[index];
	} else {
		desired_gpio_cs = gpio_find_by_name(argv[4]);
		if (desired_gpio_cs == GPIO_COUNT)
			return EC_ERROR_PARAM4;
	}

	spi_devices[index].gpio_cs = desired_gpio_cs;

	return EC_SUCCESS;
}

static int command_spi_set_ready_pin(int argc, const char **argv)
{
	int index;
	int desired_gpio;
	if (argc < 5)
		return EC_ERROR_PARAM_COUNT;

	index = find_spi_by_name(argv[3]);
	if (index < 0)
		return EC_ERROR_PARAM3;

	desired_gpio = gpio_find_by_name(argv[4]);
	if (desired_gpio == GPIO_COUNT)
		return EC_ERROR_PARAM4;

	spi_device_ready_pin[index] = desired_gpio;

	return EC_SUCCESS;
}

static int command_spi_set(int argc, const char **argv)
{
	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[2], "speed"))
		return command_spi_set_speed(argc, argv);
	if (!strcasecmp(argv[2], "cs"))
		return command_spi_set_cs(argc, argv);
	if (!strcasecmp(argv[2], "ready"))
		return command_spi_set_ready_pin(argc, argv);
	return EC_ERROR_PARAM2;
}

static int command_spi(int argc, const char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[1], "info"))
		return command_spi_info(argc, argv);
	if (!strcasecmp(argv[1], "set"))
		return command_spi_set(argc, argv);
	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND_FLAGS(spi, command_spi,
			      "info [PORT]"
			      "\nset speed PORT BPS"
			      "\nset cs PORT PIN"
			      "\nset ready PORT PIN",
			      "SPI bus manipulation", CMD_FLAG_RESTRICTED);

/*
 * Board-specific SPI driver entry point, called by usb_spi.c.
 */
void usb_spi_board_enable(void)
{
	/* All initialization already done in board_init(). */
}

void usb_spi_board_disable(void)
{
}

/*
 * Board-specific SPI driver entry point, called by usb_spi.c.  On this board,
 * every spi device is declared as requiring board specific driver, in order to
 * add enhanced TPM functionality.
 */
int usb_spi_board_transaction_async(const struct spi_device_t *spi_device,
				    uint32_t flash_flags, const uint8_t *txdata,
				    int txlen, uint8_t *rxdata, int rxlen)
{
	if (flash_flags & (FLASH_FLAG_TPM_WAIT_FOR_READY | FLASH_FLAG_TPM)) {
		/* Polling only supported in synchronous function. */
		return USB_SPI_UNSUPPORTED_FLASH_MODE;
	}
	if (flash_flags & FLASH_FLAGS_REQUIRING_SUPPORT) {
		/*
		 * The standard spi_transaction() does not support
		 * any multi-lane modes.
		 */
		return USB_SPI_UNSUPPORTED_FLASH_MODE;
	}
	return spi_transaction_async(spi_device, txdata, txlen, rxdata, rxlen);
}

/*
 * Board-specific SPI driver entry point, called by usb_spi.c.  On this board,
 * every spi device is declared as requiring board specific driver, in order to
 * add enhanced TPM functionality.
 */
int usb_spi_board_transaction_is_complete(const struct spi_device_t *spi_device)
{
	return true;
}

/*
 * Board-specific SPI driver entry point, called by usb_spi.c.  On this board,
 * every spi device is declared as requiring board specific driver, in order to
 * add enhanced TPM functionality.
 */
int usb_spi_board_transaction_flush(const struct spi_device_t *spi_device)
{
	return spi_transaction_flush(spi_device);
}

/*
 * Synchronously perform one TPM transaction, consisting of four byte header
 * followed by a number of data bytes.  Respect TPM protocol by polling when
 * data phase can proceed, as well as optionally wait for edge on Google
 * proprietary "ready signal".
 */
static int usb_spi_tpm_transaction(const struct spi_device_t *spi_device,
				   uint32_t flash_flags, const uint8_t *txdata,
				   int txlen, uint8_t *rxdata, int rxlen)
{
	size_t spi_index = spi_device - spi_devices;
	assert(spi_index < ARRAY_SIZE(spi_devices));
	int gsc_ready_pin = spi_device_ready_pin[spi_index];

	/* TPM protocol has 4-byte command/address. */
	if (txlen < 4)
		return USB_SPI_UNSUPPORTED_FLASH_MODE;

	if (flash_flags & FLASH_FLAG_TPM_WAIT_FOR_READY &&
	    gsc_ready_pin == GPIO_COUNT) {
		/*
		 * Waiting for ready pulse was requested, but ready pin not
		 * declared.
		 */
		return USB_SPI_UNSUPPORTED_FLASH_MODE;
	}

	timestamp_t deadline;
	deadline.val = get_time().val + 100000;

	/* Assert chip select */
	int chip_select_level_before = gpio_get_level(spi_device->gpio_cs);
	gpio_set_level(spi_device->gpio_cs, 0);

	/* Enable polling from fast timer interrupt. */
	if (flash_flags & FLASH_FLAG_TPM_WAIT_FOR_READY)
		start_monitoring_for_falling_edge(gsc_ready_pin);

	uint8_t resp[4];

	/* Send 4-byte TPM header, also receiving ready status. */
	int rv = spi_transaction(spi_device, txdata, 4, resp, -1);

	/* Poll for the TPM standard ready status. */
	while (rv == EC_SUCCESS && resp[3] != 0x01) {
		timestamp_t now = get_time();
		if (timestamp_expired(deadline, &now)) {
			rv = EC_ERROR_TIMEOUT;
			break;
		}
		rv = spi_transaction(spi_device, NULL, 0, resp + 3, 1);
	}

	/* Data phase of the TPM transaction. */
	if (rv == EC_SUCCESS)
		rv = spi_transaction(spi_device, txdata + 4, txlen - 4, rxdata,
				     rxlen);

	/* Release chip select even when returning an error. */
	gpio_set_level(spi_device->gpio_cs, chip_select_level_before);

	/* Optionally wait for Google ready signal. */
	if (flash_flags & FLASH_FLAG_TPM_WAIT_FOR_READY) {
		if (rv == EC_SUCCESS)
			rv = wait_for_falling_edge(deadline);
		stop_monitoring_for_falling_edge();
	}

	return rv;
}

/*
 * Board-specific SPI driver entry point, called by usb_spi.c.  On this board,
 * every spi device is declared as requiring board specific driver, in order to
 * add enhanced TPM functionality.
 */
int usb_spi_board_transaction(const struct spi_device_t *spi_device,
			      uint32_t flash_flags, const uint8_t *txdata,
			      int txlen, uint8_t *rxdata, int rxlen)
{
	if (flash_flags & FLASH_FLAG_TPM) {
		/* Tailored logic for TPM transactions. */
		return usb_spi_tpm_transaction(spi_device, flash_flags, txdata,
					       txlen, rxdata, rxlen);
	}

	int rv = usb_spi_board_transaction_async(spi_device, flash_flags,
						 txdata, txlen, rxdata, rxlen);
	if (rv == EC_SUCCESS) {
		rv = usb_spi_board_transaction_flush(spi_device);
	}
	return rv;
}

/* Reconfigure SPI ports to power-on default values. */
static void spi_reinit(void)
{
	for (unsigned int i = 0; i < spi_devices_used; i++) {
		spi_device_ready_pin[i] = GPIO_COUNT;
		{
			/* "Ordinary" SPI controller */
			spi_enable(&spi_devices[i], 0);
			spi_devices[i].gpio_cs = spi_device_default_gpio_cs[i];
			spi_devices[i].div = spi_device_default_div[i];
			spi_enable(&spi_devices[i], 1);
		}
	}
}
DECLARE_HOOK(HOOK_REINIT, spi_reinit, HOOK_PRIO_DEFAULT);

/* Initialize board for SPI. */
static void spi_init(void)
{
#if 0
	for (unsigned int i = 0; i < spi_devices_used; i++)
		spi_device_ready_pin[i] = GPIO_COUNT;

	/* Record initial values for use by `spi_reinit()` above. */
	for (unsigned int i = 0; i < spi_devices_used; i++) {
		spi_device_default_gpio_cs[i] = spi_devices[i].gpio_cs;
		spi_device_default_div[i] = spi_devices[i].div;
	}

	/* Structured endpoints */
	usb_spi_enable(1);

	/* Configure SPI GPIOs */
	gpio_config_module(MODULE_SPI, 1);

	/*
	 * Unlike most SPI, I2C and UARTs, which are configured in their
	 * alternate mode by default, SPI1 pins are in GPIO input mode on
	 * HyperDebug power-on, for compatibility with previous firmwares.  In
	 * the future we may decide to leave even more functions off by default,
	 * in order for HyperDebug to actively drive as little at possible on
	 * boot.  It is relatively straightforward to declare pins as "Alternate
	 * mode" in opentitantool json configuration file, to have them enabled
	 * by "transport init".
	 *
	 * The code below sets up the alternate function "number" for the
	 * relevant pins, such that when alternate mode is enabled on the pins,
	 * the result is the particular alternate function that HyperDebug
	 * firmware has chosen for the pin.
	 */
	STM32_GPIO_AFRL(STM32_GPIOA_BASE) |= 0x55000000; /* SPI1: PA6/PA7
							    HIDO/HODI */
	STM32_GPIO_AFRL(STM32_GPIOB_BASE) |= 0x00005000; /* SPI1: PB3 SCK */

	/*
	 * Enable SPI1.
	 */

	/* Enable clocks to SPI1 module */
	STM32_RCC_APB2ENR |= STM32_RCC_APB2ENR_SPI1EN;

	/* Reset SPI1 */
	STM32_RCC_APB2RSTR |= STM32_RCC_APB2RSTR_SPI1RST;
	STM32_RCC_APB2RSTR &= ~STM32_RCC_APB2RSTR_SPI1RST;

	spi_enable(&spi_devices[2], 1);

	/*
	 * Enable SPI2.
	 */

	/* Enable clocks to SPI2 module */
	STM32_RCC_APB1ENR |= STM32_RCC_APB1ENR_SPI2EN;

	/* Reset SPI2 */
	STM32_RCC_APB1RSTR |= STM32_RCC_APB1RSTR_SPI2RST;
	STM32_RCC_APB1RSTR &= ~STM32_RCC_APB1RSTR_SPI2RST;

	spi_enable(&spi_devices[0], 1);
#endif
}
DECLARE_HOOK(HOOK_INIT, spi_init, HOOK_PRIO_DEFAULT + 1);

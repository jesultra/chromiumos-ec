/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "config.h"
#include "console.h"
#include "link_defs.h"
#include "printf.h"
#include "queue.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "usb_api.h"
#include "usb_descriptor.h"
#include "usb_hw.h"
#include "util.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ##args)
#define USB_CONSOLE_TIMEOUT_US (30 * MSEC)

static struct queue const tx_q =
	QUEUE_NULL(CONFIG_USB_CONSOLE_TX_BUF_SIZE, uint8_t);
static struct queue const rx_q = QUEUE_NULL(USB_MAX_PACKET_SIZE, uint8_t);

static int last_tx_ok = 1;

static int is_reset;
static int is_enabled = 1;
static int is_readonly;

/* USB-Serial descriptors */
const struct usb_interface_descriptor USB_IFACE_DESC(USB_IFACE_CONSOLE) = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = USB_IFACE_CONSOLE,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass = USB_SUBCLASS_GOOGLE_SERIAL,
	.bInterfaceProtocol = USB_PROTOCOL_GOOGLE_SERIAL,
	.iInterface = USB_STR_CONSOLE_NAME,
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_CONSOLE, 0) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x80 | USB_EP_CONSOLE,
	.bmAttributes = 0x02 /* Bulk IN */,
	.wMaxPacketSize = USB_MAX_PACKET_SIZE,
	.bInterval = 10
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_CONSOLE, 1) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_EP_CONSOLE,
	.bmAttributes = 0x02 /* Bulk OUT */,
	.wMaxPacketSize = USB_MAX_PACKET_SIZE,
	.bInterval = 0
};

/* Forward declaration */
static void handle_output(void);

static void con_ep_tx(void)
{
	/* Check bytes in the FIFO needed to transmitted */
	handle_output();
}

uint8_t sie_command_read1(uint8_t cmd);

static void con_ep_rx(void)
{
	LPC_USB_CTRL = BIT(0) | (USB_EP_CONSOLE << 2);
	(void)LPC_USB_DEV_INT_ST;
	(void)LPC_USB_DEV_INT_ST;
	uint16_t rx = LPC_USB_RX_PLEN;
	if (!(rx & 0x0400)) {
		ccprintf("  Odd, rx_len: 0x%04x\n", rx);
		return;
	}
	int len = rx & 0x03FF;
	for (int i = 0; i < len; i += 4) {
		uint32_t val = LPC_USB_RX_DATA;
		for (int j = 0; j < 4 && j + i < len; j++) {
			uint8_t byte = val >> (j * 8);
			queue_add_unit(&rx_q, &byte);
		}
	}
	sie_command_read1(0xF2);

	/* wake-up the console task */
	console_has_input();
}

static void ep_event(enum usb_ep_event evt)
{
	if (evt != USB_EVENT_RESET)
		return;
#if 0
	btable_ep[USB_EP_CONSOLE].tx_addr = usb_sram_addr(ep_buf_tx);
	btable_ep[USB_EP_CONSOLE].tx_count = 0;

	btable_ep[USB_EP_CONSOLE].rx_addr = usb_sram_addr(ep_buf_rx);
	btable_ep[USB_EP_CONSOLE].rx_count =
		0x8000 | ((USB_MAX_PACKET_SIZE / 32 - 1) << 10);

	STM32_USB_EP(USB_EP_CONSOLE) =
		(USB_EP_CONSOLE | /* Endpoint Addr */
		 (2 << 4) | /* TX NAK        */
		 (0 << 9) | /* Bulk EP       */
		 (is_readonly ? EP_RX_NAK : EP_RX_VALID));
#endif
	is_reset = 1;
}

USB_DECLARE_EP(USB_EP_CONSOLE, con_ep_tx, con_ep_rx, ep_event);

static int __tx_char(void *context, int c)
{
	/* Do newline to CRLF translation */
	if (c == '\n' && __tx_char(context, '\r'))
		return 1;

	/* Return 0 on success */
	return !QUEUE_ADD_UNITS(&tx_q, &c, 1);
}

static inline int usb_console_tx_valid(void)
{
#if 0
	return (STM32_USB_EP(USB_EP_CONSOLE) & EP_TX_MASK) == EP_TX_VALID;
#endif
	return false;
}

static int usb_wait_console(void)
{
	timestamp_t deadline = get_time();
	int wait_time_us = 1;

	if (!is_enabled || !usb_is_enabled())
		return EC_SUCCESS;

	deadline.val += USB_CONSOLE_TIMEOUT_US;

	/*
	 * If the USB console is not used, Tx buffer would never free up.
	 * In this case, let's drop characters immediately instead of sitting
	 * for some time just to time out. On the other hand, if the last
	 * Tx is good, it's likely the host is there to receive data, and
	 * we should wait so that we don't clobber the buffer.
	 */
	if (last_tx_ok) {
		while (usb_console_tx_valid() || !is_reset) {
			if (timestamp_expired(deadline, NULL)) {
				last_tx_ok = 0;
				return EC_ERROR_TIMEOUT;
			}
			if (wait_time_us < MSEC)
				udelay(wait_time_us);
			else
				crec_usleep(wait_time_us);
			wait_time_us *= 2;
		}

		return EC_SUCCESS;
	} else {
		last_tx_ok = !usb_console_tx_valid();
		return EC_SUCCESS;
	}
}

uint8_t sie_command_read1(uint8_t cmd);
void sie_command_nodata(uint8_t cmd);

/* Try to send some bytes from the Tx FIFO to the host */
static void tx_fifo_handler(void)
{
	int ret;
	//usb_uint *buf = (usb_uint *)ep_buf_tx;

	if (!is_reset)
		return;

	ret = usb_wait_console();
	if (ret)
		return;

	int len = MIN(64, queue_count(&tx_q));
	if (!len)
		return;

	interrupt_disable();
	// Attempt getting write buffer for USB_EP_CONSOLE
	sie_command_read1(0x41 + 2 * USB_EP_CONSOLE);

	LPC_USB_CTRL = BIT(1) | (USB_EP_CONSOLE << 2);
	(void)LPC_USB_DEV_INT_ST;
	(void)LPC_USB_DEV_INT_ST;
	LPC_USB_TX_PLEN = len;

	for (int i = 0; i < len; i += 4) {
		uint32_t val = 0;
		for (int j = 0; j < 4 && i + j < len; j++) {
			uint8_t byte;
			if (!queue_remove_unit(&tx_q, &byte)) {
				*(int*)0 = 0;
			}
			val |= byte << (j * 8);
		}
		LPC_USB_TX_DATA = val;
	}

	sie_command_nodata(0xFA);
	interrupt_enable();
}
DECLARE_DEFERRED(tx_fifo_handler);

static void handle_output(void)
{
	/* Wake up the Tx FIFO handler */
	hook_call_deferred(&tx_fifo_handler_data, 0);
}

/*
 * Public USB console implementation below.
 */
int usb_getc(void)
{
	int c = 0;

	if (!is_enabled)
		return -1;

	if (!QUEUE_REMOVE_UNITS(&rx_q, &c, 1))
		return -1;

	return c;
}

int usb_putc(int c)
{
	int ret;

	ret = __tx_char(NULL, c);
	handle_output();

	return ret;
}

int usb_puts(const char *outstr)
{
	/* Put all characters in the output buffer */
	while (*outstr) {
		if (__tx_char(NULL, *outstr++) != 0)
			break;
	}
	handle_output();

	/* Successful if we consumed all output */
	return *outstr ? EC_ERROR_OVERFLOW : EC_SUCCESS;
}

int usb_vprintf(const char *format, va_list args)
{
	int ret;

	ret = vfnprintf(__tx_char, NULL, format, args);
	handle_output();

	return ret;
}

void usb_console_enable(int enabled, int readonly)
{
	is_enabled = enabled;
	is_readonly = readonly;
}

int usb_console_tx_blocked(void)
{
	return is_enabled && usb_console_tx_valid();
}

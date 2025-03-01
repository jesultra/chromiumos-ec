/* Copyright 2025 The ChromiumOS Authors
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

/*
 * The CMSIS-DAP specification calls for identifying the USB interface by
 * looking for "CMSIS-DAP" in the string name, not by subclass/protocol.
 */
#define USB_SUBCLASS_CMSIS_DAP 0x00
#define USB_PROTOCOL_CMSIS_DAP 0x00

/* CMSIS-DAP command bytes */
enum cmsis_dap_command_t {
	/* General commands */
	DAP_Info = 0x00,
	DAP_HostStatus = 0x01,
	DAP_Connect = 0x02,
	DAP_Disconnect = 0x03,
	DAP_TransferConfigure = 0x04,
	DAP_Transfer = 0x05,
	DAP_TransferBlock = 0x06,
	DAP_TransferAbort = 0x07,
	DAP_WriteAbort = 0x08,
	DAP_Delay = 0x09,
	DAP_ResetTarget = 0x0A,

	/* Commands used both for SWD and JTAG */
	DAP_SWJ_Pins = 0x10,
	DAP_SWJ_Clock = 0x11,
	DAP_SWJ_Sequence = 0x12,

	/* Commands used only with SWD */
	DAP_SWD_Configure = 0x13,

	/* Commands used only with JTAG */
	DAP_JTAG_Sequence = 0x14,
	DAP_JTAG_Configure = 0x15,
	DAP_JTAG_IdCode = 0x16,

	/* Commands used for UART tunnelling */
	DAP_SWO_Transport = 0x17,
	DAP_SWO_Mode = 0x18,
	DAP_SWO_Baudrate = 0x19,
	DAP_SWO_Control = 0x1A,
	DAP_SWO_Status = 0x1B,
	DAP_SWO_Data = 0x1C,

	/* Commands used to group other commands */
	DAP_QueueCommands = 0x7E,
	DAP_ExecuteCommands = 0x7F,

	/* Vendor-specific commands (reserved range 0x80 - 0x9F) */
	DAP_GOOG_Info = 0x80,
	DAP_GOOG_I2c = 0x81,
	DAP_GOOG_I2cDevice = 0x82,
	DAP_GOOG_Gpio = 0x83,
};

/* DAP Status Code */
enum cmsis_dap_status_t {
	STATUS_Ok = 0x00,
	STATUS_Error = 0xFF,
};

/* Parameter for info command */
enum cmsis_dap_info_subcommand_t {
	INFO_Vendor = 0x01,
	INFO_Product = 0x02,
	INFO_Serial = 0x03,
	INFO_Version = 0x04,
	INFO_DeviceVendor = 0x05,
	INFO_DeviceName = 0x06,
	INFO_Capabilities = 0xF0,
	INFO_SwoBufferSize = 0xFD,
	INFO_PacketCount = 0xFE,
	INFO_PacketSize = 0xFF,
};

/* Bitfield response to INFO_Capabilities */
const uint16_t CAP_Swd = BIT(0);
const uint16_t CAP_Jtag = BIT(1);
const uint16_t CAP_SwoUart = BIT(2);
const uint16_t CAP_SwoManchester = BIT(3);
const uint16_t CAP_AtomicCommands = BIT(4);
const uint16_t CAP_TestDomainTimer = BIT(5);
const uint16_t CAP_SwoStreamingTrace = BIT(6);
const uint16_t CAP_UartCommunicationPort = BIT(7);
const uint16_t CAP_UsbComPort = BIT(8);

enum connect_req_t {
	CONN_REQ_Default = 0,
	CONN_REQ_Swd = 1,
	CONN_REQ_Jtag = 2,
};

enum connect_resp_t {
	CONN_RESP_Failed = 0,
	CONN_RESP_Swd = 1,
	CONN_RESP_Jtag = 2,
};

/* Parameter for vendor (Google) info command */
enum goog_info_subcommand_t {
	GOOG_INFO_Capabilities = 0x00,
};

/* Bitfield response to vendor (Google) capabities request */
const uint32_t GOOG_CAP_I2c = BIT(0);
const uint32_t GOOG_CAP_I2cDevice = BIT(1);
const uint32_t GOOG_CAP_GpioMonitoring = BIT(2);
const uint32_t GOOG_CAP_GpioBitbanging = BIT(3);
/* This bit indicates support for a particular UART USB control request */
const uint32_t GOOG_CAP_UartClearQueue = BIT(4);
/* This bit indicates support for SPI and I2C polling for TPM ready. */
const uint32_t GOOG_CAP_TpmPoll = BIT(5);

/* Bitfield used in DAP_SWJ_Pins request */
const uint8_t PIN_SwClk_Tck = 0x01;
const uint8_t PIN_SwDio_Tms = 0x02;
const uint8_t PIN_Tdi = 0x04;
const uint8_t PIN_Tdo = 0x08;
const uint8_t PIN_Trst = 0x20;
const uint8_t PIN_Reset = 0x80;

/* Bitfield used in DAP_JTAG_Sequence request */
const uint8_t SEQ_NumBits = 0x3F;
const uint8_t SEQ_Tms = 0x40;
const uint8_t SEQ_CaptureTdo = 0x80;

/*
 * Incoming and outgoing byte streams.
 */

struct queue const cmsis_dap_tx_queue;
struct queue const cmsis_dap_rx_queue;

uint8_t rx_buffer[256];
uint8_t tx_buffer[256];

/*
 * JTAG state
 */
static bool jtag_enabled = false;

void cmsis_dap_queue_blocking_add(const void *src, size_t count)
{
	while (!cmsis_dap_unwind_requested()) {
		size_t progress =
			queue_add_units(&cmsis_dap_tx_queue, src, count);
		src += progress;
		if (progress >= count)
			return;
		count -= progress;
		/*
		 * Wait for queue consumer to wake up this task, when there is
		 * more room in the queue.
		 */
		task_wait_event(0);
	}
}

void cmsis_dap_queue_blocking_remove(void *dest, size_t count)
{
	while (!cmsis_dap_unwind_requested()) {
		size_t progress =
			queue_remove_units(&cmsis_dap_rx_queue, dest, count);
		dest += progress;
		if (progress >= count)
			return;
		count -= progress;
		/*
		 * Wait for queue producer to wake up this task, when there is
		 * more data in the queue.
		 */
		task_wait_event(0);
	}
}

/*
 * Implementation of handler routines for each CMSIS-DAP command.
 */

/* Info command, used to discover which other commands are supported. */
static void cmsis_dap_info(size_t peek_c)
{
	const char *CMSIS_DAP_VERSION_STR = "2.1.1";
	const uint16_t CAPABILITIES =
#ifdef CONFIG_USB_CMSIS_DAP_JTAG
		CAP_Jtag |
#endif
		0;
	int i;

	if (peek_c < 2)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 2);
	switch (rx_buffer[1]) {
#ifdef CONFIG_USB_SERIALNO
	case INFO_Serial: {
		struct usb_string_desc *sd = usb_serialno_desc;
		for (i = 0; i < CONFIG_SERIALNO_LEN && sd->_data[i]; i++)
			tx_buffer[2 + i] = sd->_data[i];
		tx_buffer[1] = i;
		queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2 + i);
		break;
	}
#endif
	case INFO_Version:
		tx_buffer[1] = strlen(CMSIS_DAP_VERSION_STR) + 1;
		memcpy(tx_buffer + 2, CMSIS_DAP_VERSION_STR, tx_buffer[1]);
		queue_add_units(&cmsis_dap_tx_queue, tx_buffer,
				2 + tx_buffer[1]);
		break;
	case INFO_Capabilities:
		tx_buffer[1] = sizeof(CAPABILITIES);
		memcpy(tx_buffer + 2, &CAPABILITIES, sizeof(CAPABILITIES));
		queue_add_units(&cmsis_dap_tx_queue, tx_buffer,
				2 + tx_buffer[1]);
		break;
	default:
		ccprintf("Unknown info request %02x\n", rx_buffer[1]);
		tx_buffer[1] = 0;
		queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
		break;
	}
}

/* Informational command, to allow debugging device to indicate status. */
static void cmsis_dap_host_status(size_t peek_c)
{
	if (peek_c < 3)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 3);
	tx_buffer[1] = STATUS_Ok;
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
}

/* Establish JTAG connection, take control of JTAG pins. */
static void cmsis_dap_connect(size_t peek_c)
{
	if (peek_c < 2)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 2);
	switch (rx_buffer[1]) {
	case CONN_REQ_Default:
	case CONN_REQ_Jtag:
		tx_buffer[1] = CONN_RESP_Jtag;
		if (!jtag_enabled) {
			jtag_enabled = true;
			cmsis_dap_enable_jtag_pins();
		}
		break;
	default:
		tx_buffer[1] = CONN_RESP_Failed;
	}
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
}

/* Restore JTAG pins to previous configuration. */
static void cmsis_dap_disconnect(size_t peek_c)
{
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 1);

	if (jtag_enabled) {
		jtag_enabled = false;
		cmsis_dap_disable_jtag_pins();
	}

	tx_buffer[1] = STATUS_Ok;
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
}

#ifdef CONFIG_USB_CMSIS_DAP_JTAG
/* Configure parameters for DAP_Transfer family of requests. */
static void cmsis_dap_transfer_configure(size_t peek_c)
{
	if (peek_c < 6)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 6);

	/*
	 * This file does not offer support for the DAP_Transfer family of
	 * requests, and OpenOCD does not seem to issue any requests (at least
	 * not when operating on a RISC-V OpenTitan code.
	 *
	 * OpenOCD still sends this configuration request as part of its setup
	 * sequence, we can safely ignore the parameters given, and report
	 * success to the caller.
	 */

	tx_buffer[1] = STATUS_Ok;
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
}
#endif

/* Reset the GSC (using same pin as if blue button was pressed). */
static void cmsis_dap_reset_target(size_t peek_c)
{
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 1);

	if (GPIO_JTAG_RESET != GPIO_COUNT) {
		gpio_set_level(GPIO_JTAG_RESET, false);
		crec_usleep(100000);
		gpio_set_level(GPIO_JTAG_RESET, true);
		tx_buffer[2] = 1;
	} else {
		tx_buffer[2] = 0;
	}
	tx_buffer[1] = STATUS_Ok;
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 3);
}

/* One-time setting of the output level of each JTAG signal. */
static void cmsis_dap_swj_pins(size_t peek_c)
{
	if (peek_c < 7)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 7);

	uint8_t pin_value = rx_buffer[1];
	uint8_t pin_mask = rx_buffer[2];
	uint32_t wait_us;
	memcpy(&wait_us, rx_buffer + 3, sizeof(wait_us));

	if ((pin_mask & PIN_SwClk_Tck))
		gpio_set_level(GPIO_JTAG_TCLK, !!(pin_value & PIN_SwClk_Tck));
	if ((pin_mask & PIN_SwDio_Tms))
		gpio_set_level(GPIO_JTAG_TMS, !!(pin_value & PIN_SwDio_Tms));
	if ((pin_mask & PIN_Tdi) && GPIO_JTAG_TDI != GPIO_COUNT)
		gpio_set_level(GPIO_JTAG_TDI, !!(pin_value & PIN_Tdi));
	if ((pin_mask & PIN_Trst) && GPIO_JTAG_TRST != GPIO_COUNT)
		gpio_set_level(GPIO_JTAG_TRST, !!(pin_value & PIN_Trst));
	if ((pin_mask & PIN_Reset) && GPIO_JTAG_RESET != GPIO_COUNT)
		gpio_set_level(GPIO_JTAG_RESET, !!(pin_value & PIN_Reset));

	crec_usleep(wait_us);

	tx_buffer[1] =
		(gpio_get_level(GPIO_JTAG_TCLK) ? PIN_SwClk_Tck : 0) |
		(gpio_get_level(GPIO_JTAG_TMS) ? PIN_SwDio_Tms : 0) |
		(GPIO_JTAG_TDI == GPIO_COUNT ?
			 0 :
			 (gpio_get_level(GPIO_JTAG_TDI) ? PIN_Tdi : 0)) |
		(GPIO_JTAG_TDO == GPIO_COUNT ?
			 0 :
			 (gpio_get_level(GPIO_JTAG_TDO) ? PIN_Tdo : 0)) |
		(GPIO_JTAG_TRST == GPIO_COUNT ?
			 PIN_Trst :
			 (gpio_get_level(GPIO_JTAG_TRST) ? PIN_Trst : 0)) |
		(GPIO_JTAG_RESET == GPIO_COUNT ?
			 PIN_Reset :
			 (gpio_get_level(GPIO_JTAG_RESET) ? PIN_Reset : 0));
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
}

/* Set JTAG clock frequency. */
static void cmsis_dap_swj_clock(size_t peek_c)
{
	uint32_t new_clock_hz;

	if (peek_c < 5)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 5);

	memcpy(&new_clock_hz, rx_buffer + 1, sizeof(new_clock_hz));

	if (!new_clock_hz)
		tx_buffer[1] = STATUS_Error;
	else if (cmsis_dap_set_period(new_clock_hz) != EC_SUCCESS)
		tx_buffer[1] = STATUS_Error;
	else
		tx_buffer[1] = STATUS_Ok;

	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
}

/* Clock data out on TMS. */
static void cmsis_dap_swj_sequence(size_t peek_c)
{
	if (peek_c < 2)
		return;
	unsigned int bit_count = rx_buffer[1] == 0 ? 256 : rx_buffer[1];
	unsigned c = queue_count(&cmsis_dap_rx_queue);
	if (c < 2 + (bit_count + 7) / 8)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, c);
	for (unsigned int i = 0; i < bit_count; i++) {
		gpio_set_level(GPIO_JTAG_TMS,
			       !!(rx_buffer[2 + i / 8] & (1 << (i % 8))));
		gpio_set_level(GPIO_JTAG_TCLK, false);
		cmsis_dap_half_clock_delay();
		gpio_set_level(GPIO_JTAG_TCLK, true);
		cmsis_dap_half_clock_delay();
	}
	tx_buffer[1] = STATUS_Ok;
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, 2);
}

#ifdef CONFIG_USB_CMSIS_DAP_JTAG
/*
 * Do a JTAG transaction, consisting of one or more sequences of clocking data
 * on TDI (between 1 and 64 bits), while keeping TMS at a particular level.
 */
static void cmsis_dap_jtag_sequence(size_t peek_c)
{
	if (peek_c < 3)
		return;
	int c = queue_count(&cmsis_dap_rx_queue);

	/* Check whether a complete request is in queue. */
	queue_peek_units(&cmsis_dap_rx_queue, rx_buffer, 0, c);
	size_t num_sequences = rx_buffer[1];
	size_t offset = 2;
	for (size_t i = 0; i < num_sequences; i++) {
		uint8_t header = rx_buffer[offset];
		unsigned int bit_count = header & 0x3F;
		if (bit_count == 0)
			bit_count = 0x40;
		offset += 1 + (bit_count + 7) / 8;
		if (offset > c) {
			/* We do not yet have all bytes of the request. */
			return;
		}
	}

	/* We have a complete request, mark as removed from the queue. */
	queue_advance_head(&cmsis_dap_rx_queue, offset);
	/* Prepare output buffer for being populated one bit at a time. */
	memset(tx_buffer + 1, 0, sizeof(tx_buffer) - 1);

	/*
	 * Iterate over the list of "sequences", each having a one-byte header
	 * specifying how many bits in the sequence, what the value of TMS
	 * during this sequence, and whether to record TDO during this sequence.
	 */
	const uint8_t *ptr = rx_buffer + 2;
	uint8_t *tx_ptr = tx_buffer + 2;
	const uint8_t *const end = rx_buffer + offset;
	while (ptr < end) {
		/* Consume and decode header byte for this one "sequence". */
		uint8_t header = *ptr++;
		gpio_set_level(GPIO_JTAG_TMS, header & SEQ_Tms);
		bool capture_tdo = !!(header & SEQ_CaptureTdo);
		unsigned int bit_count = (((header - 1) & SEQ_NumBits) + 1);

		/*
		 * With TMS set at a given value, clock 1 - 64 bits of data on
		 * TDI/TDO.
		 */
		for (unsigned int i = 0; i < bit_count; i++) {
			gpio_set_level(GPIO_JTAG_TDI,
				       ptr[i / 8] & (1 << (i % 8)));
			gpio_set_level(GPIO_JTAG_TCLK, false);
			cmsis_dap_half_clock_delay();
			uint32_t tdo_val = gpio_get_level(GPIO_JTAG_TDO);
			if (capture_tdo)
				tx_ptr[i / 8] |= tdo_val << (i % 8);
			gpio_set_level(GPIO_JTAG_TCLK, true);
			cmsis_dap_half_clock_delay();
		}
		/* Consume the data bytes of this one "sequence". */
		ptr += (bit_count + 7) / 8;
		if (capture_tdo)
			tx_ptr += (bit_count + 7) / 8;
	}

	tx_buffer[1] = STATUS_Ok;
	queue_add_units(&cmsis_dap_tx_queue, tx_buffer, tx_ptr - tx_buffer);
}
#endif

/* Vendor command (HyperDebug): Discover Google-specific capabilities. */
static void cmsis_dap_goog_info(size_t peek_c)
{
	const uint16_t CAPABILITIES =
#ifdef CONFIG_USB_CMSIS_DAP_BOARD_I2C
		/* Support for I2C tunneled through CMSIS-DAP. */
		GOOG_CAP_I2c |
#endif
#ifdef CONFIG_USB_CMSIS_DAP_BOARD_I2C_DEVICE
		/* Support for acting as I2C device. */
		GOOG_CAP_I2cDevice |
#endif
#ifdef CONFIG_USB_CMSIS_DAP_BOARD_GPIO
		/* Support for advanced GPIO streaming operations. */
		GOOG_CAP_GpioMonitoring | GOOG_CAP_GpioBitbanging |
#endif
#ifdef CONFIG_USB_CMSIS_DAP_TPM_POLL
		/* Support for TPM primitives via SPI and/or I2C. */
		GOOG_CAP_TpmPoll |
#endif
#ifdef CONFIG_USB_CMSIS_DAP_UART_CLEAR_QUEUE
		/* Support for USB_USART_CLEAR_QUEUES in usb-stream.c . */
		GOOG_CAP_UartClearQueue |
#endif
		0;

	if (peek_c < 2)
		return;
	queue_remove_units(&cmsis_dap_rx_queue, rx_buffer, 2);
	switch (rx_buffer[1]) {
	case GOOG_INFO_Capabilities:
		tx_buffer[1] = sizeof(CAPABILITIES);
		memcpy(tx_buffer + 2, &CAPABILITIES, sizeof(CAPABILITIES));
		queue_add_units(&cmsis_dap_tx_queue, tx_buffer,
				2 + tx_buffer[1]);
		break;
	}
}

/* Map from CMSIS-DAP command byte to handler routine. */
static void (*dispatch_table[256])(size_t peek_c) = {
	[DAP_Info] = cmsis_dap_info,
	[DAP_HostStatus] = cmsis_dap_host_status,
	[DAP_Connect] = cmsis_dap_connect,
	[DAP_Disconnect] = cmsis_dap_disconnect,
	[DAP_ResetTarget] = cmsis_dap_reset_target,

#ifdef CONFIG_USB_CMSIS_DAP_JTAG
	[DAP_TransferConfigure] = cmsis_dap_transfer_configure,
	[DAP_SWJ_Pins] = cmsis_dap_swj_pins,
	[DAP_SWJ_Clock] = cmsis_dap_swj_clock,
	[DAP_SWJ_Sequence] = cmsis_dap_swj_sequence,
	[DAP_JTAG_Sequence] = cmsis_dap_jtag_sequence,
#endif

	/* Google extensions to CMSIS-DAP protocol. */
	[DAP_GOOG_Info] = cmsis_dap_goog_info,
#ifdef CONFIG_USB_CMSIS_DAP_BOARD_I2C
	[DAP_GOOG_I2c] = cmsis_dap_goog_i2c,
#endif
#ifdef CONFIG_USB_CMSIS_DAP_BOARD_I2C_DEVICE
	[DAP_GOOG_I2cDevice] = cmsis_dap_goog_i2c_device,
#endif
#ifdef CONFIG_USB_CMSIS_DAP_BOARD_GPIO
	[DAP_GOOG_Gpio] = cmsis_dap_goog_gpio,
#endif
};

/* Dispatch incoming request according to table above. */
static void cmsis_dap_dispatch(void)
{
	/* Peek at the incoming data. */
	size_t peek_c = queue_peek_units(&cmsis_dap_rx_queue, rx_buffer, 0, 8);
	if (peek_c < 1) {
		/* Not enough data to start decoding request. */
		return;
	}

	if (dispatch_table[rx_buffer[0]]) {
		/* First byte of response is always same as command byte. */
		tx_buffer[0] = rx_buffer[0];
		/* Invoke handler routine. */
		dispatch_table[rx_buffer[0]](peek_c);
		/* Trigger sending of response. */
		queue_flush(&cmsis_dap_tx_queue);
	} else {
		/*
		 * Unrecognized command.  The CMSIS-DAP protocol does not allow
		 * us to know the size of the data of a command in general, nor
		 * is there any command-independent means for sending "not
		 * understood".  The code below discards all queued incoming
		 * data, and sends no reply. */
		queue_advance_head(&cmsis_dap_rx_queue,
				   queue_count(&cmsis_dap_rx_queue));
	}
}

/*
 * If cmsis_dap_unwind_requested_by is any value other than TASK_ID_INVALID,
 * it means that the given task (typically HOOKS or CONSOLE) has requested
 * that the CMSIS_DAP task abort any partially received request or partially
 * sent response, and return to its main loop ASAP.  Before setting
 * cmsis_dap_unwind_requested_by, the unwind_mutex must be locked by the
 * requesting task must, and it must be held until the CMSIS_DAP task has
 * acknowledged by writing TASK_ID_INVALID again.
 *
 * The CMSIS_DAP task only writes to cmsis_dap_unwind_requested_by if it sees
 * a value other than TASK_ID_INVALID, and in that case, we know that no other
 * task could be overwriting, due to the convention above.
 */
static volatile task_id_t cmsis_dap_unwind_requested_by = TASK_ID_INVALID;

static K_MUTEX_DEFINE(unwind_mutex);

bool cmsis_dap_unwind_requested(void)
{
	return cmsis_dap_unwind_requested_by != TASK_ID_INVALID;
}

/*
 * Main entry point for handling CMSIS-DAP requests received via USB.
 */
void cmsis_dap_task(void *unused)
{
	/*
	 * Signal that the consumer is allowed to buffer characters
	 * indefinitely.  `queue_flush()` will be invoked by
	 * `cmsis_dap_dispatch()` after processing each command, to ensure that
	 * the complete response is sent via USB.
	 */
	queue_enable_buffered_mode(&cmsis_dap_tx_queue);

	while (true) {
		/*
		 * If another task has requested unwinding, we can now report
		 * that the CMSIS task has returned to main loop.
		 */
		if (cmsis_dap_unwind_requested()) {
			task_id_t requesting_task =
				cmsis_dap_unwind_requested_by;
			cmsis_dap_unwind_requested_by = TASK_ID_INVALID;
			task_wake(requesting_task);
		}

		/* Wait for cmsis_dap_written() to wake up this task. */
		task_wait_event(0);
		if (cmsis_dap_unwind_requested())
			continue;

		/* Dispatch CMSIS request, if fully received. */
		cmsis_dap_dispatch();
	}
}

/*
 * Declare USB interface for CMSIS-DAP.
 */
USB_STREAM_CONFIG_FULL(cmsis_dap_usb, USB_IFACE_CMSIS_DAP,
		       USB_CLASS_VENDOR_SPEC, USB_SUBCLASS_CMSIS_DAP,
		       USB_PROTOCOL_CMSIS_DAP, USB_STR_CMSIS_DAP_NAME,
		       USB_EP_CMSIS_DAP, USB_MAX_PACKET_SIZE,
		       USB_MAX_PACKET_SIZE, cmsis_dap_rx_queue,
		       cmsis_dap_tx_queue, 0, 1);

void cmsis_dap_reinit(void)
{
	mutex_lock(&unwind_mutex);

	/* Discard any partial data in the inbound queue. */
	usb_stream_clear_rx(&cmsis_dap_usb);

	/*
	 * Cause the CMSIS task to unwind any method blocked on receiving or
	 * sending more data.  Then wait for the task to finish unwinding.
	 */
	cmsis_dap_unwind_requested_by = task_get_current();
	task_wake(TASK_ID_CMSIS_DAP);
	do {
		if (TASK_EVENT_TIMER & task_wait_event(10000)) {
			panic("CMSIS-DAP task is stuck");
		}
	} while (cmsis_dap_unwind_requested_by != TASK_ID_INVALID);

	/* Discard any partial responses in the outgoing queue. */
	usb_stream_clear_tx(&cmsis_dap_usb);

	if (jtag_enabled) {
		jtag_enabled = false;
		cmsis_dap_disable_jtag_pins();
	}

	mutex_unlock(&unwind_mutex);
}

static void cmsis_dap_written(struct consumer const *consumer, size_t count)
{
	task_wake(TASK_ID_CMSIS_DAP);
}

struct consumer_ops const cmsis_dap_consumer_ops = {
	.written = cmsis_dap_written,
};

struct consumer const cmsis_dap_consumer = {
	.queue = &cmsis_dap_rx_queue,
	.ops = &cmsis_dap_consumer_ops,
};

static void cmsis_dap_read(struct producer const *producer, size_t count)
{
	task_wake(TASK_ID_CMSIS_DAP);
}

struct producer_ops const cmsis_dap_producer_ops = {
	.read = cmsis_dap_read,
};

struct producer const cmsis_dap_producer = {
	.queue = &cmsis_dap_tx_queue,
	.ops = &cmsis_dap_producer_ops,
};

struct queue const cmsis_dap_tx_queue = QUEUE_DIRECT(
	sizeof(tx_buffer), uint8_t, cmsis_dap_producer, cmsis_dap_usb.consumer);

struct queue const cmsis_dap_rx_queue = QUEUE_DIRECT(
	sizeof(rx_buffer), uint8_t, cmsis_dap_usb.producer, cmsis_dap_consumer);

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
 *
 * If configured to allow I2C tunnelling via a CMSIS "vendor request", then we
 * advertise Google I2C subclass, with a protocol version distinct from the
 * traditional Google I2C interface, in order to allow servod to easily
 * recognize either.
 */
#if defined(CONFIG_USB_CMSIS_DAP_I2C) || defined(CONFIG_USB_CMSIS_DAP_BOARD_I2C)
#define USB_SUBCLASS_CMSIS_DAP USB_SUBCLASS_GOOGLE_I2C
#define USB_PROTOCOL_CMSIS_DAP USB_PROTOCOL_GOOGLE_I2C_VIA_CMSIS_DAP
#else
#define USB_SUBCLASS_CMSIS_DAP 0x00
#define USB_PROTOCOL_CMSIS_DAP 0x00
#endif

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

/* Bitfield used for SWD device ack response */
const uint8_t SWD_ACK_Ok = 0x01;
const uint8_t SWD_ACK_Wait = 0x02;
const uint8_t SWD_ACK_Fault = 0x04;
const uint8_t SWD_ACK_ParityError = 0x08;

/*
 * Incoming and outgoing byte streams.
 */

struct queue const cmsis_dap_tx_queue;
struct queue const cmsis_dap_rx_queue;

/*
 * JTAG state
 */
static bool jtag_enabled = false;

#ifdef CONFIG_USB_CMSIS_DAP_SWD
/* Turnaround clock period of the SWD device. */
static uint8_t swd_turn_cycles;
/*
 * false: Do not generate Data Phase on WAIT/FAULT (default).
 * true: Always generate Data Phase (also on WAIT/FAULT; Required for
 *       Sticky Overrun behavior).
 */
static bool swd_data_on_wait_or_fault;
/* Number of extra idle cycles after each transfer. */
static uint8_t swd_idle_cycles;
/* Number of transfer retries after WAIT response. */
static uint16_t swd_wait_retries;
/*
 * Number of retries on reads with Value Match in DAP_Transfer. On
 * value mismatch the Register is read again until its value matches
 * or the Match Retry count exceeds.
 */
static uint16_t swd_match_retries;
#endif

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

void cmsis_dap_queue_blocking_discard(size_t count)
{
	while (!cmsis_dap_unwind_requested()) {
		size_t progress =
			queue_advance_head(&cmsis_dap_rx_queue, count);
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

struct queue_chunk cmsis_dap_queue_get_read_chunk(void)
{
	struct queue_chunk res;
	while (!cmsis_dap_unwind_requested()) {
		res = queue_get_read_chunk(&cmsis_dap_rx_queue);
		if (res.count > 0)
			return res;
		/*
		 * Wait for queue producer to wake up this task, when there is
		 * more data in the queue.
		 */
		task_wait_event(0);
	}
	res.count = 0;
	res.buffer = NULL;
	return res;
}

int parity32(uint32_t a)
{
	a ^= a >> 16;
	a ^= a >> 8;
	a ^= a >> 4;
	a ^= a >> 2;
	a ^= a >> 1;
	return a & 1;
}

/* SWD devices sample and change state on rising clock edge. */
static inline __attribute__((always_inline)) void clock_cycle(void)
{
	gpio_set_level(GPIO_JTAG_TCLK, false);
	cmsis_dap_half_clock_delay();
	gpio_set_level(GPIO_JTAG_TCLK, true);
	cmsis_dap_half_clock_delay();
}

/*
 * Implementation of handler routines for each CMSIS-DAP command.
 */

/* Info command, used to discover which other commands are supported. */
static void cmsis_dap_info(void)
{
	const char *CMSIS_DAP_VERSION_STR = "2.1.1";
	const uint16_t CAPABILITIES =
#ifdef CONFIG_USB_CMSIS_DAP_JTAG
		CAP_Jtag |
#endif
#ifdef CONFIG_USB_CMSIS_DAP_SWD
		CAP_Swd |
#endif
		0;
	uint8_t i, len;
	uint8_t req[2];

	cmsis_dap_queue_blocking_remove(&req, sizeof(req));
	if (cmsis_dap_unwind_requested())
		return;
	queue_add_unit(&cmsis_dap_tx_queue, &req[0]);
	switch (req[1]) {
#ifdef CONFIG_USB_SERIALNO
	case INFO_Serial: {
		struct usb_string_desc *sd = usb_serialno_desc;
		for (len = 0; len < CONFIG_SERIALNO_LEN && sd->_data[len];
		     len++)
			;
		queue_add_unit(&cmsis_dap_tx_queue, &len);
		for (i = 0; i < len; i++)
			queue_add_unit(&cmsis_dap_tx_queue, &sd->_data[i]);
		break;
	}
#endif
	case INFO_Version:
		len = strlen(CMSIS_DAP_VERSION_STR) + 1;
		queue_add_unit(&cmsis_dap_tx_queue, &len);
		queue_add_units(&cmsis_dap_tx_queue, CMSIS_DAP_VERSION_STR,
				len);
		break;
	case INFO_Capabilities:
		len = sizeof(CAPABILITIES);
		queue_add_unit(&cmsis_dap_tx_queue, &len);
		queue_add_units(&cmsis_dap_tx_queue, &CAPABILITIES, len);
		break;
	case INFO_PacketCount: {
		uint8_t resp[] = { 0x40 };
		i = sizeof(resp);
		queue_add_unit(&cmsis_dap_tx_queue, &i);
		queue_add_units(&cmsis_dap_tx_queue, resp, i);
		break;
	}
	case INFO_PacketSize: {
		uint8_t resp[] = { 0x40, 0x00 };
		i = sizeof(resp);
		queue_add_unit(&cmsis_dap_tx_queue, &i);
		queue_add_units(&cmsis_dap_tx_queue, resp, i);
		break;
	}
	default:
		ccprintf("Unknown info request 0x%02x\n", req[1]);
		len = 0;
		queue_add_unit(&cmsis_dap_tx_queue, &len);
		break;
	}
}

/* Informational command, to allow debugging device to indicate status. */
static void cmsis_dap_host_status(void)
{
	uint8_t req[3];
	cmsis_dap_queue_blocking_remove(&req, sizeof(req));
	if (cmsis_dap_unwind_requested())
		return;
	queue_add_unit(&cmsis_dap_tx_queue, &req[0]);
	uint8_t resp = STATUS_Ok;
	queue_add_unit(&cmsis_dap_tx_queue, &resp);
}

/* Establish JTAG connection, take control of JTAG pins. */
static void cmsis_dap_connect(void)
{
	uint8_t req[2];
	cmsis_dap_queue_blocking_remove(&req, sizeof(req));
	if (cmsis_dap_unwind_requested())
		return;
	uint8_t resp;
	switch (req[1]) {
	case CONN_REQ_Default:
#ifdef CONFIG_USB_CMSIS_DAP_JTAG
	case CONN_REQ_Jtag:
		resp = CONN_RESP_Jtag;
		if (!jtag_enabled) {
			jtag_enabled = true;
			cmsis_dap_enable_jtag_pins();
		}
		break;
#endif
#ifdef CONFIG_USB_CMSIS_DAP_SWD
	case CONN_REQ_Swd:
		resp = CONN_RESP_Swd;
		if (!jtag_enabled) {
			jtag_enabled = true;
			cmsis_dap_enable_swd_pins();
		}
		break;
#endif
	default:
		resp = CONN_RESP_Failed;
	}
	queue_add_unit(&cmsis_dap_tx_queue, &req[0]);
	queue_add_unit(&cmsis_dap_tx_queue, &resp);
}

/* Restore JTAG pins to previous configuration. */
static void cmsis_dap_disconnect(void)
{
	uint8_t req[1];
	cmsis_dap_queue_blocking_remove(&req, sizeof(req));
	if (cmsis_dap_unwind_requested())
		return;

	if (jtag_enabled) {
		jtag_enabled = false;
		cmsis_dap_disable_jtag_swd_pins();
	}

	queue_add_unit(&cmsis_dap_tx_queue, &req[0]);
	uint8_t resp = STATUS_Ok;
	queue_add_unit(&cmsis_dap_tx_queue, &resp);
}

#if defined(CONFIG_USB_CMSIS_DAP_JTAG) || defined(CONFIG_USB_CMSIS_DAP_SWD)
/* Configure parameters for DAP_Transfer family of requests. */
static void cmsis_dap_transfer_configure(void)
{
	uint8_t req[6];
	cmsis_dap_queue_blocking_remove(&req, sizeof(req));
	if (cmsis_dap_unwind_requested())
		return;

#ifdef CONFIG_USB_CMSIS_DAP_SWD
	swd_idle_cycles = req[1];
	swd_wait_retries = req[2] + (req[3] << 8);
	swd_match_retries = req[4] + (req[5] << 8);
#else
		/*
		 * This file does not offer support for the DAP_Transfer family
		 * of requests, and OpenOCD does not seem to issue any requests
		 * (at least not when operating on a RISC-V OpenTitan code.
		 *
		 * OpenOCD still sends this configuration request as part of its
		 * setup sequence, we can safely ignore the parameters given,
		 * and report success to the caller.
		 */
#endif

	queue_add_unit(&cmsis_dap_tx_queue, &req[0]);
	uint8_t resp = STATUS_Ok;
	queue_add_unit(&cmsis_dap_tx_queue, &resp);
}

/*
 * Send eight SWD request bits, followed by reading three bits of response.
 */
static uint8_t swd_transfer_header(bool read_write, bool port, uint8_t addr)
{
	// Start bit
	gpio_set_level(GPIO_JTAG_TMS, true);
	clock_cycle();

	// DP=0/AP=1
	gpio_set_level(GPIO_JTAG_TMS, port);
	clock_cycle();

	// Read=1/Write=0
	gpio_set_level(GPIO_JTAG_TMS, read_write);
	clock_cycle();

	// Addr
	gpio_set_level(GPIO_JTAG_TMS, !!(addr & BIT(2)));
	clock_cycle();
	// Addr
	gpio_set_level(GPIO_JTAG_TMS, !!(addr & BIT(3)));
	clock_cycle();

	// Parity
	gpio_set_level(GPIO_JTAG_TMS, read_write ^ port ^ !!(addr & BIT(2)) ^
					      !!(addr & BIT(3)));
	clock_cycle();

	// Stop
	gpio_set_level(GPIO_JTAG_TMS, false);
	clock_cycle();

	// Park
	gpio_set_level(GPIO_JTAG_TMS, true);
	clock_cycle();

	// Turnaround
	cmsis_dap_swdio_input();
	for (int i = 0; i < swd_turn_cycles; i++) {
		clock_cycle();
	}

	// Three bit ack from device
	uint8_t ack = 0;
	ack |= gpio_get_level(GPIO_JTAG_TMS) ? SWD_ACK_Ok : 0;
	clock_cycle();
	ack |= gpio_get_level(GPIO_JTAG_TMS) ? SWD_ACK_Wait : 0;
	clock_cycle();
	ack |= gpio_get_level(GPIO_JTAG_TMS) ? SWD_ACK_Fault : 0;
	clock_cycle();
	return ack;
}

static int dap_read_register(bool port, uint8_t addr, uint32_t *value)
{
	uint8_t ack = swd_transfer_header(true, port, addr);

	if (ack != SWD_ACK_Ok && !swd_data_on_wait_or_fault) {
		// Turnaround
		for (int i = 0; i < swd_turn_cycles; i++)
			clock_cycle();
		cmsis_dap_swdio_output(true);
		return ack;
	}

	uint32_t rdata = 0;
	for (int i = 0; i < 32; i++) {
		rdata |= gpio_get_level(GPIO_JTAG_TMS) ? (1 << i) : 0;
		clock_cycle();
	}

	// Parity
	bool parity = gpio_get_level(GPIO_JTAG_TMS);
	clock_cycle();

	// Turnaround
	for (int i = 0; i < swd_turn_cycles; i++)
		clock_cycle();
	cmsis_dap_swdio_output(true);

	if (parity32(rdata) != parity)
		ack |= SWD_ACK_ParityError;

	if (ack == SWD_ACK_Ok)
		*value = rdata;
	return ack;
}

static int dap_write_register(bool port, uint8_t addr, uint32_t value)
{
	uint8_t ack = swd_transfer_header(false, port, addr);

	// Turnaround
	for (int i = 0; i < swd_turn_cycles; i++)
		clock_cycle();

	if (ack != SWD_ACK_Ok && !swd_data_on_wait_or_fault) {
		cmsis_dap_swdio_output(true);
		return ack;
	}

	cmsis_dap_swdio_output(value & 0x01);
	for (int i = 0; i < 32; i++) {
		gpio_set_level(GPIO_JTAG_TMS, !!(value & (1 << i)));
		clock_cycle();
	}

	// Parity
	gpio_set_level(GPIO_JTAG_TMS, parity32(value));
	clock_cycle();
	return ack;
}

void store_byte(struct queue_chunk chunk1, struct queue_chunk chunk2,
		size_t offset, uint8_t val)
{
	if (offset < chunk1.count)
		((uint8_t *)chunk1.buffer)[offset] = val;
	else
		((uint8_t *)chunk2.buffer)[offset - chunk1.count] = val;
}

static void dap_transfer(void)
{
	uint8_t req[3];
	cmsis_dap_queue_blocking_remove(&req, sizeof(req));
	if (cmsis_dap_unwind_requested())
		return;

	struct queue_chunk chunk1 =
		queue_get_write_chunk(&cmsis_dap_tx_queue, 0);
	struct queue_chunk chunk2 =
		queue_get_write_chunk(&cmsis_dap_tx_queue, chunk1.count);
	store_byte(chunk1, chunk2, 0, req[0]);

	uint8_t transfer_count = req[2];
	uint8_t transfer_index;
	uint8_t out_index = 3; // 7 if timestamped
	uint8_t status = SWD_ACK_Ok;
	for (transfer_index = 0;
	     status == SWD_ACK_Ok && transfer_index < transfer_count;
	     transfer_index++) {
		uint8_t req;
		cmsis_dap_queue_blocking_remove(&req, 1);
		uint32_t data;
		if (!(req & BIT(1))) {
			/* Write register */
			cmsis_dap_queue_blocking_remove((uint8_t *)&data, 4);
		} else if (req & BIT(4)) {
			/* Value match */
			cmsis_dap_queue_blocking_remove((uint8_t *)&data, 4);
			ccprintf("Unimplemented: value_match\n");
		} else if (req & BIT(5)) {
			/* Match mask */
			cmsis_dap_queue_blocking_remove((uint8_t *)&data, 4);
			ccprintf("Unimplemented: match_mask\n");
		}

		uint8_t addr = req & 0x0C;

		if (!(req & BIT(1))) {
			/* Write DAP register */
			do {
				status = dap_write_register(req & BIT(0), addr,
							    data);
			} while (status == SWD_ACK_Wait);
			continue;
		}
		/* Read DAP register */
		uint32_t value = 0;
		do {
			status = dap_read_register(req & BIT(0), addr, &value);
		} while (status == SWD_ACK_Wait);
		if (status != SWD_ACK_Ok)
			break;
		if (req & BIT(0)) {
			/*
			 * AP register value retrieved by reading DP
			 * 0x0C in subsequent operation.
			 */
			do {
				status = dap_read_register(false, 0x0C, &value);
			} while (status == SWD_ACK_Wait);
			if (status != SWD_ACK_Ok)
				break;
		}
		store_byte(chunk1, chunk2, out_index, value & 0xFF);
		store_byte(chunk1, chunk2, out_index + 1, (value >> 8) & 0xFF);
		store_byte(chunk1, chunk2, out_index + 2, (value >> 16) & 0xFF);
		store_byte(chunk1, chunk2, out_index + 3, (value >> 24) & 0xFF);
		out_index += 4;
	}

	size_t leftover = queue_count(&cmsis_dap_rx_queue);
	if (leftover > 0) {
		ccprintf("ERROR: %d bytes not consumed\n", leftover);
		queue_advance_head(&cmsis_dap_rx_queue, leftover);
	}

	if (swd_idle_cycles > 0) {
		gpio_set_level(GPIO_JTAG_TMS, false);
		for (int i = 0; i < swd_idle_cycles; i++) {
			clock_cycle();
		}
	}

	store_byte(chunk1, chunk2, 1, transfer_index);
	store_byte(chunk1, chunk2, 2, status);
	queue_advance_tail(&cmsis_dap_tx_queue, out_index);
}
#endif

/* Reset the GSC (using same pin as if blue button was pressed). */
static void cmsis_dap_reset_target(void)
{
	uint8_t req[1];
	cmsis_dap_queue_blocking_remove(&req, sizeof(req));
	if (cmsis_dap_unwind_requested())
		return;

	uint8_t resp;
	if (GPIO_JTAG_RESET != GPIO_COUNT) {
		gpio_set_level(GPIO_JTAG_RESET, false);
		crec_usleep(100000);
		gpio_set_level(GPIO_JTAG_RESET, true);
		resp = 1;
	} else {
		resp = 0;
	}
	queue_add_unit(&cmsis_dap_tx_queue, &req[0]);
	uint8_t status = STATUS_Ok;
	queue_add_unit(&cmsis_dap_tx_queue, &status);
	queue_add_unit(&cmsis_dap_tx_queue, &resp);
}

/* One-time setting of the output level of each JTAG signal. */
static void cmsis_dap_swj_pins(void)
{
	struct {
		uint8_t header;
		uint8_t value;
		uint8_t mask;
	} req;
	uint32_t wait_us;
	cmsis_dap_queue_blocking_remove(&req, sizeof(req));
	cmsis_dap_queue_blocking_remove(&wait_us, sizeof(wait_us));
	if (cmsis_dap_unwind_requested())
		return;

	if ((req.mask & PIN_SwClk_Tck))
		gpio_set_level(GPIO_JTAG_TCLK, !!(req.value & PIN_SwClk_Tck));
	if ((req.mask & PIN_SwDio_Tms))
		gpio_set_level(GPIO_JTAG_TMS, !!(req.value & PIN_SwDio_Tms));
	if ((req.mask & PIN_Tdi) && GPIO_JTAG_TDI != GPIO_COUNT)
		gpio_set_level(GPIO_JTAG_TDI, !!(req.value & PIN_Tdi));
	if ((req.mask & PIN_Trst) && GPIO_JTAG_TRST != GPIO_COUNT)
		gpio_set_level(GPIO_JTAG_TRST, !!(req.value & PIN_Trst));
	if ((req.mask & PIN_Reset) && GPIO_JTAG_RESET != GPIO_COUNT)
		gpio_set_level(GPIO_JTAG_RESET, !!(req.value & PIN_Reset));

	crec_usleep(wait_us);

	uint8_t resp =
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
	queue_add_unit(&cmsis_dap_tx_queue, &req.header);
	queue_add_unit(&cmsis_dap_tx_queue, &resp);
}

/* Set JTAG clock frequency. */
static void cmsis_dap_swj_clock(void)
{
	uint8_t req[1];
	uint32_t new_clock_hz;

	cmsis_dap_queue_blocking_remove(&req, sizeof(req));
	cmsis_dap_queue_blocking_remove(&new_clock_hz, sizeof(new_clock_hz));
	if (cmsis_dap_unwind_requested())
		return;

	uint8_t resp;
	if (!new_clock_hz)
		resp = STATUS_Error;
	else if (cmsis_dap_set_period(new_clock_hz) != EC_SUCCESS)
		resp = STATUS_Error;
	else
		resp = STATUS_Ok;

	queue_add_unit(&cmsis_dap_tx_queue, &req[0]);
	queue_add_unit(&cmsis_dap_tx_queue, &resp);
}

/* Clock data out on TMS. */
static void cmsis_dap_swj_sequence(void)
{
	uint8_t req[2];
	cmsis_dap_queue_blocking_remove(&req, sizeof(req));
	if (cmsis_dap_unwind_requested())
		return;
	unsigned int bit_count = req[1] == 0 ? 256 : req[1];
	struct queue_chunk chunk = { 0, NULL };
	size_t index = (size_t)-1; /* Index into the data in chunk. */
	for (unsigned int i = 0; i < bit_count; i++) {
		if (i % 8 == 0 && ++index == chunk.count) {
			if (index > 0) {
				queue_advance_head(&cmsis_dap_rx_queue, index);
			}
			chunk = cmsis_dap_queue_get_read_chunk();
			if (cmsis_dap_unwind_requested())
				return;
			index = 0;
		}
		gpio_set_level(GPIO_JTAG_TMS,
			       !!(((const uint8_t *)chunk.buffer)[index] &
				  (1 << (i % 8))));
		clock_cycle();
	}
	if (++index > 0) {
		queue_advance_head(&cmsis_dap_rx_queue, index);
	}
	queue_add_unit(&cmsis_dap_tx_queue, &req[0]);
	uint8_t status = STATUS_Ok;
	queue_add_unit(&cmsis_dap_tx_queue, &status);
}

#ifdef CONFIG_USB_CMSIS_DAP_SWD
static void dap_swd_configure(void)
{
	uint8_t req[2];
	cmsis_dap_queue_blocking_remove(&req, sizeof(req));
	if (cmsis_dap_unwind_requested())
		return;
	swd_turn_cycles = (req[1] & 0x03) + 1;
	swd_data_on_wait_or_fault = !!(req[1] & 0x04);
	req[1] = STATUS_Ok;
	queue_add_units(&cmsis_dap_tx_queue, req, 2);
}
#endif

#ifdef CONFIG_USB_CMSIS_DAP_JTAG
/*
 * Do a JTAG transaction, consisting of one or more sequences of clocking data
 * on TDI (between 1 and 64 bits), while keeping TMS at a particular level.
 */
static void cmsis_dap_jtag_sequence(void)
{
	uint8_t req[2];
	cmsis_dap_queue_blocking_remove(&req, sizeof(req));
	if (cmsis_dap_unwind_requested())
		return;

	queue_add_unit(&cmsis_dap_tx_queue, &req[0]);
	uint8_t status = STATUS_Ok;
	queue_add_unit(&cmsis_dap_tx_queue, &status);

	struct queue_chunk out_chunk =
		queue_get_write_chunk(&cmsis_dap_tx_queue, 0);
	size_t out_index = 0;
	if (cmsis_dap_unwind_requested())
		return;

	struct queue_chunk in_chunk = { 0, NULL };
	size_t in_index = 0;

	/*
	 * Iterate over the list of "sequences", each having a one-byte header
	 * specifying how many bits in the sequence, what the value of TMS
	 * during this sequence, and whether to record TDO during this sequence.
	 */
	for (int seq_no = 0; seq_no < req[1]; seq_no++) {
		/* Consume and decode header byte for this one "sequence". */
		if (in_index == in_chunk.count) {
			queue_advance_head(&cmsis_dap_rx_queue, in_index);
			in_chunk = cmsis_dap_queue_get_read_chunk();
			in_index = 0;
			if (cmsis_dap_unwind_requested())
				return;
		}
		uint8_t header = ((const uint8_t *)in_chunk.buffer)[in_index++];
		gpio_set_level(GPIO_JTAG_TMS, header & SEQ_Tms);
		bool capture_tdo = !!(header & SEQ_CaptureTdo);
		unsigned int bit_count = (((header - 1) & SEQ_NumBits) + 1);

		/*
		 * With TMS set at a given value, clock 1 - 64 bits of data on
		 * TDI/TDO.
		 */
		uint8_t data = 0, out_byte = 0;
		for (unsigned int i = 0; i < bit_count; i++) {
			if (i % 8 == 0) {
				if (in_index == in_chunk.count) {
					queue_advance_head(&cmsis_dap_rx_queue,
							   in_index);
					in_chunk =
						cmsis_dap_queue_get_read_chunk();
					in_index = 0;
					if (cmsis_dap_unwind_requested())
						return;
				}
				data = ((const uint8_t *)
						in_chunk.buffer)[in_index++];
			}
			gpio_set_level(GPIO_JTAG_TDI, data & (1 << (i % 8)));
			gpio_set_level(GPIO_JTAG_TCLK, false);
			cmsis_dap_half_clock_delay();
			uint32_t tdo_val = gpio_get_level(GPIO_JTAG_TDO);
			if (capture_tdo) {
				out_byte |= tdo_val << (i % 8);
				if (i % 8 == 7) {
					if (out_index == out_chunk.count) {
						queue_advance_tail(
							&cmsis_dap_tx_queue,
							out_index);
						out_chunk = queue_get_write_chunk(
							&cmsis_dap_tx_queue, 0);
						out_index = 0;
					}
					((uint8_t *)out_chunk
						 .buffer)[out_index++] =
						out_byte;
					out_byte = 0;
				}
			}
			gpio_set_level(GPIO_JTAG_TCLK, true);
			cmsis_dap_half_clock_delay();
		}
		/* Transmit any "partial" byte. */
		if (capture_tdo && bit_count % 8 != 0) {
			if (out_index == out_chunk.count) {
				queue_advance_tail(&cmsis_dap_tx_queue,
						   out_index);
				out_chunk = queue_get_write_chunk(
					&cmsis_dap_tx_queue, 0);
				out_index = 0;
			}
			((uint8_t *)out_chunk.buffer)[out_index++] = out_byte;
		}
	}
	queue_advance_head(&cmsis_dap_rx_queue, in_index);
	queue_advance_tail(&cmsis_dap_tx_queue, out_index);
}
#endif

/* Vendor command (HyperDebug): Discover Google-specific capabilities. */
static void cmsis_dap_goog_info(void)
{
	const uint16_t CAPABILITIES =
#if defined(CONFIG_USB_CMSIS_DAP_I2C) || defined(CONFIG_USB_CMSIS_DAP_BOARD_I2C)
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

	uint8_t req[2];

	cmsis_dap_queue_blocking_remove(&req, sizeof(req));
	if (cmsis_dap_unwind_requested())
		return;
	queue_add_unit(&cmsis_dap_tx_queue, &req[0]);
	uint8_t len;
	switch (req[1]) {
	case GOOG_INFO_Capabilities:
		len = sizeof(CAPABILITIES);
		queue_add_unit(&cmsis_dap_tx_queue, &len);
		queue_add_units(&cmsis_dap_tx_queue, &CAPABILITIES, len);
		break;
	default:
		ccprintf("Unknown Google info request %02x\n", req[1]);
		len = 0;
		queue_add_unit(&cmsis_dap_tx_queue, &len);
		break;
	}
}

#ifdef CONFIG_USB_CMSIS_DAP_I2C
#include "i2c.h"
#include "shared_mem.h"
#include "usb_i2c.h"

/* Duplicated from usb_i2c.c */
static int16_t usb_i2c_map_error(int error)
{
	switch (error) {
	case EC_SUCCESS:
		return USB_I2C_SUCCESS;
	case EC_ERROR_TIMEOUT:
		return USB_I2C_TIMEOUT;
	case EC_ERROR_BUSY:
		return USB_I2C_BUSY;
	default:
		return USB_I2C_UNKNOWN_ERROR | (error & 0x7fff);
	}
}

void cmsis_dap_goog_i2c(void)
{
	uint8_t header[7];

	/* One byte CMSIS-DAP request header, 4 bytes of I2C header. */
	cmsis_dap_queue_blocking_remove(header, 5);
	if (cmsis_dap_unwind_requested())
		return;

	/* Decode 4 bytes of I2C header. */
	int portindex = header[1] & 0xf;
	uint16_t addr_flags = header[2] & 0x7f;
	int write_count = ((header[1] << 4) & 0xf00) | header[3];
	int read_count = header[4];

	if (read_count & 0x80) {
		/* 2 more bytes of I2C header. */
		cmsis_dap_queue_blocking_remove(header + 5, 2);
		if (cmsis_dap_unwind_requested())
			return;
		read_count = (header[5] << 7) | (read_count & 0x7f);
	}

	/* Clear area for response header */
	header[1] = 0;
	header[2] = 0;
	header[3] = 0;
	header[4] = 0;

	uint16_t i2c_status = 0;
	char *data = NULL;
	if (!usb_i2c_board_is_enabled()) {
		i2c_status = USB_I2C_DISABLED;
	} else if (!read_count && !write_count) {
		/* No-op, report as success */
		i2c_status = USB_I2C_SUCCESS;
	} else if (write_count > CONFIG_USB_I2C_MAX_WRITE_COUNT) {
		i2c_status = USB_I2C_WRITE_COUNT_INVALID;
	} else if (read_count > CONFIG_USB_I2C_MAX_READ_COUNT) {
		i2c_status = USB_I2C_READ_COUNT_INVALID;
	} else if (portindex >= i2c_ports_used) {
		i2c_status = USB_I2C_PORT_INVALID;
	} else {
		int rv =
			shared_mem_acquire(MAX(write_count, read_count), &data);
		if (rv != EC_SUCCESS)
			panic("No mem");
		cmsis_dap_queue_blocking_remove(data, write_count);
		int ret = i2c_xfer(i2c_ports[portindex].port, addr_flags, data,
				   write_count, data, read_count);
		i2c_status = usb_i2c_map_error(ret);
	}
	header[1] = i2c_status & 0xFF;
	header[2] = i2c_status >> 8;
	/*
	 * Send one byte of CMSIS-DAP header, four bytes of Google I2C header,
	 * followed by any data received via I2C.
	 */
	queue_add_units(&cmsis_dap_tx_queue, header, 1 + 4);
	if (data) {
		cmsis_dap_queue_blocking_add(data, read_count);
		shared_mem_release(data);
	}
}
#endif

/* Map from CMSIS-DAP command byte to handler routine. */
static void (*dispatch_table[256])(void) = {
	[DAP_Info] = cmsis_dap_info,
	[DAP_HostStatus] = cmsis_dap_host_status,
	[DAP_Connect] = cmsis_dap_connect,
	[DAP_Disconnect] = cmsis_dap_disconnect,
	[DAP_ResetTarget] = cmsis_dap_reset_target,

#if defined(CONFIG_USB_CMSIS_DAP_JTAG) || defined(CONFIG_USB_CMSIS_DAP_SWD)
	[DAP_TransferConfigure] = cmsis_dap_transfer_configure,
	[DAP_SWJ_Pins] = cmsis_dap_swj_pins,
	[DAP_SWJ_Clock] = cmsis_dap_swj_clock,
	[DAP_SWJ_Sequence] = cmsis_dap_swj_sequence,
#endif

#ifdef CONFIG_USB_CMSIS_DAP_JTAG
	[DAP_JTAG_Sequence] = cmsis_dap_jtag_sequence,
#endif

#ifdef CONFIG_USB_CMSIS_DAP_SWD
	[DAP_SWD_Configure] = dap_swd_configure,
	[DAP_Transfer] = dap_transfer,
#endif

	/* Google extensions to CMSIS-DAP protocol. */
	[DAP_GOOG_Info] = cmsis_dap_goog_info,
#if defined(CONFIG_USB_CMSIS_DAP_I2C) || defined(CONFIG_USB_CMSIS_DAP_BOARD_I2C)
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
	uint8_t req;
	if (!queue_peek_units(&cmsis_dap_rx_queue, &req, 0, 1)) {
		/* Not enough data to start decoding request. */
		return;
	}

	if (dispatch_table[req]) {
		/* Invoke handler routine. */
		dispatch_table[req]();
		size_t s = queue_count(&cmsis_dap_rx_queue);
		if (s) {
			ccprintf("Warning, %d extra bytes after req %02x\n", s,
				 req);
		}
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
		cmsis_dap_disable_jtag_swd_pins();
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

struct queue const cmsis_dap_tx_queue =
	QUEUE_DIRECT(64, uint8_t, cmsis_dap_producer, cmsis_dap_usb.consumer);

struct queue const cmsis_dap_rx_queue =
	QUEUE_DIRECT(64, uint8_t, cmsis_dap_usb.producer, cmsis_dap_consumer);

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "usb_descriptor.h"
#include "usb_hw.h"
#include "util.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct _HID_DEVICE_INFO {
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint32_t StrDescPtr;
	uint8_t InReportCount;
	uint8_t OutReportCount;
	uint8_t SampleInterval;
	void (*InReport)( uint8_t src[], uint32_t length);
	void (*OutReport)(uint8_t dst[], uint32_t length);
} HID_DEVICE_INFO;

typedef struct _USB_DEVICE_INFO {
	uint16_t DevType;
	uint32_t DevDetailPtr;
} USB_DEV_INFO;

typedef struct _USBD {
	void (*init_clk_pins)(void);
	void (*isr)(void);
	void (*init)( USB_DEV_INFO * DevInfoPtr );
	void (*connect)(uint32_t con);
} USBD;

typedef struct _ROM {
	const USBD * pUSBD;
} ROM;

#define USB_STRING_DESCRIPTOR_TYPE 3

uint8_t USB_HID_StringDescriptor[] = {
	0x04, USB_STRING_DESCRIPTOR_TYPE, 0x09, 0x04,
	/* Index 0x04: Manufacturer */
	0x1C, USB_STRING_DESCRIPTOR_TYPE,
	'N',0, 'X',0, 'P',0,' ',0,'S',0, 'E',0, 'M',0,'I',0, 'C',0,'O',0,'N',0,'D',0,' ',0,
	/* Index 0x20: Product */
	0x28,USB_STRING_DESCRIPTOR_TYPE,
	/* bDescriptorType */
	'N',0, 'X',0, 'P',0, ' ',0, 'L',0, 'P',0, 'C',0, '1',0, '3',0, 'X',0,
	'X',0, ' ',0, 'H',0, 'I',0, 'D',0, ' ',0, ' ',0, ' ',0, ' ',0,
	/* Index 0x48: Serial Number */
	0x1A, USB_STRING_DESCRIPTOR_TYPE,
	/* bDescriptorType */
	'D',0, 'E',0, 'M',0, 'O',0, '0',0, '0',0, '0',0, '0',0, '0',0, '0',0,
	'0',0, '0',0,
	/* Index 0x62: Interface 0, Alternate Setting 0 */
	0x0E, USB_STRING_DESCRIPTOR_TYPE,
	/* bDescriptorType */
	'H',0, 'I',0, 'D',0, ' ',0, ' ',0, ' ',0,
};


void usb_hid_in_report(uint8_t src[], uint32_t length)
{
	cprintf(CC_USB, "usb_hid_in_report(_, %d)", length);
}

void usb_hid_out_report(uint8_t dst[], uint32_t length)
{
	cprintf(CC_USB, "usb_hid_out_report(_, %d)", length);
}

#define USB_DEVICE_CLASS_HUMAN_INTERFACE 0x03

ROM ** rom = (ROM **)0x1fff1ff8;

USB_DEV_INFO DeviceInfo;
HID_DEVICE_INFO HidDevInfo;

#ifdef CONFIG_USB_BOS
/* v2.10 (vs 2.00) BOS Descriptor provided */
#define USB_DEV_BCDUSB 0x0210
#else
#define USB_DEV_BCDUSB 0x0200
#endif

#ifndef USB_DEV_CLASS
#define USB_DEV_CLASS USB_CLASS_PER_INTERFACE
#endif

#ifndef CONFIG_USB_BCD_DEV
#define CONFIG_USB_BCD_DEV 0x0100 /* 1.00 */
#endif

#ifndef CONFIG_USB_SERIALNO
#define USB_STR_SERIALNO 0
#else
//static int usb_load_serial(void);
#endif

#ifndef CONFIG_USB_MAX_CONTROL_PACKET_SIZE
#define EP0_MAX_PACKET_SIZE USB_MAX_PACKET_SIZE
#else
#define EP0_MAX_PACKET_SIZE CONFIG_USB_MAX_CONTROL_PACKET_SIZE
#endif

BUILD_ASSERT(EP0_MAX_PACKET_SIZE == 8 || EP0_MAX_PACKET_SIZE == 16 ||
	     EP0_MAX_PACKET_SIZE == 32 || EP0_MAX_PACKET_SIZE == 64);

#define USB_RESUME_TIMEOUT_MS 3000

/* USB Standard Device Descriptor */
static const struct usb_device_descriptor dev_desc __attribute__ ((aligned (4))) = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = USB_DEV_BCDUSB,
	.bDeviceClass = USB_DEV_CLASS,
	.bDeviceSubClass = 0x00,
	.bDeviceProtocol = 0x00,
	.bMaxPacketSize0 = EP0_MAX_PACKET_SIZE,
	.idVendor = CONFIG_USB_VID,
	.idProduct = CONFIG_USB_PID,
	.bcdDevice = CONFIG_USB_BCD_DEV,
	.iManufacturer = USB_STR_VENDOR,
	.iProduct = USB_STR_PRODUCT,
	.iSerialNumber = USB_STR_SERIALNO,
	.bNumConfigurations = 1
};

/* USB Configuration Descriptor */
const struct usb_config_descriptor USB_CONF_DESC(conf) = {
	.bLength = USB_DT_CONFIG_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0x0BAD, /* no of returned bytes, set at runtime */
	.bNumInterfaces = USB_IFACE_COUNT,
	.bConfigurationValue = 1,
	.iConfiguration = USB_STR_VERSION,
	.bmAttributes = 0x80 /* Reserved bit */
#ifdef CONFIG_USB_SELF_POWERED /* bus or self powered */
			| 0x40
#endif
#ifdef CONFIG_USB_REMOTE_WAKEUP
			| 0x20
#endif
	,
	.bMaxPower = (CONFIG_USB_MAXPOWER_MA / 2),
};

const uint8_t usb_string_desc[] = {
	4, /* Descriptor size */
	USB_DT_STRING, 0x09, 0x04 /* LangID = 0x0409: U.S. English */
};

/* remaining size of descriptor data to transfer */
static int desc_left;
/* pointer to descriptor data if any */
static const uint8_t *desc_ptr;

static bool usb_initialized = false;

void sie_command(uint8_t cmd, uint8_t *data, size_t len)
{
	LPC_USB_DEV_INT_CLR = LPC_USB_DEV_INT_CC_EMPTY;
	
	LPC_USB_CMD_CODE = (cmd << 16) | (0x05 << 8);
	while (!(LPC_USB_DEV_INT_ST & LPC_USB_DEV_INT_CC_EMPTY))
		;
	LPC_USB_DEV_INT_CLR = LPC_USB_DEV_INT_CC_EMPTY;

	for (size_t i = 0; i < len; i++) {
		LPC_USB_CMD_CODE = (data[i] << 16) | (0x01 << 8);
		while (!(LPC_USB_DEV_INT_ST & LPC_USB_DEV_INT_CC_EMPTY))
			;
		LPC_USB_DEV_INT_CLR = LPC_USB_DEV_INT_CC_EMPTY;
	}
}

void sie_command_nodata(uint8_t cmd)
{
	LPC_USB_DEV_INT_CLR = LPC_USB_DEV_INT_CC_EMPTY;
	
	LPC_USB_CMD_CODE = (cmd << 16) | (0x05 << 8);
	while (!(LPC_USB_DEV_INT_ST & LPC_USB_DEV_INT_CC_EMPTY))
		;
	LPC_USB_DEV_INT_CLR = LPC_USB_DEV_INT_CC_EMPTY;
}

void sie_command_write1(uint8_t cmd, uint8_t data)
{
	LPC_USB_DEV_INT_CLR = LPC_USB_DEV_INT_CC_EMPTY;
	
	LPC_USB_CMD_CODE = (cmd << 16) | (0x05 << 8);
	while (!(LPC_USB_DEV_INT_ST & LPC_USB_DEV_INT_CC_EMPTY))
		;
	LPC_USB_DEV_INT_CLR = LPC_USB_DEV_INT_CC_EMPTY;

	LPC_USB_CMD_CODE = (data << 16) | (0x01 << 8);
	while (!(LPC_USB_DEV_INT_ST & LPC_USB_DEV_INT_CC_EMPTY))
		;
	LPC_USB_DEV_INT_CLR = LPC_USB_DEV_INT_CC_EMPTY;
}

uint8_t sie_command_read1(uint8_t cmd)
{
	LPC_USB_DEV_INT_CLR = LPC_USB_DEV_INT_CC_EMPTY;
	
	LPC_USB_CMD_CODE = (cmd << 16) | (0x05 << 8);
	while (!(LPC_USB_DEV_INT_ST & LPC_USB_DEV_INT_CC_EMPTY))
		;
	LPC_USB_DEV_INT_CLR = LPC_USB_DEV_INT_CC_EMPTY;

	LPC_USB_CMD_CODE = (0x02 << 8);
	while (!(LPC_USB_DEV_INT_ST & LPC_USB_DEV_INT_CD_FULL))
		;
	LPC_USB_DEV_INT_CLR = LPC_USB_DEV_INT_CD_FULL;
	return LPC_USB_CMD_DATA;
}

void usb_init(void) {
	/* Enable 32-bit timer 1, used by USB driver in ROM. */
	LPC_SYSCFG_SYSAHBCLKCTRL |= LPC_SYSCFG_SYSAHBCLKCTRL_CT32B1;

	/* Enable power to USB PLL (by zeroing its "power down" bit).  */
	LPC_SYSCFG_PDRUNCFG &= ~LPC_SYSCFG_PDRUNCFG_USBPLL_PD;

	/* Select external oscillator as source for PLL. */
	LPC_SYSCFG_USBPLLCLKSEL = 1;

	/* Configure PLL */
	const int USB_PLLM = 4;
	LPC_SYSCFG_USBPLLCTRL = ((USB_PLLM - 1) << LPC_SYSCFG_PLLCTRL_MSEL_POS)
		| LPC_SYSCFG_PLLCTRL_PSEL_2;

	/* Rising edge of enable bit in order for the changes to take effect. */
	LPC_SYSCFG_USBPLLCLKUEN = 0;
	LPC_SYSCFG_USBPLLCLKUEN = 1;

	/* Wait for PLL lock. */
	while (!(LPC_SYSCFG_USBPLLSTAT & LPC_SYSCFG_PLLSTAT_LOCK))
		;

	
	LPC_SYSCFG_SYSAHBCLKCTRL |= LPC_SYSCFG_SYSAHBCLKCTRL_USB_REG
		| LPC_SYSCFG_SYSAHBCLKCTRL_IOCON;

	/* Select USB PLL as USB clock source */
	LPC_SYSCFG_USBCLKSEL = 0;
	LPC_SYSCFG_USBCLKUEN = 0;
	LPC_SYSCFG_USBCLKUEN = 1;
	LPC_SYSCFG_USBCLKDIV = 1;

	LPC_SYSCFG_PDRUNCFG &= ~LPC_SYSCFG_PDRUNCFG_USBPAD_PD;

	LPC_IOCON_PIO0_3 = 1; // USB_VBUS
	LPC_IOCON_PIO0_6 = 1; // USB_CONNECT

	/*
	 * With above initialization, we do not need
	 * pUSBD->init_clk_pins().
	 */

#ifdef BUILTIN_USB

	HidDevInfo.idVendor = CONFIG_USB_VID;
	HidDevInfo.idProduct = CONFIG_USB_PID;
	HidDevInfo.bcdDevice = CONFIG_USB_BCD_DEV;
	HidDevInfo.StrDescPtr = (uint32_t)&USB_HID_StringDescriptor[0];
	HidDevInfo.InReportCount = 1;
	HidDevInfo.OutReportCount = 1;
	HidDevInfo.SampleInterval = 0x20;
	HidDevInfo.InReport = &usb_hid_in_report;
	HidDevInfo.OutReport = &usb_hid_out_report;

	DeviceInfo.DevType = USB_DEVICE_CLASS_HUMAN_INTERFACE;
	DeviceInfo.DevDetailPtr = (uint32_t)&HidDevInfo;

	(*rom)->pUSBD->init(&DeviceInfo);
	(*rom)->pUSBD->connect(true);

#else
	LPC_USB_DEV_INT_CLR = 0xFFFFFFFF;
	//LPC_USB_DEV_INT_EN = LPC_USB_DEV_INT_EP(0) | LPC_USB_DEV_INT_DEV_STAT;
	LPC_USB_DEV_INT_EN = 0x03FE;

	uint8_t val = 1;
	sie_command(0xFE, &val, 1);  // Set Device Status

	task_enable_irq(LPR_IRQ_USB);
	usb_initialized = true;
#endif
}
DECLARE_HOOK(HOOK_INIT, usb_init, HOOK_PRIO_DEFAULT);

static void ep0_send_descriptor(const uint8_t *desc, int len,
				uint16_t fixup_size)
{
	ccprintf("  ep0_send_descriptor(%d)\n", len);
	/*
	 * if we cannot transmit everything at once,
	 * keep the remainder for the next IN packet
	 */
	if (len >= EP0_MAX_PACKET_SIZE) {
		desc_left = len - EP0_MAX_PACKET_SIZE;
		desc_ptr = desc + EP0_MAX_PACKET_SIZE;
		len = EP0_MAX_PACKET_SIZE;
	}

	LPC_USB_CTRL = BIT(1) | (0 << 2);
	(void)LPC_USB_DEV_INT_ST;
	(void)LPC_USB_DEV_INT_ST;
	LPC_USB_TX_PLEN = len;
	//ccprintf("  LPC_USB_CTRL = 0x%04x\n", LPC_USB_CTRL);

	if (((size_t)desc & 3) == 0) {
		for (int i = 0; i < (len + 3) / 4; i++) {
			uint32_t val = ((const uint32_t *)desc)[i];
			if (fixup_size && i == 0) /* set the real descriptor size */
				val = (val & 0x0000FFFF) | (fixup_size << 16);
			//ccprintf("    0x%08x\n", val);
			LPC_USB_TX_DATA = val;
		}
	} else {
		for (int i = 0; i < (len + 3) / 4; i++) {
			uint32_t val = desc[i * 4]
				+ (desc[i * 4 + 1] << 8)
				+ (desc[i * 4 + 2] << 16)
				+ (desc[i * 4 + 3] << 24);
			if (fixup_size && i == 0) /* set the real descriptor size */
				val = (val & 0x0000FFFF) | (fixup_size << 16);
			//ccprintf("    0x%08x\n", val);
			LPC_USB_TX_DATA = val;
		}
	}
	if (!len)
		LPC_USB_TX_DATA = 0;

	//ccprintf("  LPC_USB_CTRL = 0x%04x\n", LPC_USB_CTRL);

	uint8_t before = sie_command_read1(0x01);
	sie_command_nodata(0xFA);
	uint8_t after = sie_command_read1(0x01);
	ccprintf("  EP 0  IN: 0x%02x 0x%02x\n", before, after);
}

static void ep0_send(const uint8_t *data, int len)
{
	LPC_USB_CTRL = BIT(1) | (0 << 2);
	(void)LPC_USB_DEV_INT_ST;
	(void)LPC_USB_DEV_INT_ST;
	LPC_USB_TX_PLEN = len;

	if (((size_t)data & 3) == 0) {
		for (int i = 0; i < (len + 3) / 4; i++) {
			uint32_t val = ((const uint32_t *)data)[i];
			LPC_USB_TX_DATA = val;
		}
	} else {
		for (int i = 0; i < (len + 3) / 4; i++) {
			uint32_t val = data[i * 4]
				+ (data[i * 4 + 1] << 8)
				+ (data[i * 4 + 2] << 16)
				+ (data[i * 4 + 3] << 24);
			LPC_USB_TX_DATA = val;
		}
	}
	if (!len)
		LPC_USB_TX_DATA = 0;

	uint8_t before = sie_command_read1(0x01);
	sie_command_nodata(0xFA);
	uint8_t after = sie_command_read1(0x01);
	ccprintf("  EP 0  IN: 0x%02x 0x%02x\n", before, after);
}

static void handle_control_packet(void)
{
	LPC_USB_CTRL = BIT(0) | (0 << 2);
	(void)LPC_USB_DEV_INT_ST;
	(void)LPC_USB_DEV_INT_ST;
	uint16_t rx = LPC_USB_RX_PLEN;
	if (!(rx & 0x0400)) {
		ccprintf("  Odd, rx_len: 0x%04x\n", rx);
		return;
	}
	int len = rx & 0x03FF;
	ccprintf("  control, len=%d\n", len);
	for (int i = 0; i < (len + 3) / 4; i++) {
		(void)LPC_USB_RX_DATA;
	}
	uint8_t clear = sie_command_read1(0xF2);
	ccprintf("    clear 0x%02x\n", clear);
}

static void handle_setup_packet(void)
{
	LPC_USB_CTRL = BIT(0) | (0 << 2);
	(void)LPC_USB_DEV_INT_ST;
	(void)LPC_USB_DEV_INT_ST;
	uint16_t rx = LPC_USB_RX_PLEN;
	if (!(rx & 0x0400)) {
		ccprintf("  Odd, rx_len: 0x%04x\n", rx);
		return;
	}
	ccprintf("  setup, len=%d\n", rx & 0x03FF);
	uint32_t req1 = LPC_USB_RX_DATA;
	uint32_t req2 = LPC_USB_RX_DATA;
	//ccprintf("    0x%08x\n", req1);
	//ccprintf("    0x%08x\n", req2);
	uint8_t clear = sie_command_read1(0xF2);
	(void)clear;
	//ccprintf("    clear 0x%02x\n", clear);
	switch (req1 & 0x0000FFFF) {
	case USB_DIR_IN | (USB_REQ_GET_DESCRIPTOR << 8): {
		uint8_t type = req1 >> 24;
		uint8_t idx = req1 >> 16;
		const uint8_t *desc;
		int len;

		ccprintf("  GetDescriptor(0x%02x, 0x%02x)\n", type, idx);
		switch (type) {
		case USB_DT_DEVICE: /* Setup : Get device descriptor */
			desc = (void *)&dev_desc;
			len = sizeof(dev_desc);
			break;
		case USB_DT_CONFIGURATION: /* Setup : Get configuration desc */
			desc = __usb_desc;
			len = USB_DESC_SIZE;
			break;
#ifdef CONFIG_USB_BOS
		case USB_DT_BOS: /* Setup : Get BOS descriptor */
			desc = bos_ctx.descp;
			len = bos_ctx.size;
			break;
#endif
		case USB_DT_STRING: /* Setup : Get string descriptor */

#ifdef CONFIG_USB_MS_EXTENDED_COMPAT_ID_DESCRIPTOR
			/*
			 * String descriptor request at index == 0xEE is used by
			 * Windows OS to know how to retrieve an Extended Compat
			 * ID OS Feature descriptor.
			 */
			if (idx == USB_GET_MS_DESCRIPTOR) {
				desc = (uint8_t *)usb_ms_os_string_descriptor;
				len = desc[0];
				break;
			}
#endif
			if (idx >= USB_STR_COUNT)
				/* The string does not exist : STALL */
				goto unknown_req;
#ifdef CONFIG_USB_SERIALNO
			if (idx == USB_STR_SERIALNO)
				desc = (uint8_t *)usb_serialno_desc;
			else
#endif
				desc = usb_strings[idx];
			len = desc[0];
			break;
		case USB_DT_DEVICE_QUALIFIER: /* Get device qualifier desc */
			/* Not high speed : STALL next IN used as handshake */
			goto unknown_req;
		default: /* unhandled descriptor */
			ccprintf("Unhandled descriptor type 0x%02x\n", type);
			goto unknown_req;
		}
		ep0_send_descriptor(
			desc, MIN(req2 >> 16, len),
			type == USB_DT_CONFIGURATION ? USB_DESC_SIZE : 0);
		uint8_t ep_status = sie_command_read1(0x01);
		ccprintf("  EP 00 IN: 0x%02x\n", ep_status);
		break;
	}
	case (USB_REQ_SET_ADDRESS << 8): {
		uint8_t addr = req1 >> 16;
		ccprintf("  New address %d\n", addr);
		// Will take effect after status phase of this control transfer
		sie_command_write1(0xD0, addr | 0x80);

		LPC_USB_CTRL = BIT(1) | (0 << 2);
		(void)LPC_USB_DEV_INT_ST;
		(void)LPC_USB_DEV_INT_ST;
		LPC_USB_TX_PLEN = 0;
		//ccprintf("  LPC_USB_CTRL = 0x%04x\n", LPC_USB_CTRL);
		LPC_USB_TX_DATA = 0;
		//ccprintf("  LPC_USB_CTRL = 0x%04x\n", LPC_USB_CTRL);

		uint8_t before = sie_command_read1(0x01);
		sie_command_nodata(0xFA);
		uint8_t after = sie_command_read1(0x01);
		ccprintf("  EP 0  IN: 0x%02x 0x%02x\n", before, after);

		
		break;
	}
	case (USB_REQ_SET_CONFIGURATION << 8):
		LPC_USB_CTRL = BIT(1) | (0 << 2);
		(void)LPC_USB_DEV_INT_ST;
		(void)LPC_USB_DEV_INT_ST;
		LPC_USB_TX_PLEN = 0;
		//ccprintf("  LPC_USB_CTRL = 0x%04x\n", LPC_USB_CTRL);
		LPC_USB_TX_DATA = 0;
		//ccprintf("  LPC_USB_CTRL = 0x%04x\n", LPC_USB_CTRL);

		uint8_t before = sie_command_read1(0x01);
		sie_command_nodata(0xFA);
		uint8_t after = sie_command_read1(0x01);
		ccprintf("  EP 0  IN: 0x%02x 0x%02x\n", before, after);

		break;
	case USB_DIR_IN | (USB_REQ_GET_STATUS << 8): {
		uint32_t data = 0;
		/* Get status */
#ifdef CONFIG_USB_SELF_POWERED
		data |= USB_REQ_GET_STATUS_SELF_POWERED;
#endif
#ifdef CONFIG_USB_REMOTE_WAKEUP
		if (remote_wakeup_enabled)
			data |= USB_REQ_GET_STATUS_REMOTE_WAKEUP;
#endif
		ep0_send((uint8_t *)&data, 2);
		break;
	}
	default:
		ccprintf("  Unhandled SETUP request\n");
		goto unknown_req;
	}
	// Un-stall
	sie_command_write1(0x41, 0x00);
	return;

 unknown_req:
	// Stall
	sie_command_write1(0x41, 0x01);
	return;
}

static void usb_reset(void)
{
	int ep;

	for (ep = 0; ep < USB_EP_COUNT; ep++)
		usb_ep_event[ep](USB_EVENT_RESET);

	/*
	 * set the default address : 0
	 * as we are not configured yet
	 */
	sie_command_write1(0xD8, 0x01);
	sie_command_write1(0xF3, 0x06);
}


static void USB_IRQHandler(void)
{
#ifdef BUILTIN_USB
	(*rom)->pUSBD->isr();
#else
	uint32_t intr_status = LPC_USB_DEV_INT_ST;
	LPC_USB_DEV_INT_CLR = intr_status;

	//ccprintf("%llu: USB_IRQHandler(0x%08x)\n", get_time().val, intr_status & LPC_USB_DEV_INT_EN);

	if (intr_status & LPC_USB_DEV_INT_DEV_STAT) {
		uint8_t state = sie_command_read1(0xFE);
		ccprintf("  State 0x%02x\n", state);
		if (state & 0x10) {
			usb_reset();
		}
		//ccprintf("  err 0x%02x\n", sie_command_read1(0xFF));
		return;
	}
	if (intr_status & LPC_USB_DEV_INT_EP(1)) {
		uint8_t ep_status = sie_command_read1(0x41);
		(void)ep_status;
		//ccprintf("  EP*0  IN: 0x%02x\n", ep_status);
	}
	if (intr_status & LPC_USB_DEV_INT_EP(0)) {
		uint8_t ep_status = sie_command_read1(0x40);
		ccprintf("  EP*0 OUT: 0x%02x\n", ep_status);
		if ((ep_status & 0x03) == 0x01) {
			if (ep_status & 0x04) {
				handle_setup_packet();
			} else {
				handle_control_packet();
			}
		}
	}
	for (int ep = 1; ep <= 3; ep++) {
		if (intr_status & LPC_USB_DEV_INT_EP(1 + 2 * ep)) {
			uint8_t ep_status = sie_command_read1(0x41 + 2 * ep);
			(void)ep_status;
			usb_ep_tx[ep]();
		}
		if (intr_status & LPC_USB_DEV_INT_EP(2 * ep)) {
			uint8_t ep_status = sie_command_read1(0x40 + 2 * ep);
			(void)ep_status;
			usb_ep_rx[ep]();
		}
	}
	//ccprintf("  err 0x%02x\n", sie_command_read1(0xFF));
#endif
}
DECLARE_IRQ(LPR_IRQ_USB, USB_IRQHandler, 1);

int usb_is_enabled(void)
{
	return usb_initialized;
}

#ifdef CONFIG_USB_SERIALNO
/* This will be subbed into USB_STR_SERIALNO. */
struct usb_string_desc *usb_serialno_desc =
	USB_WR_STRING_DESC(DEFAULT_SERIALNO);
#endif

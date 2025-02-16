#include "common.h"
#include "console.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "usb_descriptor.h"

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

	
	LPC_SYSCFG_SYSAHBCLKCTRL |= LPC_SYSCFG_SYSAHBCLKCTRL_USB_REG;

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
}
DECLARE_HOOK(HOOK_INIT, usb_init, HOOK_PRIO_DEFAULT);

static void USB_IRQHandler(void)
{
	(*rom)->pUSBD->isr();
}
DECLARE_IRQ(LPR_IRQ_USB, USB_IRQHandler, 1);

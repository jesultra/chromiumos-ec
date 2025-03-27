/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_HW_H
#define __CROS_EC_USB_HW_H

#include <stddef.h>
#include <stdint.h>

/* Event types for the endpoint event handler. */
enum usb_ep_event {
	USB_EVENT_RESET,
	USB_EVENT_DEVICE_RESUME, /* Device-initiated wake completed. */
};

/* Helpers for endpoint declaration */
#define _EP_HANDLER2(num, suffix) CONCAT3(ep_, num, suffix)
#define _EP_TX_HANDLER(num) _EP_HANDLER2(num, _tx)
#define _EP_RX_HANDLER(num) _EP_HANDLER2(num, _rx)
#define _EP_EVENT_HANDLER(num) _EP_HANDLER2(num, _evt)
/* Used to check function types are correct (attribute alias does not do it) */
#define _EP_TX_HANDLER_TYPECHECK(num) _EP_HANDLER2(num, _tx_typecheck)
#define _EP_RX_HANDLER_TYPECHECK(num) _EP_HANDLER2(num, _rx_typecheck)
#define _EP_EVENT_HANDLER_TYPECHECK(num) _EP_HANDLER2(num, _evt_typecheck)

#define USB_DECLARE_EP(num, tx_handler, rx_handler, evt_handler)      \
	void _EP_TX_HANDLER(num)(void)                                \
		__attribute__((alias(STRINGIFY(tx_handler))));        \
	void _EP_RX_HANDLER(num)(void)                                \
		__attribute__((alias(STRINGIFY(rx_handler))));        \
	void _EP_EVENT_HANDLER(num)(enum usb_ep_event evt)            \
		__attribute__((alias(STRINGIFY(evt_handler))));       \
	static __unused void (*_EP_TX_HANDLER_TYPECHECK(num))(void) = \
		tx_handler;                                           \
	static __unused void (*_EP_RX_HANDLER_TYPECHECK(num))(void) = \
		rx_handler;                                           \
	static __unused void (*_EP_EVENT_HANDLER_TYPECHECK(num))(     \
		enum usb_ep_event evt) = evt_handler

/* arrays with all endpoint callbacks */
extern void (*usb_ep_tx[])(void);
extern void (*usb_ep_rx[])(void);
extern void (*usb_ep_event[])(enum usb_ep_event evt);

#endif /* __CROS_EC_USB_HW_H */

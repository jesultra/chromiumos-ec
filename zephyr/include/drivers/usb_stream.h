/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "consumer.h"
#include "producer.h"

#include <zephyr/net/buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usb_device.h>

#undef CONFIG_USB_I2C_MAX_WRITE_COUNT
#define CONFIG_USB_I2C_MAX_WRITE_COUNT         \
	(1024 - 4) /* 4 is maximum header size \
		    */

#undef CONFIG_USB_I2C_MAX_READ_COUNT
#define CONFIG_USB_I2C_MAX_READ_COUNT          \
	(1024 - 6) /* 6 is maximum header size \
		    */

// TODO: where? and need to check
#undef CONFIG_USB_I2C_MAX_WRITE_COUNT
#define CONFIG_USB_I2C_MAX_WRITE_COUNT         \
	(1024 - 4) /* 4 is maximum header size \
		    */

#undef CONFIG_USB_I2C_MAX_READ_COUNT
#define CONFIG_USB_I2C_MAX_READ_COUNT          \
	(1024 - 6) /* 6 is maximum header size \
		    */

struct usb_stream_config {
	struct consumer consumer;
	struct producer producer;
};

#if defined(CONFIG_USB_DEVICE_STACK)
#define AUTO_EP_IN 0x80
#define AUTO_EP_OUT 0x00

enum gvendor_ep_index {
	OUT_EP_IDX = 0,
	IN_EP_IDX,
	EP_NUM,
};

#define USB_GOOGLE_VENDOR_THREAD(NAME)                                        \
	static void NAME##_rx_thread(void *p1, void *p2, void *p3)            \
	{                                                                     \
		ARG_UNUSED(p1);                                               \
		ARG_UNUSED(p2);                                               \
		ARG_UNUSED(p3);                                               \
                                                                              \
		while (true) {                                                \
			struct net_buf *buf;                                  \
			const struct queue *usb_to_i2c = NAME.producer.queue; \
                                                                              \
			buf = net_buf_get(&NAME##_rx_queue, K_FOREVER);       \
			if (buf->len > queue_space(usb_to_i2c)) {             \
				continue;                                     \
			}                                                     \
			queue_add_units(usb_to_i2c, buf->data, buf->len);     \
			net_buf_unref(buf);                                   \
		}                                                             \
	};                                                                    \
                                                                              \
	static void NAME##_tx_thread(void *p1, void *p2, void *p3)            \
	{                                                                     \
		ARG_UNUSED(p1);                                               \
		ARG_UNUSED(p2);                                               \
		ARG_UNUSED(p3);                                               \
		struct usb_cfg_data *cfg = &CONCAT2(google_config_, NAME);    \
                                                                              \
		while (true) {                                                \
			struct net_buf *buf;                                  \
                                                                              \
			buf = net_buf_get(&NAME##_tx_queue, K_FOREVER);       \
                                                                              \
			usb_transfer_sync(cfg->endpoint[IN_EP_IDX].ep_addr,   \
					  buf->data, buf->len,                \
					  USB_TRANS_WRITE);                   \
                                                                              \
			net_buf_unref(buf);                                   \
		}                                                             \
	};                                                                    \
                                                                              \
	K_THREAD_DEFINE(rx_##NAME, CONFIG_GOOGLE_VENDOR_RX_STACK_SIZE,        \
			NAME##_rx_thread, NULL, NULL, NULL,                   \
			K_PRIO_COOP(CONFIG_GOOGLE_VENDOR_RX_THREAD_PRIORTY),  \
			0, 0);                                                \
	K_THREAD_DEFINE(tx_##NAME, CONFIG_GOOGLE_VENDOR_TX_STACK_SIZE,        \
			NAME##_tx_thread, NULL, NULL, NULL,                   \
			K_PRIO_COOP(CONFIG_GOOGLE_VENDOR_TX_THREAD_PRIORTY),  \
			0, 0);

/* Coreboot only parses the first interface descriptor for boot keyboard
 * detection. The section name format in RAM is
 * ".usb.descriptor_primary.1.<instance>" and the USB descriptors are
 * sorted by name in the linker scripts. The string "vendor_google_<NAME>"
 * is set in the instance field to ensure that the Google vendor
 * descriptor is placed after the HID class or Google dummy class.
 */
#define GET_INST_FIELD(NAME) vendor_google_##NAME

#define INITIALIZER_IF(num_ep, iface_class, iface_subclass, iface_proto)      \
	{                                                                     \
		.bLength = sizeof(struct usb_if_descriptor),                  \
		.bDescriptorType = USB_DESC_INTERFACE, .bInterfaceNumber = 0, \
		.bAlternateSetting = 0, .bNumEndpoints = num_ep,              \
		.bInterfaceClass = iface_class,                               \
		.bInterfaceSubClass = iface_subclass,                         \
		.bInterfaceProtocol = iface_proto, .iInterface = 0,           \
	}

#define INITIALIZER_IF_EP(addr, attr, mps)                              \
	{                                                               \
		.bLength = sizeof(struct usb_ep_descriptor),            \
		.bDescriptorType = USB_DESC_ENDPOINT,                   \
		.bEndpointAddress = addr, .bmAttributes = attr,         \
		.wMaxPacketSize = sys_cpu_to_le16(mps), .bInterval = 0, \
	}

#define GVENDOR_NAME(NAME) GET_NAME(NAME)
#define USB_GOOGLE_VENDOR_DEFINE(NAME, SUBCLASS, PROTOCOL)                       \
	NET_BUF_POOL_FIXED_DEFINE(NAME##_rx_pool, 10, USB_MAX_FS_BULK_MPS,       \
				  USB_MAX_FS_BULK_MPS, NULL);                    \
	NET_BUF_POOL_FIXED_DEFINE(NAME##_tx_pool, 10, USB_MAX_FS_BULK_MPS,       \
				  USB_MAX_FS_BULK_MPS, NULL);                    \
	static K_FIFO_DEFINE(NAME##_rx_queue);                                   \
	static K_FIFO_DEFINE(NAME##_tx_queue);                                   \
                                                                                 \
	static struct usb_ep_cfg_data CONCAT2(NAME, _ep_cfg)[EP_NUM] = {       \
		[OUT_EP_IDX] = {                                               \
			.ep_cb = usb_transfer_ep_callback,                     \
			.ep_addr = AUTO_EP_OUT,                                \
		},                                                             \
		[IN_EP_IDX] = {                                                \
			.ep_cb = usb_transfer_ep_callback,                     \
			.ep_addr = AUTO_EP_IN,                                 \
		},                                                             \
	}; \
                                                                                 \
	static void CONCAT2(NAME, _read)(uint8_t ep, int size, void *priv)       \
	{                                                                        \
		ARG_UNUSED(priv);                                                \
                                                                                 \
		static uint8_t data[USB_MAX_FS_BULK_MPS];                        \
                                                                                 \
		if (size > 0) {                                                  \
			struct net_buf *buf;                                     \
			buf = net_buf_alloc(&NAME##_rx_pool, K_NO_WAIT);         \
			if (!buf) {                                              \
				printk("%s failed to allcate rx memory\n",       \
				       __func__);                                \
				return;                                          \
			}                                                        \
			net_buf_add_mem(buf, data, size);                        \
			net_buf_put(&NAME##_rx_queue, buf);                      \
		}                                                                \
		usb_transfer(ep, data, USB_MAX_FS_BULK_MPS, USB_TRANS_READ,      \
			     CONCAT2(NAME, _read), NULL);                        \
	};                                                                       \
                                                                                 \
	static void CONCAT2(NAME, _status_cb)(struct usb_cfg_data * cfg,         \
					      enum usb_dc_status_code status,    \
					      const uint8_t *param)              \
	{                                                                        \
		ARG_UNUSED(param);                                               \
                                                                                 \
		switch (status) {                                                \
		case USB_DC_CONFIGURED:                                          \
			CONCAT2(NAME, _read)                                     \
			(cfg->endpoint[OUT_EP_IDX].ep_addr, 0, NULL);            \
			break;                                                   \
		default:                                                         \
			break;                                                   \
		}                                                                \
	};                                                                       \
                                                                                 \
	void CONCAT2(usb_written_, NAME)(struct consumer const *consumer,        \
					 size_t count)                           \
	{                                                                        \
		static uint8_t data[USB_MAX_FS_BULK_MPS];                        \
		struct net_buf *buf;                                             \
                                                                                 \
		if (queue_is_empty(consumer->queue)) {                           \
			return;                                                  \
		}                                                                \
		do {                                                             \
			count = (count > USB_MAX_FS_BULK_MPS) ? 64 : count;      \
			queue_peek_units(consumer->queue, data, 0, count);       \
			buf = net_buf_alloc(&NAME##_tx_pool, K_NO_WAIT);         \
			if (!buf) {                                              \
				printk("%s failed to allcate tx memory\n",       \
				       __func__);                                \
				return;                                          \
			}                                                        \
                                                                                 \
			net_buf_add_mem(buf, data, count);                       \
			net_buf_put(&NAME##_tx_queue, buf);                      \
			queue_advance_head(consumer->queue, count);              \
			count = queue_count(consumer->queue);                    \
		} while (count != 0);                                            \
	}                                                                        \
                                                                                 \
	struct CONCAT2(NAME, _config) {                                          \
		struct usb_if_descriptor if0;                                    \
		struct usb_ep_descriptor if0_out_ep;                             \
		struct usb_ep_descriptor if0_in_ep;                              \
	} __packed;                                                              \
                                                                                 \
	USBD_CLASS_DESCR_DEFINE(primary, GET_INST_FIELD(NAME))                   \
	struct CONCAT2(NAME, _config) CONCAT2(NAME, _cfg) = {                    \
		.if0 = INITIALIZER_IF(EP_NUM, USB_BCC_VENDOR, SUBCLASS,          \
				      PROTOCOL),                                 \
		.if0_out_ep = INITIALIZER_IF_EP(AUTO_EP_OUT, USB_DC_EP_BULK,     \
						USB_MAX_FS_BULK_MPS),            \
		.if0_in_ep = INITIALIZER_IF_EP(AUTO_EP_IN, USB_DC_EP_BULK,       \
					       USB_MAX_FS_BULK_MPS),             \
	};                                                                       \
                                                                                 \
	static void CONCAT2(NAME, _interface_config)(                            \
		struct usb_desc_header * head, uint8_t bInterfaceNumber)         \
	{                                                                        \
		ARG_UNUSED(head);                                                \
                                                                                 \
		CONCAT2(NAME, _cfg).if0.bInterfaceNumber = bInterfaceNumber;     \
		return;                                                          \
	};                                                                       \
                                                                                 \
	USBD_DEFINE_CFG_DATA(CONCAT2(google_config_, NAME)) = {                         \
		.usb_device_description = NULL,                                  \
		.interface_config = CONCAT2(NAME, _interface_config),            \
		.interface_descriptor = &CONCAT2(NAME, _cfg).if0,                \
		.cb_usb_status = CONCAT2(NAME, _status_cb),                      \
		.interface = {                                                   \
			.class_handler = NULL,                                   \
			.custom_handler = NULL,                                  \
			.vendor_handler = NULL,                                  \
		},                                                               \
		.num_endpoints = ARRAY_SIZE(CONCAT2(NAME, _ep_cfg)),             \
		.endpoint = CONCAT2(NAME, _ep_cfg),                              \
	};
#else
#define USB_GOOGLE_VENDOR_DEFINE(NAME, SUBCLASS, PROTOCOL)
#define USB_GOOGLE_VENDOR_THREAD(NAME)
#endif /* CONFIG_USB_DEVICE_STACK */

#define USB_STREAM_CONFIG_FULL(NAME, INTERFACE, INTERFACE_CLASS,             \
			       INTERFACE_SUBCLASS, INTERFACE_PROTOCOL,       \
			       INTERFACE_NAME, ENDPOINT, RX_SIZE, TX_SIZE,   \
			       RX_QUEUE, TX_QUEUE, RX_IDX, TX_IDX)           \
	USB_GOOGLE_VENDOR_DEFINE(NAME, INTERFACE_SUBCLASS,                   \
				 INTERFACE_PROTOCOL);                        \
	static const struct consumer_ops CONCAT2(consumer_ops_, NAME) = {    \
		.written = CONCAT2(usb_written_, NAME),                      \
	};                                                                   \
	static const struct producer_ops CONCAT2(producer_ops_, NAME) = {    \
		.read = NULL,                                                \
	};                                                                   \
	const struct usb_stream_config NAME = {                            \
		.consumer = {                                              \
			.queue = &TX_QUEUE,                                \
			.ops = &CONCAT2(consumer_ops_, NAME),                     \
		},                                                         \
		.producer = {                                              \
			.queue = &RX_QUEUE,                                \
			.ops = &CONCAT2(producer_ops_, NAME),                     \
		},                                                         \
	}; \
	USB_GOOGLE_VENDOR_THREAD(NAME)

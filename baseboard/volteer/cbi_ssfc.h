/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _VOLTEER_CBI_SSFC__H_
#define _VOLTEER_CBI_SSFC__H_

#include "stdint.h"

/****************************************************************************
 * Volteer CBI Second Source Factory Cache
 */

/*
 * Base Sensor (Bits 0-2)
 */
enum ec_ssfc_base_sensor {
	SSFC_SENSOR_BASE_DEFAULT = 0,
	SSFC_SENSOR_BASE_BMI160 = 1,
	SSFC_SENSOR_BASE_ICM426XX = 2
};

/*
 * Lid Sensor (Bits 3-5)
 */
enum ec_ssfc_lid_sensor {
	SSFC_SENSOR_LID_DEFAULT = 0,
	SSFC_SENSOR_LID_BMA255 = 1,
	SSFC_SENSOR_LID_KX022 = 2
};

/*
 * Lightbar (Bits 6-7)
 */
enum ec_ssfc_lightbar {
	SSFC_LIGHTBAR_NONE = 0,
	SSFC_LIGHTBAR_10_LED = 1,
	SSFC_LIGHTBAR_12_LED = 2
};

/*
 * USB PD VBUS detect (Bits 8-10)
 */
enum ec_ssfc_usb_pd_vbus_detect {
	SSFC_USB_PD_VBUS_DETECT_NONE = 0,
	SSFC_USB_PD_VBUS_DETECT_TCPC = 1,
	SSFC_USB_PD_VBUS_DETECT_GPIO = 2,
	SSFC_USB_PD_VBUS_DETECT_PPC = 3,
	SSFC_USB_PD_VBUS_DETECT_CHARGER = 4
};

/*
 * USB PD DISCHARGE (Bits 11-12)
 */
enum ec_ssfc_usb_pd_discharge {
	SSFC_USB_PD_DISCHARGE_NONE = 0,
	SSFC_USB_PD_DISCHARGE_TCPC = 1,
	SSFC_USB_PD_DISCHARGE_GPIO = 2,
	SSFC_USB_PD_DISCHARGE_PPC = 3
};

/*
 * USB PD Charger OTG (Bit 13)
 */
enum ec_ssfc_charger_otg {
	SSSFC_USB_PD_CHARGER_OTG_DISABLED = 0,
	SSSFC_USB_PD_CHARGER_OTG_ENABLED = 1
};

union volteer_cbi_ssfc {
	struct {
		enum ec_ssfc_base_sensor base_sensor : 3;
		enum ec_ssfc_lid_sensor lid_sensor : 3;
		enum ec_ssfc_lightbar lightbar : 2;
		enum ec_ssfc_usb_pd_vbus_detect vbus_detect : 3;
		enum ec_ssfc_usb_pd_discharge discharge : 2;
		enum ec_ssfc_charger_otg charger_otg : 1;
		uint32_t reserved_2 : 18;
	};
	uint32_t raw_value;
};

/**
 * Get the Base sensor type from SSFC_CONFIG.
 *
 * @return the Base sensor board type.
 */
enum ec_ssfc_base_sensor get_cbi_ssfc_base_sensor(void);

/**
 * Get the Lid sensor type from SSFC_CONFIG.
 *
 * @return the Lid sensor board type.
 */
enum ec_ssfc_lid_sensor get_cbi_ssfc_lid_sensor(void);

/**
 * Get lightbar type from SSFC_CONFIG.
 *
 * @return the lightbar type.
 */
enum ec_ssfc_lightbar get_cbi_ssfc_lightbar(void);

/**
 * Get VBUS detect type from SSFC_CONFIG.
 *
 * @return the VBUS detect type.
 */
enum ec_ssfc_usb_pd_vbus_detect get_cbi_ssfc_vbus_detect(void);

/**
 * Get USB PD discharge type from SSFC_CONFIG.
 *
 * @return the USB PD discharge type.
 */
enum ec_ssfc_usb_pd_discharge get_cbi_ssfc_usb_pd_discharge(void);

/**
 * Get USB PD charger OTG from SSFC_CONFIG.
 *
 * @return the USB PD charger OTG.
 */
enum ec_ssfc_charger_otg get_cbi_ssfc_charger_otg(void);

#endif /* _Volteer_CBI_SSFC__H_ */

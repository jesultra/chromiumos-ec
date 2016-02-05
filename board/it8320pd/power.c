/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "common.h"
#include "console.h"
#include "power.h"
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* declaration of struct and enum*/

/*declaration of Public variable*/

/*declaration of Private variable*/

/*declaration of Private function*/

/*Public function*/
void power_init(uint8_t u8Port)
{
	/*set GPIO A4,A5 to 3.3V*/
	IT83XX_GPIO_GRC24 = 0;

	/*HVLDO disable*/
	 IT83XX_GPIO_GPCRA1 = OUTPUT;
	 IT83XX_GPIO_GPDRA &= ~BIT1;

	/*Don't drop the Volt*/
	POWER_A_VBUS_DROP = OUTPUT;
	POWER_B_VBUS_DROP = OUTPUT;
	POWER_A_DISABLE_VBUS_DROP();
	POWER_B_DISABLE_VBUS_DROP();

	if (u8Port == USBPD_PORT_A) {
		POWER_A_CC1_VCONN = OUTPUT;
		POWER_A_CC2_VCONN = OUTPUT;
		POWER_A_VBUS_INPUT = OUTPUT;
		POWER_A_VBUS_OUTPUT = OUTPUT;
	} else {
		POWER_B_CC1_VCONN = OUTPUT;
		POWER_B_CC2_VCONN = OUTPUT;
		POWER_B_VBUS_INPUT = OUTPUT;
		POWER_B_VBUS_OUTPUT = OUTPUT;
	}

	/*disable all*/
	power_enable_vconn(u8Port, 0, FALSE);
	power_enable_vbus(u8Port, FALSE);
}

void power_enable_vconn(uint8_t u8Port, uint32_t u32CcPin, uint8_t bIsEnable)
{
	if (bIsEnable == TRUE) {
		if (USBPD_CC_PIN_1 == u32CcPin) {
			if (u8Port == USBPD_PORT_A) {
				POWER_A_DISABLE_CC2_VCONN();
				POWER_A_ENABLE_CC1_VCONN();
			} else {
				POWER_B_DISABLE_CC2_VCONN();
				POWER_B_ENABLE_CC1_VCONN();
			}
		} else {
			if (u8Port == USBPD_PORT_A) {
				POWER_A_DISABLE_CC1_VCONN();
				POWER_A_ENABLE_CC2_VCONN();
			} else {
				POWER_B_DISABLE_CC1_VCONN();
				POWER_B_ENABLE_CC2_VCONN();
			}
		}
	} else {
		if (u8Port == USBPD_PORT_A) {
			POWER_A_DISABLE_CC1_VCONN();
			POWER_A_DISABLE_CC2_VCONN();
		} else {
			POWER_B_DISABLE_CC1_VCONN();
			POWER_B_DISABLE_CC2_VCONN();
		}
	}
}

void power_enable_vbus(uint8_t u8Port, uint8_t bIsEnable)
{
	if (TRUE == bIsEnable) {
		if (u8Port == USBPD_PORT_A) {
			POWER_A_DISABLE_VBUS_INPUT();
			POWER_A_ENABLE_VBUS_OUTPUT();
			POWER_A_DISABLE_VBUS_DROP();

		} else {
			POWER_B_DISABLE_VBUS_INPUT();
			POWER_B_ENABLE_VBUS_OUTPUT();
			POWER_A_DISABLE_VBUS_DROP();
		}
	} else{
		if (u8Port == USBPD_PORT_A) {
			POWER_A_DISABLE_VBUS_OUTPUT();
			POWER_A_ENABLE_VBUS_INPUT();
			/* drop vlot */
			POWER_A_ENABLE_VBUS_DROP();
			POWER_A_DISABLE_VBUS_DROP();
		} else {
			POWER_B_DISABLE_VBUS_OUTPUT();
			POWER_B_ENABLE_VBUS_INPUT();
			/* drop vlot */
			POWER_B_ENABLE_VBUS_DROP();
			POWER_B_DISABLE_VBUS_DROP();
		}
	}
}


uint8_t power_is_enable_vbus(uint8_t u8Port)
{
	if (u8Port == USBPD_PORT_A)
		return POWER_A_IS_ENABLE_VBUS_OUTPUT();
	else
		return POWER_B_IS_ENABLE_VBUS_OUTPUT();

}


void power_enable_drop(uint8_t u8Port, uint8_t bEnable)
{
	/* drop vlot */
	if (TRUE == bEnable) {
		if (u8Port == USBPD_PORT_A)
			POWER_A_ENABLE_VBUS_DROP();
		else
			POWER_B_ENABLE_VBUS_DROP();
	} else {
		if (u8Port == USBPD_PORT_A)
			POWER_A_DISABLE_VBUS_DROP();
		else
			POWER_B_DISABLE_VBUS_DROP();
	}
}


void power_set_volt(uint8_t u8Port, uint8_t u8Dir, uint32_t u32mv)
{

}

uint32_t power_get_m_volt(uint8_t u8Port)
{
	uint32_t volt = 0;

	volt = adc_read_channel(u8Port) * 1000; /*ADC change to mADC*/

	/*CPRINTF("port%d, volt:%d\n", u8Port, volt);*/

	volt = volt / 1024 * 3; /*get adc mV*/
	/*get adc mV.VACC DL is 3.3, but actual is 3.367*/
	volt = volt * 3367 / 3300;
	volt = (volt * 23) / 3; /*translate to real mV*/

#if 0
	/*only for demo board*/
	volt = volt / 1024 * 3; /*get adc mV*/
	/*get adc mV.	VACC DL is 3.3, but actual is 3.367*/
	volt = volt * 3.367 / 3.3;

	if (USBPD_PORT_A == u8Port)
		volt *= 7.8675;
	else
		volt *= 7.6829;
#endif
	return (uint32_t)volt;
}

void power_adc_init(void)
{
/*
    // Initial
    ADCCTL	= 0x15;
    KDCTL	|= 0x80;		//enable HW Cal

    ADC0	= ALT;			//GPI0 as ADC0
    ADC1	= ALT;			//GPI1 as ADC1

    VCH0CTL = 0x00;			//Channel 0 for Resister Div.
    VCH1CTL = 0x01;			//Channel 1 for Resister Div.

	//ADC enable. It will always update the vlotage
    ADCCFG |= 0x01;			//step 11; ADCEN
    return;
    */
}


uint32_t power_adc_detect(uint8_t u8Port)
{
	if (u8Port == USBPD_PORT_A)
		return ((IT83XX_ADC_VCH0DATM<<8) + IT83XX_ADC_VCH0DATL)
			& 0x000003FF;
	else
		return ((IT83XX_ADC_VCH1DATM<<8) + IT83XX_ADC_VCH1DATL)
			& 0x000003FF;
}

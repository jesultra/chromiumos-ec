/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PD driver */

#include "common.h"
#include "console.h"
#include "power.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd_tcpm.h"
#include "usb_pd_config.h"


#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)

/* 8320-defined, phy layer serve tcpc for register message */
int usbpd_is_link(int port)
{
	if (USBPD_DATA_ROLE(port) == USBPD_OPERATION_ROLE_UFP) {
		if (UFPVDR(port) == 0x00)
			return 0;
	} else if (USBPD_DATA_ROLE(port) == USBPD_OPERATION_ROLE_DFP) {
		if (DFPVDR(port) == 0x33)
			return 0;
	}
	return 1;
}

int usbpd_detect_vbus(int port)
{
	if (power_get_m_volt(port))
		return 1;
	else
		return 0;
}

int DFPVDR_check(int value)
{
	if ((value != 0x13) && (value != 0x31) && (value != 0x10) &&
		(value != 0x01) && (value != 0x33))
		return 0;
	else
		return 1;
}

int UFPVDR_check(int value)
{
	if ((value != 0x10) && (value != 0x30) && (value != 0x70) &&
		(value != 0x01) && (value != 0x03) && (value != 0x07))
		return 0;
	else
		return 1;
}


int usbpd_get_cc(int port, int cc_pin)
{
	uint8_t u8cc_status = 0x00;
	uint8_t cc_mask = 0;

	if (USBPD_GET_PULL_REGISTER_SELECTION(port) == 0x1)
		CLEAR_MASK(u8cc_status , Connection_Result);
	else if (USBPD_GET_PULL_REGISTER_SELECTION(port) == 0x0)
		SET_MASK(u8cc_status , Connection_Result);

	if (cc_pin == USBPD_CC_PIN_1)
		cc_mask = 0x0f;
	else
		cc_mask = 0xf0;


cc_busy:
	if (USBPD_POWER_ROLE_CONSUMER  == USBPD_GET_POWER_ROLE(port)) {
		switch ((UFPVDR(port) & cc_mask)) {
		case 0x01:
			u8cc_status |= (CC_State_SNK_OPEN << 2) |
				CC_State_SNK_DEF;
			break;
		case 0x03:
			u8cc_status |= (CC_State_SNK_OPEN << 2) |
				CC_State_SNK_1_5;
			break;
		case 0x07:
			u8cc_status |= (CC_State_SNK_OPEN << 2) |
				CC_State_SNK_3_0;
			break;
		case 0x10:
			u8cc_status |= (CC_State_SRC_RD << 2) |
				CC_State_SNK_OPEN;
			break;
		case 0x30:
			u8cc_status |= (CC_State_SNK_1_5 << 2) |
				CC_State_SNK_OPEN;
			break;
		case 0x70:
			u8cc_status |= (CC_State_SNK_3_0 << 2) |
				CC_State_SNK_OPEN;
			break;
		case 0x00:
			return TYPEC_CC_VOLT_OPEN;
		default:
			/*
			if (port == 0)
				CPRINTF("port:%d, UFP_stat:0x%x,cc busy\n",
					port, UFPVDR(port));
			*/
			goto cc_busy;
		}
	} else {
		switch (DFPVDR(port)) {
		case 0x31:
			u8cc_status |= (CC_State_SRC_OPEN << 2) |
				CC_State_SRC_RD;
			break;
		case 0x13:
			u8cc_status |= (CC_State_SRC_RD << 2) |
				CC_State_SRC_OPEN;
			break;
		case 0x01:
			u8cc_status |= (CC_State_SRC_RA << 2) |
				CC_State_SRC_RD;
			break;
		case 0x10:
			u8cc_status |= (CC_State_SRC_RD << 2) |
				CC_State_SRC_RA;
			break;
		case 0x33:
			return TYPEC_CC_VOLT_OPEN;
		case 0x03:
			return TYPEC_CC_VOLT_OPEN;
		case 0x30:
			return TYPEC_CC_VOLT_OPEN;
		default:
			/*
			if (port == 0)
				CPRINTF("port:%d, DFP_stat:0x%x,cc busy\n",
					port, DFPVDR(port));
			*/
			goto cc_busy;
		}
	}
	if (cc_pin == USBPD_CC_PIN_1)
		return (((u8cc_status & BIT(4)) >> 4) << 2) |
			(u8cc_status & 0x3);
	else
		return (((u8cc_status & BIT(4)) >> 2) |
			((u8cc_status & 0xc) >> 2));
}


enum usbpd_result usbpd_rx_data(enum usbpd_port port
				, enum usbpd_sop_type *p_enumSopType
				, void *p_TmpHead
				, uint32_t *p_u32Buf)
{

	struct usbpd_header *p_header = (struct usbpd_header *)p_TmpHead;

	if (FALSE == USBPD_IS_RX_DONE(port))
		return USBPD_RESULT_NODATA;

	/* clear interrupt */
	USBPD_ISR(port) = USBPD_MSG_RX_DONE;

	/* get sop type */
	*p_enumSopType = USBPD_GET_RX_SOP_TYPE(port);

	/* store header */
	*p_header = *((struct usbpd_header *)RMH_BASE(port));

	/* check data message */
	if (p_header->u8DataObjNum > 0)
		memcpy(p_u32Buf, (uint8_t *)RDO_BASE(port)
			, p_header->u8DataObjNum*4);

	/* Note: clear RX done interrupt after get the data!!
	 * If clear this bit, USBPD receives next packet
	 */
	MRSR(port) = USBPD_RX_MSG_VALID;

	return USBPD_RESULT_SUCC;
}

enum usbpd_result usbpd_tx_data(enum usbpd_port port
				, enum usbpd_sop_type enumSopType
				, uint8_t u8MsgType
				, uint8_t u8DataLength
				, const uint32_t *p_u32Buf)
{
	uint8_t u8Cnt = 0;

	/* set message type */
	MTSR0(port) = (MTSR0(port) & ~0x1F) | (u8MsgType & 0xF);

	/* set SOP type */
	MTSR1(port) = (MTSR1(port) & ~0x30) | ((enumSopType & 0x3) << 4);

	/* set cable or not */
	if (USBPD_SOP_TYPE_SOP == enumSopType)
		MTSR0(port) &= ~USBPD_CABLE_ENABLE;
	else
		MTSR0(port) |= USBPD_CABLE_ENABLE;

	/* clear msg length */
	MTSR1(port) &= (~0x7);

	if (u8DataLength > 0) {
		/* set data bit */
		MTSR0(port) |= BIT4;

		/* set data length setting */
		MTSR1(port) |= (u8DataLength & 0x7);

		/* set data */
		memcpy((uint8_t *)TDO_BASE(port), p_u32Buf, u8DataLength*4);
	}

PD_TX_SEND:

	/* Start TX */
	USBPD_KICK_TX_START(port);

	/* wait for TXing or not */
	while (FALSE == USBPD_IS_TX_DONE(port))
			;

	/* Get TX header(should wait TX done) */

	/* clear TX done interrupt */
	USBPD_ISR(port) = USBPD_MSG_TX_DONE;

	/* check TX status */
	if (USBPD_IS_TX_ERR(port)) {
		/* If discard, means HW doesn't send the msg. Should resend */
		if (USBPD_IS_TX_DISCARD(port)) {
			if (u8Cnt++ < 5)
				goto PD_TX_SEND;
			else
				return USBPD_RESULT_TX_DISCARD;
		} else
			return USBPD_RESULT_FAIL;
	}

	return USBPD_RESULT_SUCC;
}



void usbpd_hw_reset(enum usbpd_port port, enum usbpd_reset_type reset_type)
{
	if (TRUE == USBPD_IS_SNIFFER_MODE(port))
		return;

	if (reset_type == USBPD_RESET_TYPE_CABLE)
		MTSR0(port) |= USBPD_CABLE_ENABLE;
	else
		MTSR0(port) &= ~USBPD_CABLE_ENABLE;

	USBPD_HW_RESET(port);
}


void usbpd_sw_reset(enum usbpd_port port)
{
	USBPD_SW_RESET(port);
}

void usbpd_bist_mode_2_tx(int port)
{
	USBPD_ENABLE_SEND_BIST_MODE_2(port);
	task_wait_event(PD_T_BIST_TRANSMIT);
	USBPD_DISABLE_SEND_BIST_MODE_2(port);
}

void usbpd_enable_vconn(enum usbpd_port port, uint8_t bIsEnable)
{
	enum usbpd_cc_pin cc_pin;

	if (USBPD_GET_PULL_CC_SELECTION(port))
		cc_pin = USBPD_CC_PIN_1;
	else
		cc_pin = USBPD_CC_PIN_2;

	if (TRUE == bIsEnable) {
		/* Set status to USBPD */
		USBPD_ENABLE_VCONN(port);

		/* Disable unused CC to become VCONN */
		if (cc_pin == USBPD_CC_PIN_1) {
			CCCSR(port) = (CCCSR(port) | 0xa0) & ~0x0a;
			CCPSR(port) = (CCPSR(port)
					& ~USBPD_DISCONNECT_POWER_CC2)
					| USBPD_DISCONNECT_POWER_CC1;
		} else {
		/* Enable cc2 and Disable cc1  for VCONN */
			CCCSR(port) = (CCCSR(port) | 0x0a) & ~0xa0;
		/* enable CC1 VCONN */
			CCPSR(port) = (CCPSR(port)
					& ~USBPD_DISCONNECT_POWER_CC1)
					| USBPD_DISCONNECT_POWER_CC2;
		}
	} else {
		/* Set status to USBPD */
		USBPD_DISABLE_VCONN(port);

		/* Enable cc1 and CC2 */
		CCCSR(port) &= ~0xaa;
		CCPSR(port) |= (USBPD_DISCONNECT_POWER_CC1
				| USBPD_DISCONNECT_POWER_CC2);
	}

	/* VCONN Switch */
	power_enable_vconn(port, !cc_pin, bIsEnable);
}

void usbpd_set_power_role(int port, int powerRole)
{
	/* PD_ROLE_SINK   0, PD_ROLE_SOURCE 1 */

	#ifdef CONFIG_USB_PD_DUAL_ROLE
	/* set dual role */
	SET_MASK(PDMSR(port), BIT1);
	#endif

	if (powerRole == PD_ROLE_SOURCE)
		SET_MASK(PDMSR(port), BIT0);
	else
		CLEAR_MASK(PDMSR(port), BIT0);


	if (powerRole == PD_ROLE_SOURCE)
		SET_MASK(CCGCR(port), BIT1);
	else
		CLEAR_MASK(CCGCR(port), BIT1);

	/* Source enable to receive cable for spec.(Not follow VCONN power) */
	if (powerRole == PD_ROLE_SOURCE)
		USBPD_ENABLE_SOP_CABLE(port);
	else
		USBPD_DISABLE_SOP_CABLE(port);
}

void usbpd_set_operation_role(int port, int dataRole)
{
	/* 0: PD_ROLE_UFP 1: PD_ROLE_DFP */
	PDMSR(port) = (PDMSR(port) & ~0xc) | ((dataRole & 0x1) << 2);

#ifdef CONFIG_USB_PD_DUAL_ROLE
	SET_MASK(PDMSR(port), BIT3);
#endif
}

void usbpd_reset(int port, int role)
{
	/* reset module */
	/* usbpd_proto_init(port); */
	/* disable Power  */
	power_init(port);
	power_adc_init();
	/* set HW setting */
	USBPD_GCR(port) = 0;

	/* USBPD_GCR(port)	|= USBPD_AUTO_SEND_HW_RESET; */
	/* USBPD_GCR(port)	|= USBPD_AUTO_SEND_SW_RESET; */

	USBPD_GCR(port) |= USBPD_BMC_PHY;

	/* set basic information */
	usbpd_hw_reset_reg(port, role);
	/* set isr */
	IMR(port) = USBPD_TIMER_TIMEOUT | USBPD_AUTO_SOFT_RESET_TX_DONE |
				USBPD_MSG_TX_DONE | USBPD_HARD_RESET_TX_DONE;

	/* enable CC  */
	CCCSR(port) = 0;

	/* disable VConn */
	usbpd_enable_vconn(port, FALSE);
	CCPSR(port) = 0xff;

	CCPSR0(port) = (USBPD_TX_SWING & 0x7) << 4 |
			(USBPD_TX_DRIVING & 0x3) << 2 |
			(USBPD_TX_FC_FILTER & 0x3);
	CCPSR3(port) = USBPD_TX_PRE_DRIVING & 0x3;

	/* tuning RD Resistor and IP Current */
	CCPSR1(port) = 0x81;
	CCPSR2(port) = 0x82;

	/* TODO  should move to other place */
	 USBPD_START(port);
}

void usbpd_hw_reset_reg(int port, int role)
{
	/* SW reset(reset HW stat machine) */
	USBPD_SW_RESET(port);
#if 0
	if (stUsbPdMgr[port].u32FuncEnable & USBPD_FUNC_GLOBAL_ENABLE)
		USBPD_GCR(port)	&= ~USBPD_GLOBAL_ENABLE;
#endif
	/* reset SOP */
	PDMSR(port) = SOP_ENABLE;
	/* disable PD feature */
	PDCSR(port) = 0;

	/* Clear status */
	TSR0(port) = 0xFF;
	TSR1(port) = 0xFF;
	USBPD_ISR(port) = 0xFF;
#if 0
	/* set bus idle offset */
	BMCSR(port) = (BMCSR(port) & ~0x60)
		| (stUsbPdMgr[port].u8BusIdleOffset << 5);
#endif
	/* timeout mask */
	TIMR0(port) = USBPD_TIMEOUT_CRC_RX_BIT;
	TIMR1(port) = 0x0;

	/* DFP, enable CC and set curent */
	CCGCR(port) = USBPD_DEF_DFT_CUR;

	/* change data role as the same power role */
	usbpd_set_operation_role(port, role);
	/* Set Power role */
	usbpd_set_power_role(port, role);
}


void usbpd_init(int port, int role)
{
	uint8_t u8UsbPdClkTb[8] = {0, 1, 2, 3, 5, 7, 8, 0xb};
	/* power_init(port); */
	usbpd_reset(port, role);

	/* init PD IRQ  */
	task_clear_pending_irq(IT83XX_IRQ_USBPD0);
	task_enable_irq(IT83XX_IRQ_USBPD0);
	task_clear_pending_irq(IT83XX_IRQ_USBPD1);
	task_enable_irq(IT83XX_IRQ_USBPD1);

	/* defalut PD Clock = PLL 48 / 6 = 8M. */
	/* If enable USB, should set SMB Clock = PLL 96 / 4 = 24M */
	if ((IT83XX_ECPM_PLLFREQR & 0xf) <= PLL_96M)
		IT83XX_ECPM_SCDCR4 = (IT83XX_ECPM_SCDCR4 & 0xF0)
				| u8UsbPdClkTb[IT83XX_ECPM_PLLFREQR & 0xf];

	/* set GPIO */
	if (port == 0) {
		PD1CC1 = OUTPUT | INPUT;
		PD1CC2 = OUTPUT | INPUT;
	} else {
		PD2CC1 = OUTPUT | INPUT;
		PD2CC2 = OUTPUT | INPUT;
	}
}


void pd_irq(int port)
{
	/* check status */
	if (TRUE == USBPD_IS_HARD_RESET_DETECT(port)) {
		if (TRUE == USBPD_IS_HARD_RESET_DETECT(port)) {
			/* clear interrupt */
			USBPD_ISR(port) = USBPD_HARD_RESET_DETECT;
		}
		task_set_event(PD_PORT_TO_TASK_ID(port)
				, PD_EVENT_TCPC_RESET, 0);
	}
	if (TRUE == USBPD_IS_RX_DONE(port)) {
		tcpc_run(port, PD_EVENT_RX);
		/* clear interrupt */
		USBPD_ISR(port) = USBPD_MSG_RX_DONE;
/* task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_RX, 0); */
	}
}

/* tcpc-defined, phy layer serve tcpc for register message */
void tcpc_alert_clear(int port)
{

}

void pd_rx_disable_monitoring(int port)
{
/* USBPD_DISABLE_BMC_PHY(port); */
}

void pd_rx_enable_monitoring(int port)
{
	USBPD_ENABLE_BMC_PHY(port);
}

void pd_hw_init(int port, int role)
{
	usbpd_init(port, role);
}
/*
void pd_execute_hard_reset(int port)
{
	USBPD_HW_RESET(port);
}
*/

int pd_set_clock(int port, int freq)
{
	/* default 48M, 0:48M 1:24M 2:16M 3:12M 4:48/5(M) 5:8M */
	if (freq >= 0 && freq <= 5) {
		if ((IT83XX_ECPM_PLLFREQR & 0xf) <= PLL_96M)
			IT83XX_ECPM_SCDCR4 = (IT83XX_ECPM_SCDCR4 & 0xF0) | freq;
		return (48/(freq+1));
	} else {
		return -1;
	}
}

void pd_select_polarity(int port, int polarity)
{
	/* 0:CC1 is cc, 1:cc2 is cc */
	if (polarity == 0)
		SET_MASK(CCGCR(port), BIT0);
	else
		CLEAR_MASK(CCGCR(port), BIT0);
}

void pd_hw_release(int port)
{

}

void pd_set_host_mode(int port, int pull)
{
#if 0
	if (pull == TYPEC_CC_RD)
		SET_MASK(CCGCR(port), BIT1);
	else
		CLEAR_MASK(CCGCR(port), BIT1);
#endif
	if (pull == TYPEC_CC_RD) {
		/* 0: PD_ROLE_UFP 1: PD_ROLE_DFP */
		usbpd_set_operation_role(port, PD_ROLE_UFP);
		/* PD_ROLE_SINK   0, PD_ROLE_SOURCE 1 */
		usbpd_set_power_role(port, PD_ROLE_SINK);
	} else {
		usbpd_set_operation_role(port, PD_ROLE_DFP);
		usbpd_set_power_role(port, PD_ROLE_SOURCE);
	}
}

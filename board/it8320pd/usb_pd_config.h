/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __USB_PD_CONFIG_H
#define __USB_PD_CONFIG_H

#include "power.h"
#include "registers.h"
#include "usb_pd.h"

/* define */
#define ALT      0x00
#define INPUT    0x80
#define OUTPUT   0x40
#define PULL_UP  0x04
#define PULL_DW  0x02
#define MaxD     0x38

#define NullPin  INPUT

/*     */
#define PLL_8M			0
#define PLL_16M			1
#define PLL_24M			2
#define PLL_32M			3
#define PLL_48M			4
#define PLL_64M			5
#define PLL_72M			6
#define PLL_96M			7

#define BIT(X) (1 << (X))

#define BIT0  0x00000001
#define BIT1  0x00000002
#define BIT2  0x00000004
#define BIT3  0x00000008
#define BIT4  0x00000010
#define BIT5  0x00000020
#define BIT6  0x00000040
#define BIT7  0x00000080

#define BIT8  0x00000100
#define BIT9  0x00000200
#define BIT10 0x00000400
#define BIT11 0x00000800
#define BIT12 0x00001000
#define BIT13 0x00002000
#define BIT14 0x00004000
#define BIT15 0x00008000

#define BIT16 0x00010000
#define BIT17 0x00020000
#define BIT18 0x00040000
#define BIT19 0x00080000
#define BIT20 0x00100000
#define BIT21 0x00200000
#define BIT22 0x00400000
#define BIT23 0x00800000

#define BIT24 0x01000000
#define BIT25 0x02000000
#define BIT26 0x04000000
#define BIT27 0x08000000
#define BIT28 0x10000000
#define BIT29 0x20000000
#define BIT30 0x40000000
#define BIT31 0x80000000

#ifndef FALSE
#define FALSE   0
#endif

#ifndef TRUE
#define TRUE    1
#endif

#ifndef NULL
#define NULL (0)
#endif

#define SET_MASK(reg, bit_mask)      ((reg) |= (bit_mask))
#define CLEAR_MASK(reg, bit_mask)    ((reg) &= (~(bit_mask)))
#define IS_MASK_SET(reg, bit_mask)   (((reg) & (bit_mask)) != 0)
#define IS_MASK_CLEAR(reg, bit_mask) (((reg) & (bit_mask)) == 0)
#define INVERSE_REG(reg, bit)           ((reg) ^= (0x1 << (bit)))
#define INVERSE_REG_MASK(reg, bit_mask) ((reg) ^= (bit_mask))
#define Word_Mask(x)        (1 << (x))
#define Cpl_Byte_Mask(x)    (~(1 << (x)))
#define Cpl_Word_Mask(x)    (~(1 << (x)))

/* config */

#define __ENABLE_USBPD_LED__ 1

/* GPIO */
#define PD1CC1               IT83XX_GPIO_GPCRF4
#define PD1CC2               IT83XX_GPIO_GPCRF5
#define PD2CC1               IT83XX_GPIO_GPCRH1
#define PD2CC2               IT83XX_GPIO_GPCRH2

/* Count */
#define USBPD_HW_RESET_CNT   2

/* Buffer size */
#define USBPD_BUF_SIZE       256
#define USBPD_LOG_CNT        25
#define USBPD_LOG_LENGTH     20

/*Set*/
#define USBPD_ENTER_VDM_MODE(port)           \
	SET_MASK(PDCSR(port), ACTIVE_MODE_ENABLE)
#define USBPD_EXIT_VDM_MODE(port)            \
	CLEAR_MASK(PDCSR(port), ACTIVE_MODE_ENABLE)
#define USBPD_ENABLE_AUTO_SW_RESET(port)     \
	SET_MASK(USBPD_GCR(port), USBPD_AUTO_SEND_SW_RESET)
#define USBPD_DISABLE_AUTO_SW_RESET(port)    \
	CLEAR_MASK(USBPD_GCR(port), USBPD_AUTO_SEND_SW_RESET)
#define USBPD_ENABLE_BMC_PHY(port)           \
	SET_MASK(USBPD_GCR(port), USBPD_BMC_PHY)
#define USBPD_DISABLE_BMC_PHY(port)          \
	CLEAR_MASK(USBPD_GCR(port), USBPD_BMC_PHY)
#define USBPD_ENABLE_SEND_BIST_MODE_2(port)  \
	SET_MASK(MTSR0(port), USBPD_SEND_BIST_MODE_2)
#define USBPD_DISABLE_SEND_BIST_MODE_2(port) \
	CLEAR_MASK(MTSR0(port), USBPD_SEND_BIST_MODE_2)

#define USBPD_KICK_TX_START(port)            \
	SET_MASK(MTCR(port), USBPD_TX_START)
#define USBPD_HW_RESET(port)                 \
	SET_MASK(MTSR0(port), USBPD_SEND_HW_RESET)
#define USBPD_SW_RESET(port)                 \
	SET_MASK(USBPD_GCR(port), USBPD_SW_RESET_BIT)
#define USBPD_START(port)                    \
	CLEAR_MASK(CCGCR(port), USBPD_DISABLE_CC)
#define USBPD_STOP(port)                     \
	SET_MASK(CCGCR(port), USBPD_DISABLE_CC)
#define USBPD_ENABLE_AUTO_DETECT_PLUG(port)  \
	SET_MASK(CCADCR(port), (AUTO_CC_DISABLE_DURING_DETACH \
				| AUTO_CC_DETACH_DETECT_ENABLE \
				| AUTO_CC_ATTACH_DETECT_ENABLE))
#define USBPD_ENABLE_VCONN(port)             \
	SET_MASK(PDCSR(port), VCONN_ENABLE)
#define USBPD_DISABLE_VCONN(port)            \
	CLEAR_MASK(PDCSR(port), VCONN_ENABLE)
#define USBPD_ENABLE_STAT_VBUS_5V(port)      \
	SET_MASK(PDCSR(port), VBUS_STAT_5V_ENABLE)
#define USBPD_DISABLE_STAT_VBUS_5V(port)     \
	CLEAR_MASK(PDCSR(port), VBUS_STAT_5V_ENABLE)
#define USBPD_ENABLE_SOP_CABLE(port)         \
	SET_MASK(PDMSR(port), SOP_ENABLE | SOPP_ENABLE | SOPPP_ENABLE)
#define USBPD_DISABLE_SOP_CABLE(port)        \
	CLEAR_MASK(PDMSR(port), SOPP_ENABLE | SOPPP_ENABLE)

/*Get*/
#define USBPD_GET_POWER_ROLE(port)   (PDMSR(port) & 0x1)
#define USBPD_DATA_ROLE(port)        ((PDMSR(port) >> 2) & 0x1)
#define USBPD_TX_MSG_ID(port)        (PES0R(port) & 0x7)
#define USBPD_VER_SELF(port)         ((PDMHSR(port) >> 1) & 0x3)
#define USBPD_GET_AUTO_CC(port)      ((CCADRR(port) >> 6) & 0x1)
#define USBPD_GET_AOTO_ROLE_TYPE(port) (CCADRR(port) & 0xf)

#define USBPD_GET_TX_SOP_TYPE(port)    ((MTSR1(port) >> 4) & 0x03)
#define USBPD_GET_RX_SOP_TYPE(port)    ((MRSR(port) >> 4) & 0x03)
#define USBPD_GET_PULL_REGISTER_SELECTION(port) (CCGCR(port) >> 1 & 0x01)
#define USBPD_GET_PULL_CC_SELECTION(port)       (CCGCR(port)  & 0x01)


/*check*/
#define USBPD_IS_ENTER_VDM(port) \
	(IS_MASK_SET(PDCSR(port), ACTIVE_MODE_ENABLE) > 0 ? TRUE:FALSE)
#define USBPD_IS_VCONN_DETECT_NEED_ENABLE(port) \
	(IS_MASK_SET(CCADRR(port), USBPD_VCONN_DETECT_NEED_ENABLE) \
	> 0 ? TRUE:FALSE)
#define USBPD_IS_VCONN_ENABLE(port) \
	(IS_MASK_SET(PDCSR(port), VCONN_ENABLE) > 0 ? TRUE:FALSE)
#define USBPD_IS_PLUG_CBL(port) \
	(IS_MASK_SET(PDMSR(port), (SOPP_ENABLE | SOPPP_ENABLE)) \
	> 0 ? TRUE:FALSE)
#define USBPD_IS_RX_COMING(port) \
	(IS_MASK_SET(USBPD_ISR(port), USBPD_MSG_RX_DONE) > 0 ? TRUE:FALSE)
#define USBPD_IS_BUS_BUSY(port) \
	(IS_MASK_SET(PEPDRSR(port), USBPD_CC_BUSY) == 0 ? TRUE:FALSE)

#define USBPD_IS_RX_VALID(port) \
	(IS_MASK_SET(MRSR(port), USBPD_RX_MSG_VALID) > 0 ? TRUE:FALSE)
#define USBPD_IS_SW_RESET_MSG_TX_ERR(port) \
(IS_MASK_SET(MTCR(port), USBPD_SW_RESET_TX_STAT) > 0 ? TRUE:FALSE)
	#define USBPD_IS_CC_IDLE(port) \
(IS_MASK_SET(PEPDRSR(port), USBPD_CC_BUSY) > 0 ? TRUE:FALSE)
#define USBPD_IS_TX_ERR(port) \
	(IS_MASK_SET(MTCR(port), USBPD_TX_ERR_STAT) > 0 ? TRUE:FALSE)
#define USBPD_IS_TX_DISCARD(port) \
	(IS_MASK_SET(MTCR(port), USBPD_TX_DISCARD_STAT) > 0 ? TRUE:FALSE)
#define USBPD_IS_SNIFFER_MODE(port) \
	(IS_MASK_SET(USBPD_GCR(port), USBPD_SNIFFER_MODE) > 0 ? TRUE:FALSE)

/*current value*/
#define USBPD_DEF_DFT_CUR	USBPD_DFP_CUR_3_0MA

#define USBPD_DFP_CUR_0_9MA	0xc
#define USBPD_DFP_CUR_1_5MA	0x8
#define USBPD_DFP_CUR_3_0MA	0x4

/*ISR*/
#define USBPD_IS_TYPE_C_DETECT(port) \
	(IS_MASK_SET(USBPD_ISR(port), USBPD_TYPE_C_DETECT) > 0 ? TRUE:FALSE)
#define USBPD_IS_CABLE_RESET_DETECT(port) \
	(IS_MASK_SET(USBPD_ISR(port), USBPD_CABLE_RESET_DETECT) \
	> 0 ? TRUE:FALSE)
#define USBPD_IS_HARD_RESET_DETECT(port) \
	(IS_MASK_SET(USBPD_ISR(port), USBPD_HARD_RESET_DETECT) \
	> 0 ? TRUE:FALSE)
#define USBPD_IS_AUTO_SOFT_RESET_TX_DONE(port) \
	(IS_MASK_SET(USBPD_ISR(port), USBPD_AUTO_SOFT_RESET_TX_DONE) \
	> 0 ? TRUE:FALSE)
#define USBPD_IS_HARD_RESET_TX_DONE(port) \
	(IS_MASK_SET(USBPD_ISR(port), USBPD_HARD_RESET_TX_DONE) \
	> 0 ? TRUE:FALSE)
#define USBPD_IS_TX_DONE(port) \
	(IS_MASK_SET(USBPD_ISR(port), USBPD_MSG_TX_DONE) > 0 ? TRUE:FALSE)
#define USBPD_IS_RX_DONE(port) \
	(IS_MASK_SET(USBPD_ISR(port), USBPD_MSG_RX_DONE) > 0 ? TRUE:FALSE)
#define USBPD_IS_TIMEOUT(port) \
	(IS_MASK_SET(USBPD_ISR(port), USBPD_TIMER_TIMEOUT) > 0 ? TRUE:FALSE)


/*LED*/
#if __ENABLE_USBPD_LED__
#define USBPD_LED_PORT_A_PIN	IT83XX_GPIO_GPCRA0
#define USBPD_LED_PORT_A	IT83XX_GPIO_GPDRA
#define USBPD_LED_PORT_A_PIN_OPS	BIT0

#define USBPD_LED_PORT_A_ON() \
	CLEAR_MASK(USBPD_LED_PORT_A, USBPD_LED_PORT_A_PIN_OPS)
#define USBPD_LED_PORT_A_OFF() \
	SET_MASK(USBPD_LED_PORT_A, USBPD_LED_PORT_A_PIN_OPS)
#endif

/*tuning*/
#define USBPD_TX_SWING					4
#define USBPD_TX_DRIVING					0
#define USBPD_TX_FC_FILTER				3
#define USBPD_TX_PRE_DRIVING				0

/******************************************************************************/
/*TCPC serve TCPCI*/
/*****************************************************************************/

/*cc status*/
#define Looking_4_Connection BIT(5)
#define Connection_Result BIT(4)
#define CC2_State (BIT(3) || BIT(2))
#define CC1_State (BIT(1) || BIT(0))

#define CC_State_SRC_OPEN 0X00
#define CC_State_SRC_RA BIT0
#define CC_State_SRC_RD BIT1
#define CC_State_SNK_OPEN 0x00
#define CC_State_SNK_DEF BIT0
#define CC_State_SNK_1_5 BIT1
#define CC_State_SNK_3_0 0x03

/*****************************************************************************/
/* declaration of struct and enum*/
/*****************************************************************************/
enum usbpd_port {
	USBPD_PORT_A,
	USBPD_PORT_B,
	USBPD_PORT_CNT,
};

enum usbpd_result {
	USBPD_RESULT_SUCC,
	USBPD_RESULT_FAIL,
	USBPD_RESULT_TX_DISCARD,
	USBPD_RESULT_SKIP,
	USBPD_RESULT_NODATA,
	USBPD_RESULT_BUSY,
	USBPD_RESULT_DATA_ERROR,
	USBPD_RESULT_RESET,
	USBPD_RESULT_DISCONNECT,
};

enum usbpd_power_role {
	USBPD_POWER_ROLE_CONSUMER,
	USBPD_POWER_ROLE_PROVIDER,
	USBPD_POWER_ROLE_CONSUMER_PROVIDER,
	USBPD_POWER_ROLE_PROVIDER_CONSUMER,
};

enum usbpd_operation_role {
	USBPD_OPERATION_ROLE_UFP,
	USBPD_OPERATION_ROLE_DFP,
	USBPD_OPERATION_ROLE_DRP_UFP,
	USBPD_OPERATION_ROLE_DRP_DFP,
};

enum usbpd_sop_type {
	USBPD_SOP_TYPE_SOP,
	USBPD_SOP_TYPE_SOP_P,
	USBPD_SOP_TYPE_SOP_PP,
	USBPD_SOP_TYPE_CNT,
};

enum usbpd_reset_type {
	USBPD_RESET_TYPE_HARD,
	USBPD_RESET_TYPE_CABLE,
	USBPD_RESET_TYPE_CNT,
};

enum usbpd_cc_pin {
	USBPD_CC_PIN_1,
	USBPD_CC_PIN_2,
	USBPD_CC_PIN_CNT,
};

struct usbpd_header {
	uint8_t u8MsgType	: 4;
	uint8_t u8Reserved	: 1;
	uint8_t u8PortRole	: 1;
	uint8_t u8SpecVer	: 2;
	uint8_t u8PowerRole	: 1;
	uint8_t u8MsgId		: 3;
	uint8_t u8DataObjNum	: 3;
	uint8_t u8Reserved2	: 1;
};

/* Public function */

int usbpd_detect_vbus(int port);
int usbpd_get_cc(int port, int cc_pin);
int usbpd_is_link(int port);

/* transfer */
enum usbpd_result usbpd_rx_data(enum usbpd_port port
				, enum usbpd_sop_type *p_enumSopType
				, void *p_header
				, uint32_t *p_u8Buf);
enum usbpd_result usbpd_tx_data(enum usbpd_port port
				, enum usbpd_sop_type enumSopType
				, uint8_t u8MsgType
				, uint8_t u8DataLength
				, const uint32_t *p_u8Buf);

void usbpd_sw_reset(enum usbpd_port port);
void usbpd_hw_reset(enum usbpd_port port, enum usbpd_reset_type reset_type);

void usbpd_enable_vconn(enum usbpd_port port, uint8_t bIsEnable);
void usbpd_init(int port, int role);
void usbpd_bist_mode_2_tx(int port);

/* usb_pd_config.h definition */
void pd_select_polarity(int port, int cc_pin);
void pd_set_host_mode(int port, int pull);

void usbpd_set_power_role(int port, int powerRole);
void usbpd_set_operation_role(int port, int dataRole);
void usbpd_reset(int port, int role);
void usbpd_hw_reset_reg(int port, int role);
int DFPVDR_check(int value);
int UFPVDR_check(int value);


#endif

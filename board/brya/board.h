/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Brya board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Baseboard features */
#include "baseboard.h"

#define CONFIG_BRINGUP
#define CONFIG_SYSTEM_UNLOCKED

#define CONFIG_PWRGD_PASS_THROUGH_CUSTOM

#define CONFIG_USB_PD_TCPM_PS8815
#define CONFIG_USBC_RETIMER_INTEL_BB

/* USB Type C and USB PD defines */
#define CONFIG_USB_PD_PORT_MAX_COUNT		3

#define CONFIG_IO_EXPANDER
#define CONFIG_IO_EXPANDER_NCT38XX
#define CONFIG_IO_EXPANDER_PORT_COUNT		2

#define CONFIG_USBC_PPC_SYV682X
#define CONFIG_USBC_PPC_NX20P3483

/* TODO: b/177608416 - measure and check these values on brya */
#define PD_POWER_SUPPLY_TURN_ON_DELAY	30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY	30000 /* us */
#define PD_VCONN_SWAP_DELAY		5000 /* us */

/*
 * Passive USB-C cables only support up to 60W.
 */
#define PD_OPERATING_POWER_MW	15000
#define PD_MAX_POWER_MW		60000
#define PD_MAX_CURRENT_MA	3000
#define PD_MAX_VOLTAGE_MV	20000

/* I2C Bus Configuration */

#define I2C_PORT_SENSOR		NPCX_I2C_PORT0_0

#define I2C_PORT_TCPC0_2	NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C0_TCPC	NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C1_TCPC	NPCX_I2C_PORT4_1
#define I2C_PORT_USB_C2_TCPC	NPCX_I2C_PORT1_0	/* dual TCPC with C0 */

#define I2C_PORT_USB_C0_PPC	NPCX_I2C_PORT2_0
#define I2C_PORT_USB_C1_PPC	NPCX_I2C_PORT6_0
#define I2C_PORT_USB_C2_PPC	NPCX_I2C_PORT2_0

#define I2C_PORT_USB_C0_BC12	NPCX_I2C_PORT2_0
#define I2C_PORT_USB_C1_BC12	NPCX_I2C_PORT6_0
#define I2C_PORT_USB_C2_BC12	NPCX_I2C_PORT2_0

#define I2C_PORT_USB_C0_MUX	NPCX_I2C_PORT3_0
#define I2C_PORT_USB_C1_MUX	NPCX_I2C_PORT6_0
#define I2C_PORT_USB_C2_MUX	NPCX_I2C_PORT3_0

#define I2C_PORT_BATTERY	NPCX_I2C_PORT5_0
#define I2C_PORT_CHARGER	NPCX_I2C_PORT7_0
#define I2C_PORT_EEPROM		NPCX_I2C_PORT7_0

#define I2C_ADDR_EEPROM_FLAGS	0x50
/*
 * see b/174768555#comment22
 */
#define USBC_PORT_C0_BB_RETIMER_I2C_ADDR	0x56
#define USBC_PORT_C2_BB_RETIMER_I2C_ADDR	0x57

/* Battery fuel gauge */
#define CONFIG_BATTERY_BQ4050

/* charger defines */
/* brya uses the BQ25720 */
#define CONFIG_CHARGER_BQ25710
#define CONFIG_CHARGE_RAMP_HW
#define CONFIG_CHARGER_NARROW_VDC
#define CONFIG_CHARGER_SENSE_RESISTOR		10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC	10

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GMR_TABLET_MODE_GPIO_L		GPIO_TABLET_MODE_ODL
#define GPIO_AC_PRESENT			GPIO_ACOK_EC_OD
#define GPIO_CPU_PROCHOT		GPIO_EC_PROCHOT_ODL
#define GPIO_EC_INT_L			GPIO_EC_PCH_INT_ODL
#define GPIO_ENABLE_BACKLIGHT		GPIO_EC_EN_EDP_BL
#define GPIO_EN_PP5000			GPIO_EN_S5_RAILS
#define GPIO_ENTERING_RW		GPIO_EC_ENTERING_RW
#define GPIO_KBD_KSO2			GPIO_EC_KSO_02_INV
#define GPIO_LID_OPEN			GPIO_LID_OPEN_OD
#define GPIO_PCH_PWRBTN_L		GPIO_EC_PCH_PWR_BTN_ODL
#define GPIO_PCH_RSMRST_L		GPIO_EC_PCH_RSMRST_L
#define GPIO_PCH_RTCRST			GPIO_EC_PCH_RTCRST
#define GPIO_PCH_SLP_S0_L		GPIO_SYS_SLP_S0IX_L
#define GPIO_PCH_SLP_S3_L		GPIO_SLP_S3_L
#define GPIO_PCH_WAKE_L			GPIO_EC_PCH_INT_ODL
#define GPIO_PG_EC_ALL_SYS_PWRGD	GPIO_SEQ_EC_ALL_SYS_PG
#define GPIO_PG_EC_DSW_PWROK		GPIO_SEQ_EC_DSW_PWROK
#define GPIO_PG_EC_RSMRST_ODL		GPIO_SEQ_EC_RSMRST_ODL
#define GPIO_POWER_BUTTON_L		GPIO_GSC_EC_PWR_BTN_ODL
#define GPIO_RSMRST_L_PGOOD		GPIO_SEQ_EC_RSMRST_ODL
#define GPIO_SYS_RESET_L		GPIO_SYS_RST_ODL
#define GPIO_VOLUME_DOWN_L		GPIO_EC_VOLDN_BTN_ODL
#define GPIO_VOLUME_UP_L		GPIO_EC_VOLUP_BTN_ODL
#define GPIO_WP_L			GPIO_EC_WP_ODL

#ifndef __ASSEMBLER__

#include "gpio_signal.h"	/* must precede gpio.h */
#include "registers.h"

enum usbc_port {
	USBC_PORT_C0 = 0,
	USBC_PORT_C1,
	USBC_PORT_C2,
	USBC_PORT_COUNT
};

enum ioex_port {
	IOEX_C0_NCT38XX = 0,
	IOEX_C2_NCT38XX,
	IOEX_PORT_COUNT
};

enum battery_type {
	BATTERY_POWER_TECH,
	BATTERY_LGC011,
	BATTERY_TYPE_COUNT
};

enum pwm_channel {
	PWM_CH_LED2 = 0,		/* PWM0 (white charger) */
	PWM_CH_LED3,			/* PWM1 */
	PWM_CH_LED1,			/* PWM2 (orange charger) */
	PWM_CH_KBLIGHT,			/* PWM3 */
	PWM_CH_FAN,			/* PWM5 */
	PWM_CH_LED4,			/* PWM7 */
	PWM_CH_COUNT
};

enum mft_channel {
	MFT_CH_0 = 0,
	MFT_CH_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */

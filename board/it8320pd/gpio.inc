/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
GPIO_INT(AC_PRESENT,         PIN(F, 0), GPIO_INT_BOTH,                  extpower_interrupt)      /* EC_PCH_ACPRESENT */
GPIO_INT(PD_MCU_INT,         PIN(F, 1), GPIO_INT_BOTH | GPIO_PULL_UP,   pd_mcu_interrupt)        /* USB_MCU_EC_INT */
GPIO_INT(USB_C0_VBUS_WAKE_L, PIN(F, 2), GPIO_INT_BOTH,                  vbus0_evt)               /* USB_C0_VBUS_DET_L */
GPIO_INT(USB_C1_VBUS_WAKE_L, PIN(F, 3), GPIO_INT_BOTH,                  vbus1_evt)               /* USB_C1_VBUS_DET_L */
GPIO_INT(USB_C0_BC12_INT_L,  PIN(F, 4), GPIO_INT_FALLING,               usb0_evt)                /* USB_C0_BC12_INT_L */
GPIO_INT(USB_C1_BC12_INT_L,  PIN(F, 5), GPIO_INT_FALLING,               usb1_evt)                /* USB_C1_BC12_INT_L */
GPIO_INT(POWER_BUTTON_L,     PIN(E, 4), GPIO_INT_BOTH | GPIO_PULL_UP,   power_button_interrupt)
GPIO_INT(PCH_PLTRST_L,       PIN(E, 3), GPIO_INT_BOTH | GPIO_PULL_UP,   lpcrst_interrupt)
GPIO_INT(LID_OPEN,           PIN(E, 2), GPIO_INT_BOTH | GPIO_PULL_DOWN, lid_interrupt)
GPIO_INT(WP_L,               PIN(E, 1), GPIO_INT_BOTH,                  switch_interrupt)         /* Write protect input */
#ifdef CONFIG_LOW_POWER_IDLE
GPIO_INT(UART1_RX,           PIN(B, 0), GPIO_INT_FALLING,               uart_deepsleep_interrupt) /* UART1 RX input */
#endif

GPIO(USB_C0_5V_EN,           PIN(A, 0), GPIO_OUT_LOW)  /* EN_USB_C0_5V_OUT */
GPIO(USB_C1_5V_EN,           PIN(A, 1), GPIO_OUT_LOW)  /* EN_USB_C1_5V_OUT */
GPIO(USB_C0_CHARGE_EN_L,     PIN(A, 2), GPIO_OUT_LOW)  /* EN_USB_C0_CHARGE_EC_L */
GPIO(USB_C1_CHARGE_EN_L,     PIN(A, 3), GPIO_OUT_LOW)  /* EN_USB_C1_CHARGE_EC_L */
GPIO(PMIC_SLP_SUS_L,         PIN(J, 2), GPIO_OUT_LOW)  /* SLP_SUS_L_PMIC */
GPIO(PCH_ACOK,               PIN(J, 3), GPIO_OUT_LOW)  /* ROP_EC_ACOK */
GPIO(USB_C0_DP_HPD,          PIN(J, 4), GPIO_OUT_LOW)  /* USB_C0_DP_HPD */
GPIO(USB_C1_DP_HPD,          PIN(J, 5), GPIO_OUT_LOW)  /* USB_C1_DP_HPD */
GPIO(PD_RST_L,               PIN(E, 5), GPIO_ODR_HIGH)
GPIO(BAT_PRESENT_L,          PIN(I, 2), GPIO_INPUT)    /* EC_BATT_TMP */		//msmart

GPIO(PCH_SMI_L,              PIN(D, 3), GPIO_OUT_HIGH)
GPIO(PCH_SCI_L,              PIN(D, 4), GPIO_OUT_HIGH)
GPIO(GATE_A20_H,             PIN(B, 5), GPIO_OUT_HIGH)
GPIO(PCH_RCIN_L,             PIN(B, 6), GPIO_OUT_HIGH)
GPIO(LPC_CLKRUN_L,           PIN(H, 0), GPIO_OUT_LOW)
GPIO(PCH_WAKE_L,             PIN(B, 7), GPIO_ODR_HIGH)  /* Wake signal from EC to PCH */

GPIO(I2C_A_SCL,      PIN(B, 3), GPIO_INPUT)
GPIO(I2C_A_SDA,      PIN(B, 4), GPIO_INPUT)
GPIO(I2C_B_SCL,      PIN(C, 1), GPIO_INPUT)
GPIO(I2C_B_SDA,      PIN(C, 2), GPIO_INPUT)
#ifdef CONFIG_IT83XX_SMCLK2_ON_GPC7
GPIO(I2C_C_SCL,      PIN(C, 7), GPIO_INPUT)
#else
GPIO(I2C_C_SCL,      PIN(F, 6), GPIO_INPUT)
#endif
GPIO(I2C_C_SDA,      PIN(F, 7), GPIO_INPUT)

#ifdef CONFIG_UART_HOST
GPIO(UART2_SIN1,     PIN(H, 1), GPIO_INPUT)
GPIO(UART2_SOUT1,    PIN(H, 2), GPIO_INPUT)
#endif

/* KSO/KSI pins can be used as GPIO input. */
GPIO(BOARD_VERSION1, PIN(KSO_H, 5), GPIO_INPUT)
GPIO(BOARD_VERSION2, PIN(KSO_H, 6), GPIO_INPUT)
GPIO(BOARD_VERSION3, PIN(KSO_H, 7), GPIO_INPUT)

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)

ALTERNATE(PIN_MASK(B, 0x03), 1, MODULE_UART, GPIO_PULL_UP) /* UART1 */
#ifdef CONFIG_UART_HOST
ALTERNATE(PIN_MASK(H, 0x06), 1, MODULE_UART, 0)            /* UART2 */
#endif
ALTERNATE(PIN_MASK(A, 0x80), 1, MODULE_PWM, 0)             /* PWM7 for FAN1 */
ALTERNATE(PIN_MASK(D, 0x40), 3, MODULE_PWM, 0)             /* TACH0A for FAN1 */
ALTERNATE(PIN_MASK(B, 0x18), 1, MODULE_I2C,  GPIO_PULL_UP)             /* I2C A SCL/SDA */		//msmart
#ifdef CONFIG_IT83XX_SMCLK2_ON_GPC7
ALTERNATE(PIN_MASK(C, 0x86), 1, MODULE_I2C,  GPIO_PULL_UP)             /* I2C B SCL/SDA, C SCL */	//msmart
ALTERNATE(PIN_MASK(F, 0x80), 1, MODULE_I2C,  GPIO_PULL_UP)             /* I2C C SDA */		//msmart
#else
ALTERNATE(PIN_MASK(C, 0x06), 1, MODULE_I2C,  GPIO_PULL_UP)             /* I2C B SCL/SDA */		//msmart
ALTERNATE(PIN_MASK(F, 0xC0), 1, MODULE_I2C,  GPIO_PULL_UP)             /* I2C C SCL/SDA */		//msmart
#endif

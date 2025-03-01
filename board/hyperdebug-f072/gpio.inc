/* -*- mode:c -*-
 *
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * List of all GPIO pins available for Host computer to manipulate.
 * The are named based on their location on the two 70-pin DIL
 * connectors on either side of the HyperDebug board.  Pins with
 * special functions are commented out, and declared with relevant
 * symbolic name further below.
 */

/*
 * The common define GPIO_INT_BOTH includes GPIO_INPUT, which interferes with
 * GPIO_ALTERNATE.  Instead we want to declare the intent to detect either
 * rising or falling edges, making it conditional on the pins possibly later
 * being re-configured as GPIO inputs.
 */
#define ANY_EDGE (GPIO_INT_F_RISING | GPIO_INT_F_FALLING)

/* P1_1: 3V */
/* P1_2: GND */
/* P1_3: VBAT */
GPIO(P1_4,	PIN(C, 13),  GPIO_INPUT)
GPIO(P1_5,	PIN(C, 14),  GPIO_INPUT)
GPIO(P1_6,	PIN(C, 15),  GPIO_INPUT)
GPIO(P1_7,	PIN(F, 0),  GPIO_INPUT)	
GPIO(P1_8,	PIN(F, 1),  GPIO_INPUT)	
/* P1_9: GND */
/* P1_10: NRST */
GPIO(P1_11,	PIN(C, 0),  GPIO_INPUT)	
GPIO(P1_12,	PIN(C, 1),  GPIO_INPUT)	
GPIO(P1_13,	PIN(C, 2),  GPIO_INPUT)	
GPIO(P1_14,	PIN(C, 3),  GPIO_INPUT)	
/*GPIO(P1_15,	PIN(A, 0),  GPIO_INPUT)*/
GPIO(P1_16,	PIN(A, 1),  GPIO_INPUT)	
GPIO(P1_17,	PIN(A, 2),  GPIO_ALTERNATE)	
GPIO(P1_18,	PIN(A, 3),  GPIO_ALTERNATE)	
/* P1_19: GND */
/* P1_20: VDD */
GPIO(P1_21,	PIN(A, 4),  GPIO_INPUT)	
GPIO(P1_22,	PIN(A, 5),  GPIO_INPUT)	
GPIO(P1_23,	PIN(A, 6),  GPIO_INPUT)	
GPIO(P1_24,	PIN(A, 7),  GPIO_INPUT)	
GPIO(P1_25,	PIN(C, 4),  GPIO_INPUT)	
GPIO(P1_26,	PIN(C, 5),  GPIO_INPUT)	
GPIO(P1_27,	PIN(B, 0),  GPIO_INPUT)	
GPIO(P1_28,	PIN(B, 1),  GPIO_INPUT)	
GPIO(P1_29,	PIN(B, 2),  GPIO_INPUT)	
GPIO(P1_30,	PIN(B, 10),  GPIO_INPUT)
GPIO(P1_31,	PIN(B, 11),  GPIO_INPUT)
GPIO(P1_32,	PIN(B, 12),  GPIO_INPUT)
/* P1_33: GND */

/* P2_1: 5V */
/* P2_2: GND */
GPIO(P2_3,	PIN(B, 9),  GPIO_INPUT)
GPIO(P2_4,	PIN(B, 8),  GPIO_INPUT)
/* P2_5: VDD */
/* P2_6: BOOT0 */
GPIO(P2_7,	PIN(B, 7),  GPIO_INPUT)
GPIO(P2_8,	PIN(B, 6),  GPIO_INPUT)
GPIO(P2_9,	PIN(B, 5),  GPIO_INPUT)
GPIO(P2_10,	PIN(B, 4),  GPIO_INPUT)
GPIO(P2_11,	PIN(B, 3),  GPIO_INPUT)
GPIO(P2_12,	PIN(D, 2),  GPIO_INPUT)
GPIO(P2_13,	PIN(C, 12),  GPIO_INPUT)
GPIO(P2_14,	PIN(C, 11),  GPIO_INPUT)
GPIO(P2_15,	PIN(C, 10),  GPIO_INPUT)
GPIO(P2_16,	PIN(A, 15),  GPIO_OUT_LOW)
GPIO(P2_17,	PIN(A, 14),  GPIO_INPUT)  /* ST-link SWCLK */
/* P2_18: VDDIO2 */
/* P2_19: VDDA */
GPIO(P2_20,	PIN(A, 13),  GPIO_INPUT)  /* ST-link SWDIO */
GPIO(P2_21,	PIN(A, 12),  GPIO_ALTERNATE)  /* USB D+ */
GPIO(P2_22,	PIN(A, 11),  GPIO_ALTERNATE)  /* USB D- */
GPIO(P2_23,	PIN(A, 10),  GPIO_INPUT)
GPIO(P2_24,	PIN(A, 9),  GPIO_INPUT)
GPIO(P2_25,	PIN(A, 8),  GPIO_INPUT)
/*GPIO(P2_26,	PIN(C, 9),  GPIO_INPUT)*/
/*GPIO(P2_27,	PIN(C, 8),  GPIO_INPUT)*/
/*GPIO(P2_28,	PIN(C, 7),  GPIO_INPUT)*/
/*GPIO(P2_29,	PIN(C, 6),  GPIO_INPUT)*/
GPIO(P2_30,	PIN(B, 15),  GPIO_INPUT)
GPIO(P2_31,	PIN(B, 14),  GPIO_INPUT)
GPIO(P2_32,	PIN(B, 13),  GPIO_INPUT)
/* P2_33: GND */

/* These pins are used for USART and are set to alternate mode below */
/*GPIO_INT(HYPER_CONSOLE_TX,	PIN(G, 7),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge)*/
/*GPIO_INT(HYPER_CONSOLE_RX,	PIN(G, 8),  GPIO_ALTERNATE | ANY_EDGE, gpio_edge)*/

/* Signals for hardware on the Nucleo board itself */
GPIO(NUCLEO_LED_RED,		PIN(C, 6),  GPIO_OUT_LOW)
GPIO(NUCLEO_LED_BLUE,		PIN(C, 7),  GPIO_OUT_LOW)
GPIO(NUCLEO_LED_ORANGE,		PIN(C, 8),  GPIO_OUT_LOW)
GPIO(NUCLEO_LED_GREEN,		PIN(C, 9),  GPIO_OUT_HIGH)
GPIO_INT(NUCLEO_USER_BTN,	PIN(A, 0), GPIO_INPUT | ANY_EDGE, user_button_edge)


/* Unimplemented signals since we are not an EC */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)


ALTERNATE(PIN_MASK(A, 0x000C), 1, MODULE_UART, 0) /* USART2: PA2/PA3 - HyperDebug console */

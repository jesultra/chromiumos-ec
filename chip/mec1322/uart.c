/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "lpc.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"

#ifdef CONFIG_UART_HOST
#define IRQ_UART_HOST CONCAT2(LM4_IRQ_UART, CONFIG_UART_HOST)
#endif

static int init_done;

int uart_init_done(void)
{
	return init_done;
}

void uart_tx_start(void)
{
	/* If interrupt is already enabled, nothing to do */
	if (MEC1322_UART_IER & (1 << 1))
		return;

	/* Do not allow deep sleep while transmit in progress */
	disable_sleep(SLEEP_MASK_UART);

	/*
	 * Re-enable the transmit interrupt, then forcibly trigger the
	 * interrupt.  This works around a hardware problem with the
	 * UART where the FIFO only triggers the interrupt when its
	 * threshold is _crossed_, not just met.
	 */
	MEC1322_UART_IER |= (1 << 1);
	/*task_trigger_irq(LM4_IRQ_UART0);*/
}

void uart_tx_stop(void)
{
	MEC1322_UART_IER &= ~(1 << 1);

	/* Re-allow deep sleep */
	enable_sleep(SLEEP_MASK_UART);
}

extern void set_led(int, int);

void uart_tx_flush(void)
{
	set_led(1, 0);
	/* Wait for transmit FIFO empty */
	while (!(MEC1322_UART_LSR & (1 << 5)))
		;
	set_led(1, 1);
}

int uart_tx_ready(void)
{
	/* TODO: This is FIFO empty bit instead of FIFO full bit? */
	return MEC1322_UART_LSR & (1 << 5);
}

int uart_rx_available(void)
{
	return MEC1322_UART_LSR & (1 << 0);
}

void uart_write_char(char c)
{
	/* Wait for space in transmit FIFO. */
	while (!uart_tx_ready())
		;

	MEC1322_UART_TB = c;
	udelay(1000);
}

int uart_read_char(void)
{
	return MEC1322_UART_RB;
}

static void uart_clear_rx_fifo(int channel)
{
	MEC1322_UART_FCR = (1 << 0) | (1 << 1);
}

void uart_disable_interrupt(void)
{
	task_disable_irq(MEC1322_IRQ_UART);
}

void uart_enable_interrupt(void)
{
	task_enable_irq(MEC1322_IRQ_UART);
}

extern void set_led(int led, int on);

/**
 * Interrupt handler for UART
 */
static void uart_ec_interrupt(void)
{
	/* Read input FIFO until empty, then fill output FIFO */
	uart_process_input();
	uart_process_output();
}
DECLARE_IRQ(MEC1322_IRQ_UART, uart_ec_interrupt, 1);

#ifdef CONFIG_UART_HOST

/**
 * Interrupt handler for Host UART
 */
static void uart_host_interrupt(void)
{
	/* Clear transmit and receive interrupt status */
	LM4_UART_ICR(CONFIG_UART_HOST) = 0x70;

#ifdef CONFIG_LPC
	/*
	 * If we have space in our FIFO and a character is pending in LPC,
	 * handle that character.
	 */
	if (!(LM4_UART_FR(CONFIG_UART_HOST) & 0x20) && lpc_comx_has_char()) {
		/* Copy the next byte then disable transmit interrupt */
		LM4_UART_DR(CONFIG_UART_HOST) = lpc_comx_get_char();
		LM4_UART_IM(CONFIG_UART_HOST) &= ~0x20;
	}

	/*
	 * Handle received character.  There is no flow control on input;
	 * received characters are blindly forwarded to LPC.  This is ok
	 * because LPC is much faster than UART, and we don't have flow control
	 * on the UART receive-side either.
	 */
	if (!(LM4_UART_FR(CONFIG_UART_HOST) & 0x10))
		lpc_comx_put_char(LM4_UART_DR(CONFIG_UART_HOST));
#endif
}
/* Must be same prio as LPC interrupt handler so they don't preempt */
DECLARE_IRQ(IRQ_UART_HOST, uart_host_interrupt, 2);

#endif /* CONFIG_UART_HOST */

void uart_init(void)
{
	/* Set UART to reset on VCC1_RESET instaed of nSIO_RESET */
	MEC1322_UART_CFG &= ~(1 << 1);

	/* Baud rate = 115200. 1.8432MHz clock. Divisor = 1 */

	/* Set CLK_SRC = 0 */
	MEC1322_UART_CFG &= ~(1 << 0);

	/* Set DLAB = 1 */
	MEC1322_UART_LCR |= (1 << 7);

	/* PBRG0/PBRG1 */
	MEC1322_UART_PBRG0 = 1;
	MEC1322_UART_PBRG1 = 0;

	/* Set DLAB = 0 */
	MEC1322_UART_LCR &= ~(1 << 7);

	/* Set word length to 8-bit */
	MEC1322_UART_LCR |= (1 << 0) | (1 << 1);

	/* Enable FIFO */
	MEC1322_UART_FCR = (1 << 0);

	/* UART TxD/RxD Mux */
	/*MEC_GPIO_PIN(0165) = 0x11240;
	MEC_GPIO_PIN(0162) = 0x11240;*/
	REG32(0x40081000 + (0165 << 2)) = 0x11240;
	REG32(0x40081000 + (0162 << 2)) = 0x11240;

	/* Activate UART */
	MEC1322_UART_ACT |= (1 << 0);

	/*
	clock_enable_peripheral(CGC_OFFSET_UART, mask,
			CGC_MODE_RUN | CGC_MODE_SLEEP);

	gpio_config_module(MODULE_UART, 1);*/

	/*
	 * Enable interrupts for UART0 only. Host UART will have to wait
	 * until the LPC bus is initialized.
	 */
	uart_clear_rx_fifo(0);
	MEC1322_UART_IER |= (1 << 0);
	task_enable_irq(MEC1322_IRQ_UART);
	MEC1322_INT_SOURCE(15) |= (1 << 0);
	MEC1322_INT_DISABLE(15) |= (1 << 0);
	MEC1322_INT_ENABLE(15) |= (1 << 0);
	MEC1322_INT_BLK_EN |= (1 << 15);
	MEC1322_UART_MCR |= (1 << 3);

	init_done = 1;
}


/*****************************************************************************/
/* COMx functions */

#ifdef CONFIG_UART_HOST

void uart_comx_enable(void)
{
	uart_clear_rx_fifo(CONFIG_UART_HOST);
	task_enable_irq(IRQ_UART_HOST);
}

int uart_comx_putc_ok(void)
{
	if (LM4_UART_FR(CONFIG_UART_HOST) & 0x20) {
		/*
		 * FIFO is full, so enable transmit interrupt to let us know
		 * when it empties.
		 */
		LM4_UART_IM(CONFIG_UART_HOST) |= 0x20;
		return 0;
	} else {
		return 1;
	}
}

void uart_comx_putc(int c)
{
	LM4_UART_DR(CONFIG_UART_HOST) = c;
}

#endif /* CONFIG_UART_HOST */

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_COMXTEST

/**
 * Write a character to COMx, waiting for space in the output buffer if
 * necessary.
 */
static void uart_comx_putc_wait(int c)
{
		while (!uart_comx_putc_ok())
			;
		uart_comx_putc(c);
}

static int command_comxtest(int argc, char **argv)
{
	/* Put characters to COMX port */
	const char *c = argc > 1 ? argv[1] : "testing comx output!";

	ccprintf("Writing \"%s\\r\\n\" to COMx UART...\n", c);

	while (*c)
		uart_comx_putc_wait(*c++);

	uart_comx_putc_wait('\r');
	uart_comx_putc_wait('\n');

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(comxtest, command_comxtest,
			"[string]",
			"Write test data to COMx uart",
			NULL);

#endif /* CONFIG_CMD_COMXTEST */

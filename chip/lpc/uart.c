#include "registers.h"
#include "uart.h"
#include "clock.h"

#include <stdbool.h>
#include <stdint.h>

/* Console USART index */
#define UARTN CONFIG_UART_CONSOLE
#define UARTN_BASE STM32_USART_BASE(CONFIG_UART_CONSOLE)

void uart_tx_start(void) {
}
void uart_tx_stop(void) {
}
int uart_tx_ready(void)
{
	return true;
}
void uart_tx_flush(void)
{
}
#include "gpio.h"

void uart_write_char(char c)
{
	const uint16_t ticks = clock_get_freq() / 115200;

	/* Start bit */
	uint16_t next = LPC_TMR_TC(16B0) + ticks;
	LPC_GPIO_DATA(LPC_GPIO_BASE(0), BIT(0)) = 0;
	while ((int16_t)(LPC_TMR_TC(16B0) - next) < 0)
		;
	/* 8 data bits, starting with LSB */
	for (int i = 0; i < 8; i++) {
		LPC_GPIO_DATA(LPC_GPIO_BASE(0), BIT(0)) = (c & BIT(i)) ? BIT(0) : 0;
		next += ticks;
		while ((int16_t)(LPC_TMR_TC(16B0) - next) < 0)
			;
	}
	/* Stop bit */
	LPC_GPIO_DATA(LPC_GPIO_BASE(0), BIT(0)) = BIT(0);
	next += ticks;
	while ((int16_t)(LPC_TMR_TC(16B0) - next) < 0)
		;
}

bool uart_initialized = false;

void uart_init(void)
{
	/* GPIO output on pin 0 */
	LPC_GPIO_DIR(LPC_GPIO_BASE(0)) |= BIT(0);

	LPC_GPIO_DATA(LPC_GPIO_BASE(0), BIT(0)) = BIT(0);
	
	/* Open drain bit-banged UART output on the RESET pin */
	LPC_IOCON_RESET_PIO0_0 |=
		(1 << LPC_IOCON_FUNC_POS) /* "alternate" function 1 is gpio */
		| LPC_IOCON_MODE_PULLUP | LPC_IOCON_OD;
	
	/* Enable clock to 16 bit timer 0 */
	LPC_SYSCFG_SYSAHBCLKCTRL |= LPC_SYSCFG_SYSAHBCLKCTRL_CT16B0;

	LPC_TMR_PR(16B0) = 0x00; /* No prescaling (i.e. factor 1) */
	LPC_TMR_TCR(16B0) = 0x01;

	uart_initialized = true;
}

int uart_init_done(void)
{
	return uart_initialized;
}


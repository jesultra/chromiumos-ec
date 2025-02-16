#include "clock.h"
#include "clock_chip.h"
#include "panic.h"
#include "registers.h"

#define LPC_RC_CLOCK 12000000

static int freq = LPC_RC_CLOCK;

void clock_init(void)
{
	/* Enable external oscillator */
	LPC_SYSCFG_PDRUNCFG &= ~LPC_SYSCFG_PDRUNCFG_SYSOSC_PD;

#ifdef LPC_USE_PLL
	/* Enable power to PLL (by zeroing its "power down" bit).  */
	LPC_SYSCFG_PDRUNCFG &= ~LPC_SYSCFG_PDRUNCFG_SYSPLL_PD;

	/* Select internal RC as source for PLL. */
	/*LPC_SYSCFG_SYSPLLCLKSEL = 0;*/
	/* Select external oscillator as source for PLL. */
	LPC_SYSCFG_SYSPLLCLKSEL = 1;

	/* Configure PLL */
	switch (LPC_PLLP) {
	case 1:
		LPC_SYSCFG_SYSPLLCTRL = ((LPC_PLLM - 1) << LPC_SYSCFG_PLLCTRL_MSEL_POS)
			| LPC_SYSCFG_PLLCTRL_PSEL_1;
		break;
	case 2:
		LPC_SYSCFG_SYSPLLCTRL = ((LPC_PLLM - 1) << LPC_SYSCFG_PLLCTRL_MSEL_POS)
			| LPC_SYSCFG_PLLCTRL_PSEL_2;
		break;
	case 4:
		LPC_SYSCFG_SYSPLLCTRL = ((LPC_PLLM - 1) << LPC_SYSCFG_PLLCTRL_MSEL_POS)
			| LPC_SYSCFG_PLLCTRL_PSEL_4;
		break;
	case 8:
		LPC_SYSCFG_SYSPLLCTRL = ((LPC_PLLM - 1) << LPC_SYSCFG_PLLCTRL_MSEL_POS)
			| LPC_SYSCFG_PLLCTRL_PSEL_8;
		break;
	default:
		panic("Unsupported clock divisor");
	}

	/* Rising edge of enable bit in order for the changes to take effect. */
	LPC_SYSCFG_SYSPLLCLKUEN = 0;
	LPC_SYSCFG_SYSPLLCLKUEN = 1;

	/* Wait for PLL lock. */
	while (!(LPC_SYSCFG_SYSPLLSTAT & LPC_SYSCFG_PLLSTAT_LOCK))
		;

	/* Select PLL as main clock source. */
	LPC_SYSCFG_MAINCLKSEL = 3;
	LPC_SYSCFG_MAINCLKUEN = 0;
	LPC_SYSCFG_MAINCLKUEN = 1;

	freq = LPC_RC_CLOCK * LPC_PLLM;
#endif
}

int clock_get_freq() {
	return freq;
}

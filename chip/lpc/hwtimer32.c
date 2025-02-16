#include "clock.h"
#include "common.h"
#include "hwtimer.h"
#include "registers.h"
#include "task.h"

#include <stdint.h>

uint32_t __hw_clock_source_read(void)
{
	return LPC_TMR_TC(SYS_TIMER);
}

void __hw_clock_event_set(uint32_t deadline)
{
	/* set the match on the deadline */
	LPC_TMR_MR1(SYS_TIMER) = deadline;
	/* Clear the match flags */
	LPC_TMR_IR(SYS_TIMER) = BIT(1);
	/* Set the match interrupt */
	LPC_TMR_MCR(SYS_TIMER) |= BIT(3);
}

void __hw_clock_event_clear(void)
{
	/* Disable the match interrupts */
	LPC_TMR_MCR(SYS_TIMER) &= ~BIT(3);
}

uint32_t __hw_clock_event_get(void)
{
	return LPC_TMR_MR1(SYS_TIMER);
}

int __hw_clock_source_init(uint32_t start_t)
{
	/* Enable clock to 32 bit timer 0. */
	LPC_SYSCFG_SYSAHBCLKCTRL |= LPC_SYSCFG_SYSAHBCLKCTRL_CT32B0;

	/* Set prescaler for one tick per microsecond. */
	LPC_TMR_PR(SYS_TIMER) = (clock_get_freq() / SECOND) - 1;

	/* Enable interrupt on match channel 0. */
	LPC_TMR_MCR(SYS_TIMER) = BIT(0);

	/* Interrupt on overflow (that is, when counter "increments" to zero). */
	LPC_TMR_MR0(SYS_TIMER) = 0;

	/* Start counting. */
	LPC_TMR_TCR(SYS_TIMER) = 0x01;

	task_enable_irq(IRQ_TIM(SYS_TIMER));
	return IRQ_TIM(SYS_TIMER);
}

static void __hw_clock_source_irq(void)
{
	uint32_t stat_tim = LPC_TMR_IR(SYS_TIMER);

	/* Clear status */
	LPC_TMR_IR(SYS_TIMER) = stat_tim;

	/*
	 * Find expired timers and set the new timer deadline
	 * signal overflow if the update interrupt flag is set.
	 */
	process_timers(stat_tim & 0x01);
}
DECLARE_IRQ(IRQ_TIM(SYS_TIMER), __hw_clock_source_irq, 1);


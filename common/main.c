/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Main routine for Chrome EC
 */

#include "registers.h"

#include "board_config.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "dma.h"
#include "eeprom.h"
#include "eoption.h"
#include "flash.h"
#include "gpio.h"
#include "hooks.h"
#include "jtag.h"
#include "keyboard_scan.h"
#ifdef CONFIG_MPU
#include "mpu.h"
#endif
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "watchdog.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

#define MEC_GPIO_BASE 0x40081000
#define MEC_GPIO_PIN_(x) (MEC_GPIO_BASE + ((x) << 2))
#define MEC_GPIO_PIN(x) REG32(MEC_GPIO_PIN_(x))

#define MEC_UART_CONFIG_BASE 0x400f1f00
#define MEC_UART_RUNTIME_BASE 0x400f1c00

#define MEC_PWR_BASE 0x40080100

#define MEC_VBAT_BASE 0x4000a400

#define MEC_LPC_CONFIG_BASE 0x400f3300

#define MEC_TMR0_BASE 0x40000c80

void set_led(int idx, int on)
{
	MEC_GPIO_PIN(0153+idx) = on ? 0x240 : 0x10240;
}

void set_int5(int val)
{
	int i;
	for (i = 0; i < 5; ++i)
		MEC_GPIO_PIN(i+1) = (val & (1 << i)) ? 0x10240 : 0x240;
	MEC_GPIO_PIN(6) = 0x240;
	set_led(2, 1);
	for (i = 0; i < 100000; ++i)
		;
	set_led(2, 0);
	MEC_GPIO_PIN(6) = 0x10240;
}

static void myprintnumrec(uint32_t v)
{
	int i = v % 10;
	if (!v)
		return;
	myprintnumrec(v / 10);
	uart_write_char(i + '0');
}

static void myprintnum(uint32_t v)
{
	if (!v)
		uart_write_char('0');
	myprintnumrec(v);
	uart_write_char('\n');
	uart_write_char('\r');
}

void test_timer(void)
{
	int i, j;

	/* Enable */
	REG32(MEC_TMR0_BASE + 0x10) |= (1 << 0);

	/* Pre-scale = 48 -> 1MHz -> Period = 1us */
	REG32(MEC_TMR0_BASE + 0x10) = (REG32(MEC_TMR0_BASE + 0x10) & 0xff) | (47 << 16);

	/* Count up */
	/*REG32(MEC_TMR0_BASE + 0x10) |= (1 << 2);*/

	REG32(MEC_TMR0_BASE + 0xc) |= 1;

	REG32(MEC_TMR0_BASE + 0x4) = 0xffffffff;

	REG32(MEC_TMR0_BASE + 0x0) = 0xffffffff;

	/* Auto restart */
	REG32(MEC_TMR0_BASE + 0x10) |= (1 << 3);

	/* Start */
	REG32(MEC_TMR0_BASE + 0x10) |= (1 << 5);

	myprintnum(REG32(MEC_TMR0_BASE + 0x0));
	for (j = 0; j < 10; ++j) {
		for (i = 0; i < 4000000; ++i)
			set_led(2, 0);
		myprintnum(REG32(MEC_TMR0_BASE + 0x0));
	}
}
#if 0
static void girq_ah(void)
{
	static int cur = 0;
	cur++;
	set_led(1, cur & (1 << 3));
	if (REG32(0x4000c130) & (1 << 4)) {
		REG32(0x4000c12c) |= (1 << 4);
	}
}
DECLARE_IRQ(MEC1322_IRQ_GIRQ23, girq_ah, 1);
static void girq_ah1(void)
{
	static int cur = 0;
	cur++;
	set_led(2, cur & (1 << 2));
	if (REG32(0x4000c130) & 0x01) {
		REG32(0x4000c12c) |= 0x01;
	}
}
DECLARE_IRQ(MEC1322_IRQ_TIMER32_0, girq_ah1, 1);
#endif
void test_interrupt(void)
{
	int i;

	set_led(1, 0);
	set_led(2, 0);
	set_led(3, 0);

	/*task_pre_init();*/

#if 1
	REG32(0x4000fc18) |= 1;
#else
	REG32(0x4000fc18) &= ~1;
#endif
	/*ccprintf("NVIC_EN: 0x%x\n", REG32(0x4000fc18));*/
	/*REG32(0x4000fc14) |= (1 << 17);*/
#if 0
	REG32(0x4000fc18) &= ~1;
#endif
	REG32(0x4000c12c) = (1 << 4); /* SOURCE */
	REG32(0x4000c138) = (1 << 4); /* ENABLE_CLEAR */
	REG32(0x4000c130) = (1 << 4); /* ENABLE */
	REG32(0x4000c200) = (1 << 23); /* BLOCK_EN */
	task_enable_irq(72);
	/*REG32(0xe000e108) |= (1 << 8);*/
	task_enable_irq(MEC1322_IRQ_TIMER32_0);
	asm("cpsie i");
	/*interrupt_enable();*/

	REG32(0x40000c90) = 0x10;
	for (i = 0; i < 100; ++i)
		asm("nop");
	REG32(0x40000c84) = 60000;
	REG32(0x40000c8c) = 1;
	REG32(0x40000c90) = 0x2f0029;

	while (1) {
		/*ccprintf("%d\n", REG32(0x40000c00));*/
	}
}

test_mockable int main(void)
{
	/*test_interrupt();*/
	/*
	 * Pre-initialization (pre-verified boot) stage.  Initialization at
	 * this level should do as little as possible, because verified boot
	 * may need to jump to another image, which will repeat this
	 * initialization.  In particular, modules should NOT enable
	 * interrupts.
	 */
#ifdef CONFIG_BOARD_PRE_INIT
	board_config_pre_init();
#endif

#ifdef CONFIG_MPU
	mpu_pre_init();
#endif

	/* Configure the pin multiplexers and GPIOs */
	gpio_pre_init();

#ifdef CONFIG_BOARD_POST_GPIO_INIT
	board_config_post_gpio_init();
#endif
	/*
	 * Initialize interrupts, but don't enable any of them.  Note that
	 * task scheduling is not enabled until task_start() below.
	 */
	task_pre_init();

	/*
	 * Initialize the system module.  This enables the hibernate clock
	 * source we need to calibrate the internal oscillator.
	 */
	system_pre_init();
	system_common_pre_init();

#ifdef CONFIG_FLASH
	/*
	 * Initialize flash and apply write protect if necessary.  Requires
	 * the reset flags calculated by system initialization.
	 */
	flash_pre_init();
#endif

	/* Set the CPU clocks / PLLs.  System is now running at full speed. */
	clock_init();

	/*
	 * Initialize timer.  Everything after this can be benchmarked.
	 * get_time() and udelay() may now be used.  usleep() requires task
	 * scheduling, so cannot be used yet.  Note that interrupts declared
	 * via DECLARE_IRQ() call timer routines when profiling is enabled, so
	 * timer init() must be before uart_init().
	 */
	timer_init();

	/* Main initialization stage.  Modules may enable interrupts here. */
	cpu_init();

#ifdef CONFIG_DMA
	/* Initialize DMA.  Must be before UART. */
	dma_init();
#endif

	/* Initialize UART.  Console output functions may now be used. */
	uart_init();

	if (system_jumped_to_this_image()) {
		CPRINTF("[%T UART initialized after sysjump]\n");
	} else {
		CPUTS("\n\n--- UART initialized after reboot ---\n");
		CPUTS("[Reset cause: ");
		system_print_reset_flags();
		CPUTS("]\n");
	}
	CPRINTF("[Image: %s, %s]\n",
		 system_get_image_copy_string(), system_get_build_info());

#ifdef CONFIG_WATCHDOG
	/*
	 * Intialize watchdog timer.  All lengthy operations between now and
	 * task_start() must periodically call watchdog_reload() to avoid
	 * triggering a watchdog reboot.  (This pretty much applies only to
	 * verified boot, because all *other* lengthy operations should be done
	 * by tasks.)
	 */
	watchdog_init();
#endif

	/*
	 * Verified boot needs to read the initial keyboard state and EEPROM
	 * contents.  EEPROM must be up first, so keyboard_scan can toggle
	 * debugging settings via keys held at boot.
	 */
#ifdef CONFIG_EEPROM
	eeprom_init();
#endif
#ifdef CONFIG_EOPTION
	eoption_init();
#endif
#ifdef HAS_TASK_KEYSCAN
	keyboard_scan_init();
#endif

	/* Initialize the hook library.  This calls HOOK_INIT hooks. */
	hook_init();

	/*
	 * Print the init time.  Not completely accurate because it can't take
	 * into account the time before timer_init(), but it'll at least catch
	 * the majority of the time.
	 */
	CPRINTF("[%T Inits done]\n");

	/* Launch task scheduling (never returns) */
	return task_start();
}

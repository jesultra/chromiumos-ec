/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Clocks and power management settings */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "watchdog.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CLOCK, outstr)
#define CPRINTF(format, args...) cprintf(CC_CLOCK, format, ## args)

static int freq = 48000000;

void clock_enable_pll(int enable, int notify)
{
}

void clock_wait_cycles(uint32_t cycles)
{
	asm("1: subs %0, #1\n"
	    "   bne 1b\n" :: "r"(cycles));
}

int clock_get_freq(void)
{
	return freq;
}

void clock_init(void)
{
	/* XOSEL = Single ended clock source */
	MEC1322_VBAT_CE |= 0x1;

	/* 32K clock enable */
	MEC1322_VBAT_CE |= 0x2;

	return;
}

void clock_enable_peripheral(uint32_t offset, uint32_t mask, uint32_t mode)
{
}

void clock_disable_peripheral(uint32_t offset, uint32_t mask, uint32_t mode)
{
}

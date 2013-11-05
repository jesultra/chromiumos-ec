/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : LM4 hardware specific implementation */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"


#if 0
/**
 * Read the real-time clock.
 *
 * @param ss_ptr       Destination for sub-seconds value, if not null.
 *
 * @return the real-time clock seconds value.
 */
uint32_t system_get_rtc_sec_subsec(uint32_t *ss_ptr)
{
	uint32_t rtc, rtc2;
	uint32_t rtcss, rtcss2;

	/*
	 * The hibernate module isn't synchronized, so need to read repeatedly
	 * to guarantee a valid read.
	 */
	do {
		rtc = LM4_HIBERNATE_HIBRTCC;
		rtcss = LM4_HIBERNATE_HIBRTCSS & 0x7fff;
		rtcss2 = LM4_HIBERNATE_HIBRTCSS & 0x7fff;
		rtc2 = LM4_HIBERNATE_HIBRTCC;
	} while (rtc != rtc2 || rtcss != rtcss2);

	if (ss_ptr)
		*ss_ptr = rtcss;

	return rtc;
}

timestamp_t system_get_rtc(void)
{
	uint32_t rtc, rtc_ss;
	timestamp_t time;

	rtc = system_get_rtc_sec_subsec(&rtc_ss);

	time.val = ((uint64_t)rtc) * SECOND + HIB_RTC_SUBSEC_TO_USEC(rtc_ss);
	return time;
}

/**
 * Set the real-time clock.
 *
 * @param seconds	New clock value.
 */
void system_set_rtc(uint32_t seconds)
{
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCLD = seconds;
	wait_for_hibctl_wc();
}

/**
 * Set the hibernate RTC match time at a given time from now
 *
 * @param seconds      Number of seconds from now for RTC match
 * @param microseconds Number of microseconds from now for RTC match
 */
static void set_hibernate_rtc_match_time(uint32_t seconds,
					uint32_t microseconds)
{
	uint32_t rtc, rtcss;

	/*
	 * Make sure that the requested delay is not less then the
	 * amount of time it takes to set the RTC match registers,
	 * otherwise, the match event could be missed.
	 */
	if (seconds == 0 && microseconds < HIB_SET_RTC_MATCH_DELAY_USEC)
		microseconds = HIB_SET_RTC_MATCH_DELAY_USEC;

	/* Calculate the wake match */
	rtc = system_get_rtc_sec_subsec(&rtcss) + seconds;
	rtcss += HIB_RTC_USEC_TO_SUBSEC(microseconds);
	if (rtcss > 0x7fff) {
		rtc += rtcss >> 15;
		rtcss &= 0x7fff;
	}

	/* Set RTC alarm match */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCM0 = rtc;
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCSS = rtcss << 16;
	wait_for_hibctl_wc();
}

/**
 * Use hibernate module to set up an RTC interrupt at a given
 * time from now
 *
 * @param seconds      Number of seconds before RTC interrupt
 * @param microseconds Number of microseconds before RTC interrupt
 */
void system_set_rtc_alarm(uint32_t seconds, uint32_t microseconds)
{
	/* Clear pending interrupt */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIC = LM4_HIBERNATE_HIBRIS;

	/* Set match time */
	set_hibernate_rtc_match_time(seconds, microseconds);

	/* Enable RTC interrupt on match */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIM = 1;

	/*
	 * Wait for the write to commit.  This ensures that the RTC interrupt
	 * actually gets enabled.  This is important if we're about to switch
	 * the system to the 30 kHz oscillator, which might prevent the write
	 * from comitting.
	 */
	wait_for_hibctl_wc();
}

/**
 * Disable and clear the RTC interrupt.
 */
void system_reset_rtc_alarm(void)
{
	/* Disable hibernate interrupts */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIM = 0;

	/* Clear interrupts */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIC = LM4_HIBERNATE_HIBRIS;
}

/**
 * Hibernate module interrupt
 */
static void __hibernate_irq(void)
{
	system_reset_rtc_alarm();
}
DECLARE_IRQ(LM4_IRQ_HIBERNATE, __hibernate_irq, 1);

/**
 * Enable hibernate interrupt
 */
void system_enable_hib_interrupt(void)
{
	task_enable_irq(LM4_IRQ_HIBERNATE);
}

/**
 * Internal hibernate function.
 *
 * @param seconds      Number of seconds to sleep before RTC alarm
 * @param microseconds Number of microseconds to sleep before RTC alarm
 * @param flags        Additional hibernate wake flags
 */
static void hibernate(uint32_t seconds, uint32_t microseconds, uint32_t flags)
{
	uint32_t hibctl;

	/* Set up wake reasons and hibernate flags */
	hibctl = LM4_HIBERNATE_HIBCTL | LM4_HIBCTL_PINWEN;

	if (flags & HIBDATA_WAKE_PIN)
		hibctl |= LM4_HIBCTL_PINWEN;
	else
		hibctl &= ~LM4_HIBCTL_PINWEN;

	if (seconds || microseconds) {
		hibctl |= LM4_HIBCTL_RTCWEN;
		flags |= HIBDATA_WAKE_RTC;

		set_hibernate_rtc_match_time(seconds, microseconds);

		/* Enable RTC interrupt on match */
		wait_for_hibctl_wc();
		LM4_HIBERNATE_HIBIM = 1;
	} else {
		hibctl &= ~LM4_HIBCTL_RTCWEN;
	}
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBCTL = hibctl;

	/* Clear pending interrupt */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIC = LM4_HIBERNATE_HIBRIS;

	/* Store hibernate flags */
	hibdata_write(HIBDATA_INDEX_WAKE, flags);

	__enter_hibernate(hibctl | LM4_HIBCTL_HIBREQ);
}

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	/* Flush console before hibernating */
	cflush();
	hibernate(seconds, microseconds, HIBDATA_WAKE_PIN);
}

void system_pre_init(void)
{
	uint32_t hibctl;

	/*
	 * Enable clocks to the hibernation module in run, sleep,
	 * and deep sleep modes.
	 */
	clock_enable_peripheral(CGC_OFFSET_HIB, 0x1, CGC_MODE_ALL);

	/*
	 * Enable the hibernation oscillator, if it's not already enabled.
	 * This should only need setting if the EC completely lost power (for
	 * example, the battery was pulled).
	 */
	if (!(LM4_HIBERNATE_HIBCTL & LM4_HIBCTL_CLK32EN)) {
		int i;

		/* Enable clock to hibernate module */
		wait_for_hibctl_wc();
		LM4_HIBERNATE_HIBCTL |= LM4_HIBCTL_CLK32EN;

		/* Wait for write-complete */
		for (i = 0; i < 1000000; i++) {
			if (LM4_HIBERNATE_HIBRIS & 0x10)
				break;
		}

		/* Enable and reset RTC */
		wait_for_hibctl_wc();
		LM4_HIBERNATE_HIBCTL |= LM4_HIBCTL_RTCEN;
		system_set_rtc(0);

		/* Clear all hibernate data entries */
		for (i = 0; i < LM4_HIBERNATE_HIBDATA_ENTRIES; i++)
			hibdata_write(i, 0);
	}

	/*
	 * Set wake reasons to RTC match and WAKE pin by default.
	 * Before going in to hibernate, these may change.
	 */
	hibctl = LM4_HIBERNATE_HIBCTL;
	hibctl |= LM4_HIBCTL_RTCWEN;
	hibctl |= LM4_HIBCTL_PINWEN;
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBCTL = hibctl;

	/*
	 * Initialize registers after reset to work around LM4 chip errata
	 * (still present in A3 chip stepping).
	 */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCT = 0x7fff;
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIM = 0;

	check_reset_cause();

	/* Initialize bootcfg if needed */
	if (LM4_SYSTEM_BOOTCFG != CONFIG_BOOTCFG_VALUE) {
		/* read-modify-write */
		LM4_FLASH_FMD = (LM4_SYSTEM_BOOTCFG_MASK & LM4_SYSTEM_BOOTCFG)
			| (~LM4_SYSTEM_BOOTCFG_MASK & CONFIG_BOOTCFG_VALUE);
		LM4_FLASH_FMA = 0x75100000;
		LM4_FLASH_FMC = 0xa4420008;  /* WRKEY | COMT */
		while (LM4_FLASH_FMC & 0x08)
			;
	}

	/* Brown-outs should trigger a reset */
	LM4_SYSTEM_PBORCTL |= 0x02;
}

void system_reset(int flags)
{
	uint32_t save_flags = 0;

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	/* Save current reset reasons if necessary */
	if (flags & SYSTEM_RESET_PRESERVE_FLAGS)
		save_flags = system_get_reset_flags() | RESET_FLAG_PRESERVED;

	if (flags & SYSTEM_RESET_LEAVE_AP_OFF)
		save_flags |= RESET_FLAG_AP_OFF;

	hibdata_write(HIBDATA_INDEX_SAVED_RESET_FLAGS, save_flags);

	if (flags & SYSTEM_RESET_HARD) {
		/*
		 * Bounce through hibernate to trigger a hard reboot.  Do
		 * not wake on wake pin, since we need the full duration.
		 */
		hibernate(0, HIB_RESET_USEC, HIBDATA_WAKE_HARD_RESET);
	} else
		CPU_NVIC_APINT = 0x05fa0004;

	/* Spin and wait for reboot; should never return */
	while (1)
		;
}

int system_set_scratchpad(uint32_t value)
{
	return hibdata_write(HIBDATA_INDEX_SCRATCHPAD, value);
}

uint32_t system_get_scratchpad(void)
{
	return hibdata_read(HIBDATA_INDEX_SCRATCHPAD);
}

const char *system_get_chip_vendor(void)
{
	return "ti";
}

static char to_hex(int x)
{
	if (x >= 0 && x <= 9)
		return '0' + x;
	return 'a' + x - 10;
}

const char *system_get_chip_id_string(void)
{
	static char str[15] = "Unknown-";
	char *p = str + 8;
	uint32_t did = LM4_SYSTEM_DID1 >> 16;

	if (*p)
		return (const char *)str;

	*p = to_hex(did >> 12);
	*(p + 1) = to_hex((did >> 8) & 0xf);
	*(p + 2) = to_hex((did >> 4) & 0xf);
	*(p + 3) = to_hex(did & 0xf);
	*(p + 4) = '\0';

	return (const char *)str;
}

const char *system_get_raw_chip_name(void)
{
	switch ((LM4_SYSTEM_DID1 & 0xffff0000) >> 16) {
	case 0x10de:
		return "tm4e1g31h6zrb";
	case 0x10e2:
		return "lm4fsxhh5bb";
	case 0x10e3:
		return "lm4fs232h5bb";
	case 0x10e4:
		return "lm4fs99h5bb";
	case 0x10e6:
		return "lm4fs1ah5bb";
	case 0x10ea:
		return "lm4fs1gh5bb";
	default:
		return system_get_chip_id_string();
	}
}

const char *system_get_chip_name(void)
{
	const char *postfix = "-tm"; /* test mode */
	static char str[20];
	const char *raw_chip_name = system_get_raw_chip_name();
	char *p = str;

	if (LM4_TEST_MODE_ENABLED) {
		/* Debug mode is enabled. Postfix chip name. */
		while (*raw_chip_name)
			*(p++) = *(raw_chip_name++);
		while (*postfix)
			*(p++) = *(postfix++);
		*p = '\0';
		return (const char *)str;
	} else {
		return raw_chip_name;
	}
}

int system_get_vbnvcontext(uint8_t *block)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int system_set_vbnvcontext(const uint8_t *block)
{
	return EC_ERROR_UNIMPLEMENTED;
}

const char *system_get_chip_revision(void)
{
	static char rev[3];

	/* Extract the major[15:8] and minor[7:0] revisions. */
	rev[0] = 'A' + ((LM4_SYSTEM_DID0 >> 8) & 0xff);
	rev[1] = '0' + (LM4_SYSTEM_DID0 & 0xff);
	rev[2] = 0;

	return rev;
}

/*****************************************************************************/
/* Console commands */

static int command_system_rtc(int argc, char **argv)
{
	uint32_t rtc;
	uint32_t rtcss;

	if (argc == 3 && !strcasecmp(argv[1], "set")) {
		char *e;
		uint32_t t = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		system_set_rtc(t);
	} else if (argc > 1) {
		return EC_ERROR_INVAL;
	}

	rtc = system_get_rtc_sec_subsec(&rtcss);
	ccprintf("RTC: 0x%08x.%04x (%d.%06d s)\n",
		 rtc, rtcss, rtc, HIB_RTC_SUBSEC_TO_USEC(rtcss));

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rtc, command_system_rtc,
			"[set <seconds>]",
			"Get/set real-time clock",
			NULL);

#ifdef CONFIG_CMD_RTC_ALARM
/**
 * Test the RTC alarm by setting an interrupt on RTC match.
 */
static int command_rtc_alarm_test(int argc, char **argv)
{
	int s = 1, us = 0;
	char *e;

	ccprintf("Setting RTC alarm\n");
	system_enable_hib_interrupt();

	if (argc > 1) {
		s = strtoi(argv[1], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;

	}
	if (argc > 2) {
		us = strtoi(argv[2], &e, 10);
		if (*e)
			return EC_ERROR_PARAM2;

	}

	system_set_rtc_alarm(s, us);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rtc_alarm, command_rtc_alarm_test,
			"[seconds [microseconds]]",
			"Test alarm",
			NULL);
#endif /* CONFIG_CMD_RTC_ALARM */

/*****************************************************************************/
/* Host commands */

static int system_rtc_get_value(struct host_cmd_handler_args *args)
{
	struct ec_response_rtc *r = args->response;

	r->time = system_get_rtc_sec_subsec(NULL);
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_GET_VALUE,
		     system_rtc_get_value,
		     EC_VER_MASK(0));

static int system_rtc_set_value(struct host_cmd_handler_args *args)
{
	const struct ec_params_rtc *p = args->params;

	system_set_rtc(p->time);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_SET_VALUE,
		     system_rtc_set_value,
		     EC_VER_MASK(0));
#endif 

#define DUMMY(x) x {}
#define DUMMY_int(x) x { return 0; }

/*DUMMY(void system_reset(int flags));*/
const char *system_get_chip_vendor(void) { return "SMSC"; }
const char *system_get_chip_name(void) { return "MEC1322"; }
const char *system_get_chip_revision(void) { return "0"; }
DUMMY(void system_hibernate(uint32_t seconds, uint32_t microseconds));
DUMMY_int(int system_get_vbnvcontext(uint8_t *block));
DUMMY_int(int system_set_vbnvcontext(const uint8_t *block));
/*DUMMY(void system_pre_init(void));*/

void system_pre_init(void)
{
	/* Enable direct NVIC */
	MEC1322_EC_INT_CTRL |= 1;
}

void system_reset(int flags) {
	/* Trigger watchdog in 1ms */
	REG32(0x40000400) = 1;
	REG32(0x40000404) |= 1;
}

/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * A module that provides time synchronization between the AP and the EC.
 * The AP can send a host command (EC_CMD_SYNC_RTC) with its current RTC
 * timestamp as seconds-since-epoch. The EC will store its current seconds-
 * since-boot when it gets the host command along with the AP's timestamp.
 *
 * The "current date and time" can be retrieved using get_host_synced_rtc.
 *
 * "Current date and time" is calculated as a delta, in seconds, from when
 * the AP synced its time with the EC.
 */

#include "rtc.h"
#include "hooks.h"
#include "host_command.h"
#include "console.h"
#include "common.h"
#include "timer.h"
#include "system.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

#if defined(CONFIG_HOSTCMD_RTC_SYNC)
/*
 * Local timestamp indicating when the host
 * synchronized RTC time with the EC
 */
struct epoch_sync {
	timestamp_t local_timestamp;
	uint64_t host_epoch_sec;
};
static struct epoch_sync local_epoch;

#define LOCAL_EPOCH_SYSJUMP_TAG 0x8284 /* RT */
#define LOCAL_EPOCH_HOOK_VERSION 1

/**
 * Preserve synchronized RTC across a sysjump.
 */
static void synced_rtc_preserve_state(void)
{
	system_add_jump_tag(LOCAL_EPOCH_SYSJUMP_TAG, LOCAL_EPOCH_HOOK_VERSION,
			sizeof(local_epoch), &local_epoch);
}
DECLARE_HOOK(HOOK_SYSJUMP, synced_rtc_preserve_state, HOOK_PRIO_DEFAULT);

/**
 * Restore synchronized RTC after a sysjump.
 */
static void synced_rtc_restore_state(void)
{
	const struct epoch_sync *prev_epoch;
	int size, version;

	prev_epoch = (const struct epoch_sync *)system_get_jump_tag(
		LOCAL_EPOCH_SYSJUMP_TAG, &version, &size);

	if (prev_epoch && version == LOCAL_EPOCH_HOOK_VERSION &&
		size == sizeof(struct epoch_sync)) {
		memcpy(&local_epoch, prev_epoch, sizeof(local_epoch));
	}
}
DECLARE_HOOK(HOOK_INIT, synced_rtc_restore_state, HOOK_PRIO_DEFAULT);

/* Synchronize EC's MTC and host RTC */
void set_rtc_epoch(timestamp_t local_time, uint64_t host_epoch_sec)
{
	local_epoch.local_timestamp = local_time;
	local_epoch.host_epoch_sec = host_epoch_sec;
}

/* Get the current date/time, as synchronized from the host */
struct calendar_date get_host_synced_rtc(void)
{
	timestamp_t current_ts = get_time();

	/*
	 * Get the difference between the synced epoch time
	 * and the current timestamp in seconds.  Add the difference
	 * to the synced epoch seconds and convert to calendar_date
	 */
	const uint64_t us_diff = (current_ts.val -
				local_epoch.local_timestamp.val);
	const uint64_t sec_diff = us_diff / (uint64_t)SECOND;
	const uint64_t current_rtc_sec = local_epoch.host_epoch_sec + sec_diff;

	return sec_to_date(current_rtc_sec);
}

static void print_synced_rtc(void)
{
	struct calendar_date tm = get_host_synced_rtc();

	if (tm.year != 0) {
		/* struct calendar_date only stores years since 2000 */
		const int real_year = tm.year + 2000;

		CPRINTF("RTC: %02u-%02u-%04u %02u:%02u:%02u\n",
			tm.month, tm.day, real_year,
			tm.hour, tm.minute, tm.second);
	}
}


/* Synchronize current time from the host */
static int host_command_sync_rtc(struct host_cmd_handler_args *args)
{
	/* Get the EC timestamp ASAP */
	timestamp_t tm = get_time();
	const struct ec_params_rtc_sync *r = args->params;
	const uint64_t host_epoch = r->host_epoch_sec;

	/* Synchronize EC time with RTC time */
	set_rtc_epoch(tm, host_epoch);
	print_synced_rtc();

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SYNC_RTC,
		     host_command_sync_rtc,
		     EC_VER_MASK(0));

#if defined(HAS_TASK_CHIPSET)
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, print_synced_rtc, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, print_synced_rtc, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, print_synced_rtc, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, print_synced_rtc, HOOK_PRIO_DEFAULT);
#endif /* defined(HAS_CHIPSET_TASK) */
#endif /* defined(CONFIG_HOSTCMD_RTC_SYNC) */

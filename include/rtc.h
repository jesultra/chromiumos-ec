/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* RTC cross-platform functions */

#ifndef __CROS_EC_RTC_H
#define __CROS_EC_RTC_H

#include "common.h"

#define SECS_PER_MINUTE     60U
#define MINUTES_PER_HOUR    60U
#define SECS_PER_HOUR       (SECS_PER_MINUTE * MINUTES_PER_HOUR)
#define SECS_PER_DAY        (SECS_PER_HOUR * 24)
#define SECS_PER_YEAR       (365 * SECS_PER_DAY)
/* The seconds elapsed from 01-01-1970 to 01-01-2000 */
#define SECS_TILL_YEAR_2K   (946684800U)
#define IS_LEAP_YEAR(x)     \
	(((x) % 4 == 0) && (((x) % 100 != 0) || ((x) % 400 == 0)))

struct calendar_date {
	/* The number of years since A.D. 2000, i.e. year = 17 for y2017 */
	uint8_t year;
	/* 1-based indexing, i.e. sane values range from 1 to 12 */
	uint8_t month;
	/* 1-based indexing, i.e. sane values range from 1 to 31 */
	uint8_t day;
	/* 0-based, 24-hour, 0 to 23 */
	uint8_t hour;
	/* 0-based, 0 to 59 */
	uint8_t minute;
	/* 0-based, 0 to 59 */
	uint8_t second;
};

/**
 * Convert calendar date to seconds elapsed since epoch time.
 *
 * @param time  The calendar date (years, months, and days).
 * @return the calendar date and time (years, months, days, hours, minutes,
 *   and seconds).
 */
uint64_t date_to_sec(struct calendar_date time);

/**
 * Convert seconds elapsed since epoch time to calendar date
 *
 * @param sec  The seconds elapsed since epoch time (01-01-1970 00:00:00).
 * @return the calendar date and time (years, months, days, hours, minutes,
 *   and seconds).
 */
struct calendar_date sec_to_date(uint64_t sec);

#endif /* __CROS_EC_RTC_H */

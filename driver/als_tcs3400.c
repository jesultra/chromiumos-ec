/* Copyright 2019 The Chromium OS Authors. All rights reserved.
* Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AMS TCS3400 light sensor driver
 */
#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "driver/als_tcs3400.h"
#include "hooks.h"
#include "hwtimer.h"
#include "i2c.h"
#include "math_util.h"
#include "task.h"
#include "util.h"

#define CPRINTS(fmt, args...) cprints(CC_ACCEL, "%s "fmt, __func__, ## args)

/*
 * #define TEST_MODE  to enable test mode to generate the LUX table
 */
#undef TEST_MODE

/*
 * 0 = use a step constant when adjusting atime
 * 1 = use the Lux atime table when determining how to adjust atime
 */
#define TCS_USE_LUX_TABLE 1

enum alslog_level {
	DISABLED = 0,
	ERRORS = 1,
	PRIORITY = 2,
	RAW_DATA = 4,
	SATURATION = 8,
	DEBUG = 16,
	VERBOSE = 32,
	FLOW = 64,
	GRAPH = 128,
	SCALING = 256,
	XYZ_XLATE = 512,
	TEST = 1024,
};

#ifdef CONFIG_CMD_ALSLOG
#ifdef TEST_MODE
static int gAlsLogMask = TEST;
#else
static int gAlsLogMask;
#endif
static int32_t negative_fp(fp_t fp)
{
	return ((fp & BIT(31)) ? 1 : 0);
}

static int32_t ones_from_fp(fp_t fp)
{
	if (fp & BIT(31))
		return ((~(fp) + 1) >> 16);
	else
		return (fp >> 16);
}

static int32_t remainder_from_fp(fp_t fp)
{
	if (fp & BIT(31))
		fp = ~(fp) + 1;
	return ((fp & 0xffff) * 10000 / 65535);
}

static int command_log_als_data(int argc, char **argv)
{
	/* toggle log state */
	gAlsLogMask = (gAlsLogMask) ? DISABLED : PRIORITY;
	CPRINTS("ALS data logging now %sabled", gAlsLogMask ? "en" : "dis");
	if (argc > 1) {
		gAlsLogMask = atoi(argv[1]);
		CPRINTS("ALS data logging mask set to %d", gAlsLogMask);
		if (gAlsLogMask == 0) {
			fp_t fp = FLOAT_TO_FP(-3.5);

			CPRINTS("FLOAT_TO_FP(-3.5) = %d.%04d ones = %d "
				"(0x%08x), remainder = %d (0x%08x)",
				ones_from_fp(fp), remainder_from_fp(fp),
				ones_from_fp(fp), ones_from_fp(fp),
				remainder_from_fp(fp), remainder_from_fp(fp));
			fp = FLOAT_TO_FP(-1.7);
			CPRINTS("FLOAT_TO_FP(-1.7) = %d.%04d ones = %d "
				"(0x%08x), remainder = %d (0x%08x)",
				ones_from_fp(fp),
				remainder_from_fp(fp), ones_from_fp(fp),
				ones_from_fp(fp), remainder_from_fp(fp),
				remainder_from_fp(fp));
			fp = FLOAT_TO_FP(1.2505);
			CPRINTS("FLOAT_TO_FP(1.2505) = %d.%04d ones = %d "
				"(0x%08x), remainder = %d (0x%08x)",
				ones_from_fp(fp), remainder_from_fp(fp),
				ones_from_fp(fp), ones_from_fp(fp),
				remainder_from_fp(fp), remainder_from_fp(fp));
		}
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(alslog, command_log_als_data,
	"",
	"Toggle state of ALS data logging.");

#define ALSLOG(level, format, args...) \
	do { \
		if (gAlsLogMask & level) \
			CPRINTS(format, ## args); \
	} while (0)
#else
#define ALSLOG(level, format, args...) \
	do { } while (0)
#endif


#ifdef CONFIG_ACCEL_FIFO
static volatile uint32_t last_interrupt_timestamp;
#endif

#if TCS_USE_LUX_TABLE
/*
 * Stores the number of atime increments/decrements needed to change light value
 * by 1% of saturation for each gain setting for each predefined LUX range.
 *
 * Values in array are TCS_ATIME_GAIN_FACTOR (100x) times actual value to allow
 * for fractions using integers.
 */
static const uint32_t
range_atime[TCS_MAX_AGAIN-TCS_MIN_AGAIN+1][TCS_MAX_ATIME_RANGES] = {
{11200, 5600, 5600, 7200, 5500, 4500, 3800, 3800, 3300, 2900, 2575, 2275, 2075},
{11200, 5100, 2700, 1840, 1400, 1133, 981, 963, 833, 728, 650, 577, 525},
{250, 1225, 643, 441, 337, 276, 237, 235, 203, 176, 150, 0, 0},
{790, 311, 163, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} };
#endif /* TCS_USE_LUX_TABLE */

static inline int tcs3400_i2c_read8(const struct motion_sensor_t *s,
				    int reg, int *data)
{
	return i2c_read8(s->port, s->i2c_spi_addr_flags, reg, data);
}

static inline int tcs3400_i2c_write8(const struct motion_sensor_t *s,
				     int reg, int data)
{
	return i2c_write8(s->port, s->i2c_spi_addr_flags, reg, data);
}

static void tcs3400_read_deferred(void)
{
	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_ALS_TCS3400_INT_EVENT, 0);
}
DECLARE_DEFERRED(tcs3400_read_deferred);

/* convert ATIME register to integration time, in microseconds */
static int tcs3400_get_integration_time(int atime)
{
	return 2780 * (256 - atime);
}

static int tcs3400_read(const struct motion_sensor_t *s, intv3_t v)
{
	int atime, again;
	int ret;

	/* Chip may have been off, make sure to setup important registers */
	if (TCS3400_RGB_DRV_DATA(s+1)->calibration_mode == TCS_CAL_MODE) {
		atime = TCS_CALIBRATION_ATIME;
		again = TCS_CALIBRATION_AGAIN;
	} else {
		atime = TCS3400_RGB_DRV_DATA(s+1)->saturation.atime;
		again = TCS3400_RGB_DRV_DATA(s+1)->saturation.again;
	}
	ret = tcs3400_i2c_write8(s, TCS_I2C_ATIME, atime);
	if (ret)
		return ret;
	ret = tcs3400_i2c_write8(s, TCS_I2C_CONTROL, again);
	if (ret)
		return ret;

	/* Enable power, ADC, and interrupt to start cycle */
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, TCS3400_MODE_COLLECTING);
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_ALS_TCS3400_EMULATED_IRQ_EVENT)) {
		int atime;

		ret = tcs3400_i2c_read8(s, TCS_I2C_ATIME, &atime);
		if (ret)
			return ret;

		hook_call_deferred(&tcs3400_read_deferred_data,
				tcs3400_get_integration_time(atime));
	}

	/*
	 * If write succeeded, we've started the read process, but can't
	 * complete it yet until data is ready, so pass back EC_RES_IN_PROGRESS
	 * to inform upper level that read data process is under way and data
	 * will be delivered when available.
	 */
	return EC_RES_IN_PROGRESS;
}

static int tcs3400_rgb_read(const struct motion_sensor_t *s, intv3_t v)
{
	return EC_SUCCESS;
}

#ifndef TEST_MODE
#if TCS_USE_LUX_TABLE
static void
decrement_atime(struct tcs_saturation_t *sat_p, uint16_t cur_lux, int percent)
{
	int steps;
	int lux = MIN(cur_lux, TCS_GAIN_TABLE_MAX_LUX);

	steps = percent * range_atime[sat_p->again][lux/1000] /
			TCS_ATIME_GAIN_FACTOR;
	sat_p->atime = MAX(sat_p->atime - steps, TCS_MIN_ATIME);
	ALSLOG((SATURATION|GRAPH), "decrement ATIME = %d", sat_p->atime);
}
#else
static void decrement_atime(struct tcs_saturation_t *sat_p)
{
	sat_p->atime = MAX(sat_p->atime - TCS_ATIME_DEC_STEP, TCS_MIN_ATIME);
}
#endif /* TCS_USE_LUX_TABLE */

static void increment_atime(struct tcs_saturation_t *sat_p)
{
	sat_p->atime = MIN(sat_p->atime + TCS_ATIME_INC_STEP, TCS_MAX_ATIME);
	ALSLOG((SATURATION|GRAPH), "increment ATIME = %d", sat_p->atime);
}
#endif /* TEST_MODE */

#ifdef CONFIG_CMD_ALSLOG
void log(uint16_t light_val, uint16_t lux, struct tcs_saturation_t *sat_p,
		uint16_t *raw_data)
{
	ALSLOG((GRAPH|TEST), "lux: %d, %d%% saturation, GAIN: %d, ATIME: %d, "
	       "[ 0x%x 0x%x 0x%x 0x%x ",
	       lux, light_val * 100 / TCS_SATURATION_LEVEL,
	       sat_p->again, sat_p->atime,
	       raw_data[0], raw_data[1], raw_data[2], raw_data[3]);
}
#endif


#ifdef TEST_MODE
static uint16_t g_cur_percentage;
static uint8_t  g_atime_bump_idx;
static uint16_t g_atime_bumps[TCS_MAX_AGAIN-TCS_MIN_AGAIN+1][100];
static uint32_t g_running_avg[TCS_MAX_AGAIN-TCS_MIN_AGAIN+1];
static uint8_t g_test_done;

/*
 * Run through each atime setting for each gain level
 */
enum test_stages {
	TCS_TEST_STAGE_S1 = 0,
	TCS_TEST_STAGE_S2,
	TCS_TEST_STAGE_S3,
	TCS_TEST_STAGE_S4,
	TCS_TEST_STAGE_S5,
	TCS_TEST_STAGE_S6,
};


/****************************************************************
 * Test Mode will walk the device through each atime setting
 * in each again setting and calculate how many atime increments
 * it takes to increase light intensity by 1% at that particular
 * light level setting.  This data is then used to optimize the
 * saturation auto-adjustment mechanism.
 *
 *  Stage 1) set again = 0
 *  Stage 2) set atime = TCS_MAX_ATIME
 *           set g_atime_bump_idx = 0
 *  Stage 3) run cycle
 *  Stage 4) if (atime == TCS_MAX_ATIME)
 *              set g_cur_percentage = current percentage of saturation
 *           else if (current percentage != g_cur_percentage)
 *              a) g_cur_percentage = current percentage
 *              b) g_atime_bump_idx++
 *
 *           if (atime > TCS_MIN_ATIME)
 *              a) atime--
 *              b) g_atime_bumps[g_atime_bump_idx]++
 *              c) goto stage 3
 *
 *           if (again == TCS_MAX_AGAIN)
 *              a) calculate table
 *              b) print table
 *              c) exit, we're done
 *           else
 *              a) again++
 *              b) goto stage 2
 *
 ****************************************************************/
static int g_test_stage = TCS_TEST_STAGE_S1;
static int g_last_lux;
static int next_test_setting(struct motion_sensor_t *s, int saturation,
			     uint32_t percentage, int32_t lux)
{
	struct tcs_saturation_t *sat_p =
			&TCS3400_RGB_DRV_DATA(s+1)->saturation;
	int ret = EC_SUCCESS;
	uint16_t orig_atime = sat_p->atime;
	uint16_t orig_again = sat_p->again;
	uint32_t running_avg_sum;
	int x, y;

	if (saturation && (g_test_done == 0))
		g_test_stage = TCS_TEST_STAGE_S5;

	do {
		switch (g_test_stage) {
		case TCS_TEST_STAGE_S1:
			sat_p->again = 0;
			/* fall thru to TCS_TEST_STAGE_S2 */

		case TCS_TEST_STAGE_S2:
			sat_p->atime = TCS_MAX_ATIME;
			g_atime_bump_idx = 0;
			/* fall thru to TCS_TEST_STAGE_S3 */

		case TCS_TEST_STAGE_S3:
			if (orig_again != sat_p->again) {
				ret = tcs3400_i2c_write8(s, TCS_I2C_CONTROL,
					(sat_p->again & TCS_I2C_CONTROL_MASK));
				if (ret)
					return ret;
			}

			if (orig_atime != sat_p->atime) {
				ret = tcs3400_i2c_write8(s, TCS_I2C_ATIME,
							 sat_p->atime);
				if (ret)
					return ret;
			}
			/* return to stage 4 */
			g_test_stage = TCS_TEST_STAGE_S4;
			return EC_SUCCESS; /* let next cycle run */

		case TCS_TEST_STAGE_S4:
			if (sat_p->atime == TCS_MAX_ATIME) {
				g_cur_percentage = percentage;
			} else if (percentage != g_cur_percentage) {
				g_cur_percentage = percentage;
				ALSLOG(TEST, "change after %d bumps",
				g_atime_bumps[sat_p->again][g_atime_bump_idx]);
				g_atime_bump_idx++;
			}

			if (sat_p->atime > TCS_MIN_ATIME) {
				sat_p->atime--;
				g_atime_bumps[sat_p->again][g_atime_bump_idx]++;
				g_test_stage = TCS_TEST_STAGE_S3;
				break;
			}

			if (sat_p->again < TCS_MAX_AGAIN) {
				sat_p->again++;
				g_test_stage = TCS_TEST_STAGE_S2;
			} else {
				g_test_stage = TCS_TEST_STAGE_S5;
				break;
			}
			break;

		case TCS_TEST_STAGE_S5:
			/*
			 * calculate average for each again level, throw
			 * out end values if three or more valid
			 */
			for (x = 0; x < 4; x++) {
				/* average bumps for this again level */
				y = 0;
				running_avg_sum = 0;
				while (g_atime_bumps[x][y] != 0) {
					running_avg_sum += (g_atime_bumps[x][y]
							* TCS_MAX_ATIME_RANGES);
					y++;
				}
				if (y >= 3) {
					/*
					 * remove first and last counts as they
					 * may not represent complete percentage
					 * leaps
					 */
					running_avg_sum -= ((g_atime_bumps[x][0]
						 * TCS_MAX_ATIME_RANGES) +
						 (g_atime_bumps[x][y-1] *
						    TCS_MAX_ATIME_RANGES));
					y -= 2;
				}
				if (y == 0)
					g_running_avg[x] = 0;
				else
					g_running_avg[x] = running_avg_sum / y;
			}
			ALSLOG(TEST, "AGAIN BUMPS LUX %d = [ %d, %d, %d, %d ",
			       g_last_lux, g_running_avg[0], g_running_avg[1],
			       g_running_avg[2], g_running_avg[3]);

			g_test_done = 1;
			gAlsLogMask = DISABLED;
			sat_p->again = TCS_MIN_AGAIN;
			sat_p->atime = TCS_MIN_ATIME;
			g_test_stage = TCS_TEST_STAGE_S6;
			/* fall thru to TCS_TEST_STAGE_S6 */

		case TCS_TEST_STAGE_S6:
			return EC_SUCCESS;

		default:
			break;
		}
	} while (1); /* exit via direct return statements */

	return ret;
}

#else

/*
 * tcs3400_adjust_sensor_for_saturation() tries to keep CRGB values as
 * close to saturation as possible without saturating by implementing
 * the following logic:
 *
 * If any of the R, G, B, or C channels have saturated, then decrease AGAIN.
 * If AGAIN is already at its minimum, increase ATIME if not at its max already.
 *
 * Else if none of the R, G, B, or C channels have saturated, and
 * all samples read are less than 90% of saturation, then increase
 * AGAIN if it is not already at its maximum, or if it is, decrease
 * ATIME if it is not at it's minimum already.
 */
static uint32_t g_saturation_count;
static int
tcs3400_adjust_sensor_for_saturation(struct motion_sensor_t *s,
				     uint16_t *crgb_data)
{
	struct tcs_saturation_t *sat_p =
			&TCS3400_RGB_DRV_DATA(s+1)->saturation;
	const uint8_t save_again = sat_p->again;
	const uint8_t save_atime = sat_p->atime;
	uint16_t max_val =  0;
	int ret = EC_SUCCESS;
	int status = 0;
	int percent_left = 0;

	/* Adjust for saturation if needed */
	ret = tcs3400_i2c_read8(s, TCS_I2C_STATUS, &status);
	if (ret)
		return ret;

	for (int i = 0; i < TCS_CHANNEL_COUNT; i++)
		max_val = MAX(max_val, crgb_data[i]);

#ifdef CONFIG_CMD_ALSLOG
	log(max_val, s->raw_xyz[X], sat_p, crgb_data);
#endif
	ALSLOG(SATURATION, "channels: 0x%x 0x%x 0x%x 0x%x", crgb_data[0],
			crgb_data[1], crgb_data[2], crgb_data[3]);

	if ((status & TCS_I2C_STATUS_ALS_VALID) ||
	    (max_val >= TCS_SATURATION_LEVEL)) {
		/* Saturation occurred, decrease AGAIN if we can */
		g_saturation_count++;
		if (sat_p->again > TCS_MIN_AGAIN) {
			sat_p->again--;
			ALSLOG(GRAPH, "decrement AGAIN = %d", sat_p->again);
			ALSLOG(SATURATION, "Sat #%d reduce AGAIN to %d",
			       g_saturation_count, sat_p->again);
		} else if (sat_p->atime < TCS_MAX_ATIME) {
			/* reduce accumulation time by incrementing ATIME reg */
			increment_atime(sat_p);
			ALSLOG(SATURATION, "Sat #%d increase ATIME to %d",
			       g_saturation_count, sat_p->atime);
		} else {
			ALSLOG(SATURATION, "Sat #%d: can't fix, already at "
			       "least sensitivity", g_saturation_count);
		}
	} else if (max_val < ((TCS_SATURATION_LEVEL *
				TSC_SATURATION_LOW_BAND_PERCENT) / 100)) {
		/* value < 90% saturation, try to increase sensitivity */
		/* increase AGAIN if we can without saturating */
		if (max_val <= TCS_GAIN_SAT_LEVEL) {
			if (sat_p->again < TCS_MAX_AGAIN) {
				sat_p->again++;
				ALSLOG(GRAPH, "increment AGAIN = %d",
				       sat_p->again);
				ALSLOG(SATURATION, "All < %dx of "
				       "saturation, inc AGAIN to %d",
				       TCS_GAIN_ADJUST_FACTOR,
				       sat_p->again);
			} else if (sat_p->atime > TCS_MIN_ATIME) {
				/* increase ATIME */
				percent_left = (max_val * 100 /
						TCS_GAIN_SAT_LEVEL);
				percent_left = TSC_SATURATION_LOW_BAND_PERCENT -
						percent_left;
#if TCS_USE_LUX_TABLE
				decrement_atime(sat_p, max_val, percent_left);
#else
				decrement_atime(sat_p);
#endif
				ALSLOG(SATURATION, "All < %dx of "
				       "saturation, dec ATIME to %d",
				       TCS_GAIN_ADJUST_FACTOR,
				       sat_p->atime);
			} else {
				ALSLOG(SATURATION, "All < 90%% "
					"saturation, can't adjust "
					"(%d, %d)",
					sat_p->again, sat_p->atime);
			}
		} else if (sat_p->atime > TCS_MIN_ATIME) {
			/* increase ATIME */
			percent_left = (max_val * 100 / TCS_GAIN_SAT_LEVEL);
			percent_left = TSC_SATURATION_LOW_BAND_PERCENT -
					percent_left;
#if TCS_USE_LUX_TABLE
			decrement_atime(sat_p, max_val, percent_left);
#else
			decrement_atime(sat_p);
#endif
			ALSLOG(SATURATION, "All < 90%% saturation, dec "
			       "ATIME to %d", sat_p->atime);
		} else if (sat_p->again < TCS_MAX_AGAIN) {
			/*
			 * Although we're not at maximum gain yet, we
			 * can't just increase gain because a 4x change
			 * in gain under these light conditions would
			 * saturate on the next sample.  What we can do
			 * is to adjust atime to reduce sensitivity so
			 * that we may increase gain without saturation.
			 * This combination effectively acts as a half
			 * gain increase (2x estimate) instead of a full
			 * gain increase of 4x that would result in
			 * saturation.
			 */
			if (max_val < TCS_GAIN_SAT_UPSHIFT_LEVEL) {
				sat_p->atime = TCS_GAIN_UPSHIFT_ATIME;
				sat_p->again++;
				ALSLOG(GRAPH, "increment AGAIN = %d",
				       sat_p->again);
				ALSLOG(SATURATION, "All < 90%% saturation, "
				       "upshift case (%d, %d)",
				       sat_p->again, sat_p->atime);
			} else {
				ALSLOG(SATURATION, "All < 90%% saturation, "
					"can't adjust (%d, %d)",
					sat_p->again, sat_p->atime);
			}
		} else {
			ALSLOG(SATURATION, "All < 90%% saturation, "
			       "can't adjust (%d, %d)",
			       sat_p->again, sat_p->atime);
		}
	} else {
		ALSLOG(SATURATION, "saturation sweet spot (0x%08x < "
		       "0x%08x, sat = 0x%08x)", crgb_data[0],
		       TCS_SATURATION_LEVEL, TCS_SATURATION_LEVEL * 100 /
		       TSC_SATURATION_LOW_BAND_PERCENT);
	}

	/* If atime or gain setting changed, update atime and gain registers */
	if (save_again != sat_p->again) {
		ret = tcs3400_i2c_write8(s, TCS_I2C_CONTROL,
				(sat_p->again & TCS_I2C_CONTROL_MASK));
		if (ret)
			return ret;
		ALSLOG(SATURATION, "Set AGAIN = 0x%02x", sat_p->again);
	}

	if (save_atime != sat_p->atime) {
		ret = tcs3400_i2c_write8(s, TCS_I2C_ATIME, sat_p->atime);
		if (ret)
			return ret;
		ALSLOG(SATURATION, "Set ATIME = 0x%02x", sat_p->atime);
	}

	return ret;
}
#endif

/**
 * normalize_channel_data - normalize the light data to remove effect of
 * different atime and again settings from the sample.
 */
static uint32_t normalize_channel_data(struct motion_sensor_t *s,
					uint32_t  sample)
{
	struct tcs_saturation_t *sat_p =
				&(TCS3400_RGB_DRV_DATA(s+1)->saturation);
	/* index of value = AGAIN register setting (eg. AGAIN=2 for 16x) */
	const uint16_t tcs3400_agains[] = { 1, 4, 16, 64 };
	const uint16_t cur_gain = tcs3400_agains[sat_p->again];
	const uint16_t cal_again = tcs3400_agains[TCS_CALIBRATION_AGAIN];

	ALSLOG(SCALING, "%d = (%d * %d * %d) / ( (%d - %d) * %d )",
		(uint32_t) DIV_ROUND_NEAREST(sample * (TCS_ATIME_GRANULARITY -
			TCS_CALIBRATION_ATIME) * cal_again,
			(TCS_ATIME_GRANULARITY - sat_p->atime) * cur_gain),
			sample, TCS_ATIME_GRANULARITY - TCS_CALIBRATION_ATIME,
			cal_again, TCS_ATIME_GRANULARITY, sat_p->atime,
			cur_gain);

	ALSLOG(SCALING, "%d (0x%08x) normalized to %d (0x%08x) "
	       "(again:%d atime:%d)", (uint32_t) sample, (uint32_t) sample,
		(uint32_t) DIV_ROUND_NEAREST(sample *
				(TCS_ATIME_GRANULARITY -
				TCS_CALIBRATION_ATIME) * cal_again,
				(TCS_ATIME_GRANULARITY - sat_p->atime) *
				cur_gain),
				(uint32_t) sample,
				sat_p->again, sat_p->atime);

	return (uint32_t) DIV_ROUND_NEAREST(sample * (TCS_ATIME_GRANULARITY -
				TCS_CALIBRATION_ATIME) * cal_again,
				(TCS_ATIME_GRANULARITY - sat_p->atime) *
				cur_gain);
}


static void tcs3400_translate_to_xyz(struct motion_sensor_t *s,
				     int32_t *crgb_data, int32_t *xyz_data)
{
	struct tcs3400_rgb_drv_data_t *rgb_drv_data = TCS3400_RGB_DRV_DATA(s+1);
	int32_t crgb_prime[4];
	int32_t ir;
	int i;

	/* IR removal */
	ir = (crgb_data[1] + crgb_data[2] + crgb_data[3] - crgb_data[0]) / 2;

	ALSLOG(XYZ_XLATE, "IR = %d  ((%d + %d + %d - %d) / 2)", ir,
			crgb_data[1], crgb_data[2], crgb_data[3], crgb_data[0]);

	for (i = 0; i < ARRAY_SIZE(crgb_prime); i++) {
		if (crgb_data[i] < ir) {
			ALSLOG(ERRORS, "ERROR - IR > crgb_data[i] (0x%08x > "
				"0x%08x)", ir, crgb_data[i]+ir);
			crgb_prime[i] = 0;
		} else {
			crgb_prime[i] = crgb_data[i] - ir;
		}
	}

	/* if CC == 0, set BC = 0 */
	if (crgb_prime[CLEAR_CRGB_IDX] == 0)
		crgb_prime[BLUE_CRGB_IDX] = 0;

	/* regression fit to XYZ space */
	for (i = 0; i < 3; i++) {
		const struct rgb_calibration_t *p = &rgb_drv_data->rgb_cal[i];

		ALSLOG(XYZ_XLATE, "coeff[%d] = [ %s%d.%04d, %s%d.%04d, "
			"%s%d.%04d, %s%d.%04d ", i,
			negative_fp(p->coeff[RED_CRGB_IDX]) ?  "-" : "",
			ones_from_fp(p->coeff[RED_CRGB_IDX]),
			remainder_from_fp(p->coeff[RED_CRGB_IDX]),
			negative_fp(p->coeff[GREEN_CRGB_IDX]) ?  "-" : "",
			ones_from_fp(p->coeff[GREEN_CRGB_IDX]),
			remainder_from_fp(p->coeff[GREEN_CRGB_IDX]),
			negative_fp(p->coeff[BLUE_CRGB_IDX]) ?  "-" : "",
			ones_from_fp(p->coeff[BLUE_CRGB_IDX]),
			remainder_from_fp(p->coeff[BLUE_CRGB_IDX]),
			negative_fp(p->coeff[CLEAR_CRGB_IDX]) ?  "-" : "",
			ones_from_fp(p->coeff[CLEAR_CRGB_IDX]),
			remainder_from_fp(p->coeff[CLEAR_CRGB_IDX]));

		xyz_data[i] = p->offset + (fp_inter_t) FP_TO_INT(
			(fp_inter_t)p->coeff[RED_CRGB_IDX] *
					crgb_prime[RED_CRGB_IDX] +
			(fp_inter_t)p->coeff[GREEN_CRGB_IDX] *
					crgb_prime[GREEN_CRGB_IDX] +
			(fp_inter_t)p->coeff[BLUE_CRGB_IDX] *
					crgb_prime[BLUE_CRGB_IDX] +
			(fp_inter_t)p->coeff[CLEAR_CRGB_IDX] *
					crgb_prime[CLEAR_CRGB_IDX]);

		ALSLOG(XYZ_XLATE, "xyz_data[%d] = %d + (%d * %s%d.%04d) + "
			"(%d * %s%d.%04d) + (%d * %s%d.%04d) + "
			"(%d * %s%d.%04d)", i, p->offset,
			crgb_prime[RED_CRGB_IDX],
			negative_fp(p->coeff[RED_CRGB_IDX]) ?  "-" : "",
			ones_from_fp(p->coeff[RED_CRGB_IDX]),
			remainder_from_fp(p->coeff[RED_CRGB_IDX]),
			crgb_prime[GREEN_CRGB_IDX],
			negative_fp(p->coeff[GREEN_CRGB_IDX]) ?  "-" : "",
			ones_from_fp(p->coeff[GREEN_CRGB_IDX]),
			remainder_from_fp(p->coeff[GREEN_CRGB_IDX]),
			crgb_prime[BLUE_CRGB_IDX],
			negative_fp(p->coeff[BLUE_CRGB_IDX]) ?  "-" : "",
			ones_from_fp(p->coeff[BLUE_CRGB_IDX]),
			remainder_from_fp(p->coeff[BLUE_CRGB_IDX]),
			crgb_prime[CLEAR_CRGB_IDX],
			negative_fp(p->coeff[CLEAR_CRGB_IDX]) ?  "-" : "",
			ones_from_fp(p->coeff[CLEAR_CRGB_IDX]),
			remainder_from_fp(p->coeff[CLEAR_CRGB_IDX]));

		ALSLOG(XYZ_XLATE, "xyz_data[%d] = %d (0x%08x)", i,
			xyz_data[i], xyz_data[i]);

		if (xyz_data[i] < 0) {
			ALSLOG(ERRORS, "ERROR - xyz_data[i] 0%08x negative, "
				"setting to 0", xyz_data[i]);
			xyz_data[i] = 0;
		}
	}
}

static void tcs3400_process_raw_data(struct motion_sensor_t *s,
				    uint8_t *raw_data_buf,
				    uint16_t *raw_light_data, int32_t *xyz_data)
{
	struct als_drv_data_t *als_drv_data = TCS3400_DRV_DATA(s);
	struct tcs3400_rgb_drv_data_t *rgb_drv_data = TCS3400_RGB_DRV_DATA(s+1);
	const uint8_t calibration_mode = rgb_drv_data->calibration_mode;
	uint16_t  k_channel_scale =
			als_drv_data->als_cal.channel_scale.k_channel_scale;
	uint16_t cover_scale = als_drv_data->als_cal.channel_scale.cover_scale;
	int32_t crgb_data[CRGB_COUNT];
	int i;

	/* adjust for calibration and scale data */
	for (i = 0; i < TCS_CHANNEL_COUNT; i++) {
		int index = i * 2;

		/* assemble the light value for this channel */
		crgb_data[i] = raw_light_data[i] =
			((raw_data_buf[index+1] << 8) | raw_data_buf[index]);
		ALSLOG((SCALING), "raw: crgb_data[%d] = %d (0x%04x)",
		       i, crgb_data[i], crgb_data[i]);

		/* in calibration mode, we only assemble the raw data */
		if (calibration_mode)
			continue;

		/* rgb data at index 1, 2, and 3 owned by rgb driver, not ALS */
		if (i > 0) {
			k_channel_scale =
				rgb_drv_data->rgb_scale[i-1].k_channel_scale;
			cover_scale = rgb_drv_data->rgb_scale[i-1].cover_scale;
		}
		ALSLOG(SCALING, "chan %d: k_scale = %s%d.%d"
				" cover_scale = %s%d.%d", i,
				negative_fp(k_channel_scale) ?  "-" : "",
				ones_from_fp(k_channel_scale),
				remainder_from_fp(k_channel_scale),
				negative_fp(cover_scale) ?  "-" : "",
				ones_from_fp(cover_scale),
				remainder_from_fp(cover_scale));

		/* Step 1: divide by individual channel scale value */
		ALSLOG(SCALING, "kscale: crgb_data[%d] = %d (%d / %s%d.%d)", i,
				(uint32_t) fp_div_dbz(crgb_data[i],
							k_channel_scale),
				crgb_data[i],
				negative_fp(k_channel_scale) ?  "-" : "",
				ones_from_fp(k_channel_scale),
				remainder_from_fp(k_channel_scale));

		crgb_data[i] = SENSOR_APPLY_DIV_SCALE(crgb_data[i],
							k_channel_scale);

		/* compensate for the light cover */
		ALSLOG(SCALING, "cover: crgb_data[%d] = %d (%d * %s%d.%d)", i,
			fp_mul(cover_scale, crgb_data[i]),
			(uint32_t) crgb_data[i],
			negative_fp(cover_scale) ?  "-" : "",
			ones_from_fp(cover_scale),
			remainder_from_fp(cover_scale));

		crgb_data[i] = SENSOR_APPLY_SCALE(crgb_data[i], cover_scale);

		/* normalize the data for atime and again changes */
		crgb_data[i] = normalize_channel_data(s,
						      (uint32_t) crgb_data[i]);
		ALSLOG(SCALING, "Normalize: now %d", (uint32_t) crgb_data[i]);
	}

	ALSLOG(SCALING, "crgb = [ %d (0x%04x), %d (0x%04x), %d (0x%04x), %d "
			"(0x%04x) ",
			crgb_data[0], crgb_data[1], crgb_data[2], crgb_data[3],
			crgb_data[0], crgb_data[1], crgb_data[2], crgb_data[3]);

	if ((calibration_mode == TCS_RUN_MODE) &&
		IS_ENABLED(CONFIG_RGB_XYZ_TRANSLATE)) {
		/* we're not in calibration mode & we want xyz translation */
		tcs3400_translate_to_xyz(s, crgb_data, xyz_data);
		ALSLOG(SCALING, "xyz xlate to [ 0x%04x, 0x%04x, 0x%04x ",
			xyz_data[0], xyz_data[1], xyz_data[2]);
	} else {
		/* calibration mode returns raw data */
		for (i = 0; i < 3; i++)
			xyz_data[i] = crgb_data[i+1];
	}
}

static int tcs3400_post_events(struct motion_sensor_t *s, uint32_t last_ts)
{
	/*
	 * Rule says RGB sensor is right after ALS sensor.
	 * This routine will only get called from ALS sensor driver.
	 */
	struct motion_sensor_t *rgb_s = s + 1;
	const uint8_t calibration_mode =
			TCS3400_RGB_DRV_DATA(rgb_s)->calibration_mode;
	struct ec_response_motion_sensor_data vector;
	uint8_t buf[TCS_RGBC_DATA_SIZE]; /* holds raw data read from chip */
	int32_t xyz_data[3] = { 0, 0, 0 };
	uint16_t raw_data[4]; /* holds raw CRGB data assembled from buf[] */
	int retries = 20;     /* 400 ms max */
	int *last_v = s->raw_xyz;
	int32_t data = 0;
	int i, ret = EC_SUCCESS;

	/* Make sure data is valid */
	do {
		ret = tcs3400_i2c_read8(s, TCS_I2C_STATUS, &data);
		if (ret)
			return ret;
		if (!(data & TCS_I2C_STATUS_RGBC_VALID)) {
			retries--;
			if (retries == 0) {
				CPRINTS("RGBC not valid (0x%x)", data);
				return EC_ERROR_UNCHANGED;
			}
			msleep(20);
		}
	} while (!(data & TCS_I2C_STATUS_RGBC_VALID));

	/* Read the light registers */
	ret = i2c_read_block(s->port, s->i2c_spi_addr_flags,
			TCS_DATA_START_LOCATION,
			buf, sizeof(buf));
	if (ret)
		return ret;

	/* Process the raw light data, adjusting for scale and calibration */
	tcs3400_process_raw_data(s, buf, raw_data, xyz_data);

	ALSLOG((GRAPH|RAW_DATA), "RAW VALUES : "
		"0x%04x 0x%04x 0x%04x 0x%04x [%d, %d",
		raw_data[0], raw_data[1], raw_data[2], raw_data[3],
		TCS3400_RGB_DRV_DATA(s+1)->saturation.again,
		TCS3400_RGB_DRV_DATA(s+1)->saturation.atime);

	/* if clear channel data changed, send illuminance upstream */
	if ((raw_data[CLEAR_CRGB_IDX] != TCS_SATURATION_LEVEL) &&
	    (last_v[X] != xyz_data[Y])) {
		if (calibration_mode == TCS_CAL_MODE)
			last_v[X] = raw_data[CLEAR_CRGB_IDX];
		else
			last_v[X] = xyz_data[Y];
		vector.flags = 0;
		vector.data[X] = last_v[X];
		vector.data[Y] = last_v[Y] = 0;
		vector.data[Z] = last_v[Z] = 0;

#ifdef CONFIG_ACCEL_SPOOF_MODE
		/* If in spoof mode, replace actual data with our fake data */
		if (s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE) {
			for (i = 0; i < 3; i++)
				vector.data[i] = last_v[i] = s->spoof_xyz[i];
		}
#endif /* CONFIG_ACCEL_SPOOF_MODE */

#ifdef CONFIG_ACCEL_FIFO
		vector.sensor_num = s - motion_sensors;
		motion_sense_fifo_stage_data(&vector, s, 3, last_ts);
		if (calibration_mode)
			ALSLOG(PRIORITY, "Sending raw clear data [ 0x%04x ",
			       (unsigned short) vector.data[X]);
		else
			ALSLOG(PRIORITY, "Sending CLEAR data LUX = %d [%d, %d",
				(unsigned short) vector.data[X],
				TCS3400_RGB_DRV_DATA(s+1)->saturation.again,
				TCS3400_RGB_DRV_DATA(s+1)->saturation.atime);

#endif
	} else {
		if (raw_data[CLEAR_CRGB_IDX] != TCS_SATURATION_LEVEL) {
			ALSLOG(FLOW, "Clear channel data unchanged [ 0x%04x ",
			       (unsigned short) xyz_data[Y]);
		} else {
			ALSLOG((FLOW|GRAPH), "Clear channel data saturated "
			       "[ 0x%04x ",
			       (unsigned short) raw_data[CLEAR_CRGB_IDX]);
		}
	}

	/*
	 * If rgb channel data changed since last sample and didn't saturate,
	 * send it upstream
	 */
	last_v = rgb_s->raw_xyz;
	if (((last_v[X] != xyz_data[X]) || (last_v[Y] != xyz_data[Y]) ||
		(last_v[Z] != xyz_data[Z])) &&
		((raw_data[RED_CRGB_IDX] != TCS_SATURATION_LEVEL) &&
		(raw_data[BLUE_CRGB_IDX] != TCS_SATURATION_LEVEL) &&
		(raw_data[GREEN_CRGB_IDX] != TCS_SATURATION_LEVEL))) {
		vector.flags = 0;
		if (calibration_mode == TCS_CAL_MODE) {
			memcpy(vector.data, &raw_data[RED_CRGB_IDX],
			       sizeof(vector.data));
			memcpy(rgb_s->raw_xyz, &raw_data[RED_CRGB_IDX],
			       sizeof(vector.data));
		} else {
			for (i = 0; i < 3; i++)
				vector.data[i] = last_v[i] = xyz_data[i];
		}
#ifdef CONFIG_ACCEL_SPOOF_MODE
		if (rgb_s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE) {
			for (i = 0; i < 3; i++) {
				vector.data[i] = last_v[i] =
						rgb_s->spoof_xyz[i];
			}
		}
#endif /* CONFIG_ACCEL_SPOOF_MODE */

#ifdef CONFIG_ACCEL_FIFO
		ALSLOG(PRIORITY, "Sending %s data [ 0x%04x 0x%04x 0x%04x "
			"[%d, %d", calibration_mode ? "raw RGB" : "RGB XYZ",
			(unsigned short) vector.data[X],
			(unsigned short) vector.data[Y],
			(unsigned short) vector.data[Z],
			TCS3400_RGB_DRV_DATA(s+1)->saturation.again,
			TCS3400_RGB_DRV_DATA(s+1)->saturation.atime);
		vector.sensor_num = rgb_s - motion_sensors;
		motion_sense_fifo_stage_data(&vector, rgb_s, 3, last_ts);
#endif
	} else if ((raw_data[RED_CRGB_IDX] != TCS_SATURATION_LEVEL) &&
		(raw_data[BLUE_CRGB_IDX] != TCS_SATURATION_LEVEL) &&
		(raw_data[GREEN_CRGB_IDX] != TCS_SATURATION_LEVEL)) {
		ALSLOG(FLOW, "RGB channel unchanged [ 0x%04x 0x%04x 0x%04x ] "
			"[%d,%d", (unsigned short) xyz_data[X],
			(unsigned short) xyz_data[Y],
			(unsigned short) xyz_data[Z],
			TCS3400_RGB_DRV_DATA(s+1)->saturation.again,
			TCS3400_RGB_DRV_DATA(s+1)->saturation.atime);
	} else {
		ALSLOG((FLOW|GRAPH), "RGB channel saturated "
		       "[ 0x%04x 0x%04x 0x%04x ] [%d,%d",
			raw_data[RED_CRGB_IDX], raw_data[GREEN_CRGB_IDX],
			raw_data[BLUE_CRGB_IDX],
			TCS3400_RGB_DRV_DATA(s+1)->saturation.again,
			TCS3400_RGB_DRV_DATA(s+1)->saturation.atime);
	}
#ifdef CONFIG_ACCEL_FIFO
	motion_sense_fifo_commit_data();
#endif

#ifdef TEST_MODE
	{
		int max_val = 0;
		int saturation = 0;
		uint32_t percentage;

		for (int i = 0; i < TCS_CHANNEL_COUNT; i++)
			max_val = MAX(max_val, raw_data[i]);

		percentage = max_val * 100 / TCS_SATURATION_LEVEL;

		log(max_val, xyz_data[Y],
		    &TCS3400_RGB_DRV_DATA(s+1)->saturation, raw_data);
;
		if ((raw_data[0] == 0xffff) ||
		    (raw_data[1] == 0xffff) ||
		    (raw_data[2] == 0xffff) ||
		    (raw_data[3] == 0xffff)) {
			saturation = 1;
		}
		next_test_setting(s, saturation, percentage, g_last_lux);
		g_last_lux = xyz_data[Y];
	}
#else
	if (calibration_mode == TCS_RUN_MODE)
		ret = tcs3400_adjust_sensor_for_saturation(s, raw_data);
#endif

	return ret;
}

void tcs3400_interrupt(enum gpio_signal signal)
{
#ifdef CONFIG_ACCEL_FIFO
	last_interrupt_timestamp = __hw_clock_source_read();
#endif
	task_set_event(TASK_ID_MOTIONSENSE,
		       CONFIG_ALS_TCS3400_INT_EVENT, 0);
}

/*
 * tcs3400_irq_handler - bottom half of the interrupt stack.
 * Ran from the motion_sense task, finds the events that raised the interrupt,
 * and posts those events via motion_sense_fifo_stage_data()..
 *
 * This routine will get called for the TCS3400 ALS driver, but NOT for the
 * RGB driver.  We harvest data for both drivers in this routine.  The RGB
 * driver is guaranteed to directly follow the ALS driver in the sensor list
 * (i.e rgb's motion_sensor_t structure can be found at (s+1) ).
 */
static int tcs3400_irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	int status = 0;
	int ret = EC_SUCCESS;

	if (!(*event & CONFIG_ALS_TCS3400_INT_EVENT))
		return EC_ERROR_NOT_HANDLED;

	ret = tcs3400_i2c_read8(s, TCS_I2C_STATUS, &status);
	if (ret)
		return ret;

	ALSLOG(DEBUG, "status=0x%x", status);

	/* Disable future interrupts */
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, TCS3400_MODE_IDLE);
	if (ret)
		return ret;

	if ((status & TCS_I2C_STATUS_RGBC_VALID) ||
			((status & TCS_I2C_STATUS_ALS_IRQ) &&
			(status & TCS_I2C_STATUS_ALS_VALID)) ||
			IS_ENABLED(CONFIG_ALS_TCS3400_EMULATED_IRQ_EVENT)) {
		ret = tcs3400_post_events(s, last_interrupt_timestamp);
		if (ret)
			return ret;
	}

	tcs3400_i2c_write8(s, TCS_I2C_AICLEAR, 0);

	/* Disable ADC and turn off internal oscillator */
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, TCS3400_MODE_SUSPEND);
	if (ret)
		return ret;

	return ret;
}

static int tcs3400_rgb_get_range(const struct motion_sensor_t *s)
{
	/* Currently, calibration info is same for all channels */
	return (TCS3400_RGB_DRV_DATA(s)->device_scale << 16) |
			TCS3400_RGB_DRV_DATA(s)->device_uscale;
}

static int tcs3400_rgb_set_range(const struct motion_sensor_t *s,
				 int range,
				 int rnd)
{
	TCS3400_RGB_DRV_DATA(s)->device_scale = range >> 16;
	TCS3400_RGB_DRV_DATA(s)->device_uscale = range & 0xffff;
	return EC_SUCCESS;
}

static int tcs3400_rgb_get_scale(const struct motion_sensor_t *s,
				 uint16_t *scale,
				 int16_t *temp)
{
	struct rgb_calibration_t *rgb_cal = TCS3400_RGB_DRV_DATA(s)->rgb_cal;

	scale[X] = rgb_cal[RED_RGB_IDX].scale.k_channel_scale;
	scale[Y] = rgb_cal[GREEN_RGB_IDX].scale.k_channel_scale;
	scale[Z] = rgb_cal[BLUE_RGB_IDX].scale.k_channel_scale;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int tcs3400_rgb_set_scale(const struct motion_sensor_t *s,
				 const uint16_t *scale,
				 int16_t temp)
{
	struct rgb_calibration_t *rgb_cal = TCS3400_RGB_DRV_DATA(s)->rgb_cal;

	rgb_cal[RED_RGB_IDX].scale.k_channel_scale = scale[X];
	rgb_cal[GREEN_RGB_IDX].scale.k_channel_scale = scale[Y];
	rgb_cal[BLUE_RGB_IDX].scale.k_channel_scale = scale[Z];
	return EC_SUCCESS;
}

static int tcs3400_rgb_get_offset(const struct motion_sensor_t *s,
				  int16_t *offset,
				  int16_t *temp)
{
	offset[X] = TCS3400_RGB_DRV_DATA(s)->rgb_cal[X].offset;
	offset[Y] = TCS3400_RGB_DRV_DATA(s)->rgb_cal[Y].offset;
	offset[Z] = TCS3400_RGB_DRV_DATA(s)->rgb_cal[Z].offset;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int tcs3400_rgb_set_offset(const struct motion_sensor_t *s,
				  const int16_t *offset,
				  int16_t temp)
{
	TCS3400_RGB_DRV_DATA(s)->rgb_cal[X].offset = offset[X];
	TCS3400_RGB_DRV_DATA(s)->rgb_cal[Y].offset = offset[Y];
	TCS3400_RGB_DRV_DATA(s)->rgb_cal[Z].offset = offset[Z];
	return EC_SUCCESS;
}

static int tcs3400_rgb_get_data_rate(const struct motion_sensor_t *s)
{
	return TCS3400_RGB_DRV_DATA(s)->rate;
}

static int tcs3400_rgb_set_data_rate(const struct motion_sensor_t *s,
				     int rate,
				     int rnd)
{
	TCS3400_RGB_DRV_DATA(s)->rate = rate;
	return EC_SUCCESS;
}

/* Enable/disable special factory calibration mode */
static int tcs3400_rgb_perform_calib(const struct motion_sensor_t *s,
				     int enable)
{
	int mode = (enable) ? TCS_CAL_MODE : TCS_RUN_MODE;

	TCS3400_RGB_DRV_DATA(s+1)->calibration_mode = mode;
	return EC_SUCCESS;
}

static int tcs3400_get_range(const struct motion_sensor_t *s)
{
	return (TCS3400_DRV_DATA(s)->als_cal.scale << 16) |
			(TCS3400_DRV_DATA(s)->als_cal.uscale);
}

static int tcs3400_set_range(const struct motion_sensor_t *s,
			     int range,
			     int rnd)
{
	TCS3400_DRV_DATA(s)->als_cal.scale = range >> 16;
	TCS3400_DRV_DATA(s)->als_cal.uscale = range & 0xffff;
	return EC_SUCCESS;
}

static int tcs3400_get_scale(const struct motion_sensor_t *s,
				 uint16_t *scale,
				 int16_t *temp)
{
	scale[X] = TCS3400_DRV_DATA(s)->als_cal.channel_scale.k_channel_scale;
	scale[Y] = 0;
	scale[Z] = 0;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int tcs3400_set_scale(const struct motion_sensor_t *s,
				 const uint16_t *scale,
				 int16_t temp)
{
	TCS3400_DRV_DATA(s)->als_cal.channel_scale.k_channel_scale = scale[X];
	return EC_SUCCESS;
}

static int tcs3400_get_offset(const struct motion_sensor_t *s,
			      int16_t *offset,
			      int16_t *temp)
{
	offset[X] = TCS3400_DRV_DATA(s)->als_cal.offset;
	offset[Y] = 0;
	offset[Z] = 0;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int tcs3400_set_offset(const struct motion_sensor_t *s,
			      const int16_t *offset,
			      int16_t temp)
{
	TCS3400_DRV_DATA(s)->als_cal.offset = offset[X];
	return EC_SUCCESS;
}

static int tcs3400_get_data_rate(const struct motion_sensor_t *s)
{
	return TCS3400_DRV_DATA(s)->rate;
}

static int tcs3400_set_data_rate(const struct motion_sensor_t *s,
				 int rate,
				 int rnd)
{
	enum tcs3400_mode mode;
	int data;
	int ret;

	if (rate == 0) {
		/* Suspend driver */
		mode = TCS3400_MODE_SUSPEND;
	} else {
		/*
		 * We set the sensor for continuous mode,
		 * integrating over 800ms.
		 * Do not allow range higher than 1Hz.
		 */
		if (rate > 1000)
			rate = 1000;
		mode = TCS3400_MODE_COLLECTING;
	}
	TCS3400_DRV_DATA(s)->rate = rate;

	ret = tcs3400_i2c_read8(s, TCS_I2C_ENABLE, &data);
	if (ret)
		return ret;

	data = (data & TCS_I2C_ENABLE_MASK) | mode;
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, data);

	return ret;
}

/**
 * Initialise TCS3400 light sensor.
 */
static int tcs3400_rgb_init(const struct motion_sensor_t *s)
{
	return sensor_init_done(s);
}

static int tcs3400_init(const struct motion_sensor_t *s)
{
	/*
	 * These are default power-on register values with two exceptions:
	 * Set ATIME = 0 (712 ms)
	 * Set AGAIN = 16 (0x10)  (AGAIN is in CONTROL register)
	 */
	struct reg_data {
		uint8_t reg;
		uint8_t data;
	} defaults[] = {
		{ TCS_I2C_ENABLE, 0 },
		{ TCS_I2C_ATIME, TCS_DEFAULT_ATIME },
		{ TCS_I2C_WTIME, 0xFF },
		{ TCS_I2C_AILTL, 0 },
		{ TCS_I2C_AILTH, 0 },
		{ TCS_I2C_AIHTL, 0 },
		{ TCS_I2C_AIHTH, 0 },
		{ TCS_I2C_PERS, 0 },
		{ TCS_I2C_CONFIG, 0x40 },
		{ TCS_I2C_CONTROL, (TCS_DEFAULT_AGAIN & TCS_I2C_CONTROL_MASK) },
		{ TCS_I2C_AUX, 0 },
		{ TCS_I2C_IR, 0 },
		{ TCS_I2C_CICLEAR, 0 },
		{ TCS_I2C_AICLEAR, 0 }
	};
	int data = 0;
	int ret;

#ifdef TEST_MODE
	defaults[1].data = TCS_MAX_ATIME;
	defaults[9].data = TCS_MIN_AGAIN;
#endif

	ret = tcs3400_i2c_read8(s, TCS_I2C_ID, &data);
	if (ret) {
		CPRINTS("failed reading ID reg 0x%x, ret=%d", TCS_I2C_ID, ret);
		return ret;
	}
	if ((data != TCS340015_DEVICE_ID) && (data != TCS340037_DEVICE_ID)) {
		CPRINTS("no ID match, data = 0x%x", data);
		return EC_ERROR_ACCESS_DENIED;
	}

	/* reset chip to default power-on settings, changes ATIME and CONTROL */
	for (int x = 0; x < ARRAY_SIZE(defaults); x++) {
		ret = tcs3400_i2c_write8(s, defaults[x].reg, defaults[x].data);
		if (ret)
			return ret;
	}

	return sensor_init_done(s);
}

const struct accelgyro_drv tcs3400_drv = {
	.init = tcs3400_init,
	.read = tcs3400_read,
	.set_range = tcs3400_set_range,
	.get_range = tcs3400_get_range,
	.set_offset = tcs3400_set_offset,
	.get_offset = tcs3400_get_offset,
	.set_scale = tcs3400_set_scale,
	.get_scale = tcs3400_get_scale,
	.set_data_rate = tcs3400_set_data_rate,
	.get_data_rate = tcs3400_get_data_rate,
	.perform_calib = tcs3400_rgb_perform_calib,
#ifdef CONFIG_ACCEL_INTERRUPTS
	.irq_handler = tcs3400_irq_handler,
#endif
};

const struct accelgyro_drv tcs3400_rgb_drv = {
	.init = tcs3400_rgb_init,
	.read = tcs3400_rgb_read,
	.set_range = tcs3400_rgb_set_range,
	.get_range = tcs3400_rgb_get_range,
	.set_offset = tcs3400_rgb_set_offset,
	.get_offset = tcs3400_rgb_get_offset,
	.set_scale = tcs3400_rgb_set_scale,
	.get_scale = tcs3400_rgb_get_scale,
	.set_data_rate = tcs3400_rgb_set_data_rate,
	.get_data_rate = tcs3400_rgb_get_data_rate,
};

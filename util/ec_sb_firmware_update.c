/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lock/gec_lock.h"
#include "comm-host.h"
#include "misc_util.h"
#include "ec_sb_firmware_update.h"
#include <unistd.h>

#define DEBUG_EC_SB_FW_UPDATE 0

#if DEBUG_EC_SB_FW_UPDATE
#define DPRINTF(fmt, ...) \
	printf("FW Update: " fmt, ## __VA_ARGS__)
#else
#define DPRINTF(fmt, ...)
#endif

/** Simplo Battery: Required 10 seconds delay for 1st 10 block write
 * unit in seconds
 */
int delay_x_us = 9000000;

/** Simplo Battery: Additional delays are required after each 32-byte write
 *  unit in useconds
 */
int delay_y_us = 40000;

static void print_battery_firmware_image_hdr(
	struct smart_battery_fw_header *hdr)
{
	printf("%c%c%c%c hdr_ver:%04X major_minor:%04X\n",
		hdr->signature[0],
		hdr->signature[1],
		hdr->signature[2],
		hdr->signature[3],
		hdr->hdr_version, hdr->pkg_version_major_minor);

	printf("vendor_id:%04X battery_type:%04X fw_ver:%04X tbl_ver:%04X\n",
		hdr->vendor_id, hdr->battery_type, hdr->fw_version,
		hdr->data_table_version);

	printf("bin off:%08X size:%08X chk_sum:%02X\n",
		hdr->fw_binary_offset, hdr->fw_binary_size, hdr->checksum);
}

static void ec_sb_fw_update_print_info(struct ec_sb_fw_update_info *p)
{
	printf("\ninfo state:0x%X fw_id:0x%X\n",
		p->hdr.state,
		p->hdr.fw_id);
	printf("maker_id:0x%X hw_id:0x%X fw_ver:0x%X d_ver:0x%X\n",
		p->info.maker_id,
		p->info.hardware_id,
		p->info.fw_version,
		p->info.data_version);
	return;
}

static void ec_sb_fw_update_print_status(struct ec_sb_fw_update_status *p)
{
	printf("\nstatus state:0x%X fw_id:0x%X sz:%d\n",
		p->hdr.state,
		p->hdr.fw_id, (int)sizeof(struct ec_sb_fw_update_status));
	printf("f_maker_id:%d f_hw_id:%d f_fw_ver:%d f_permnent:%d\n",
		p->status.v_fail_maker_id,
		p->status.v_fail_hw_id,
		p->status.v_fail_fw_version,
		p->status.v_fail_permanent);
	printf("permanent failure:%d abnormal:%d fw_update:%d\n",
		p->status.permanent_failure,
		p->status.abnormal_condition,
		p->status.fw_update_supported);
	printf("fw_update_mode:%d fw_corrupted:%d cmd_reject:%d\n",
		p->status.fw_update_mode,
		p->status.fw_corrupted,
		p->status.cmd_reject);
	printf("invliad data:%d fw_fatal_err:%d fec_err:%d busy:%d\n",
		p->status.invalid_data,
		p->status.fw_fatal_error,
		p->status.fec_error,
		p->status.busy);
	printf("\n");
	return;
}

/* @return 1 (True) if img signature is valid */
static int check_battery_firmware_image_signature(
	struct smart_battery_fw_header *hdr)
{
	return (hdr->signature[0] == 'B') &&
		(hdr->signature[1] == 'T') &&
		(hdr->signature[2] == 'F') &&
		(hdr->signature[3] == 'W');
}

/* @return 1 (True) if img checksum is valid. */
static int check_battery_firmware_image_checksum(
	struct smart_battery_fw_header *hdr)
{
	int i;
	uint8_t sum = 0;
	uint8_t *img = (uint8_t *)hdr;
	img += hdr->fw_binary_offset;
	for (i = 0; i < hdr->fw_binary_size; i++)
		sum += img[i];
	sum += hdr->checksum;
	return sum == 0;
}

/* @return 1 (True) if img versions are ok to update. */
static int check_battery_firmware_image_version(
	struct smart_battery_fw_header *hdr,
	struct ec_sb_fw_update_info *p)
{
	return (((hdr->fw_version == 0xFFFF)
			|| (hdr->fw_version > p->info.fw_version)) &&
		((hdr->data_table_version == 0xFFFF)
			|| (hdr->data_table_version > p->info.data_version)));
}


static int check_battery_firmware_ids(
	struct smart_battery_fw_header *hdr,
	struct ec_sb_fw_update_info *p)
{
	return ((hdr->vendor_id == p->info.maker_id) &&
		(hdr->battery_type == p->info.hardware_id));
}

/* ec_sb_check_if_need_update_fw
 * @return 1 (true) if need; 0 (false) if not.
 */
int ec_sb_check_if_need_update_fw(
		struct smart_battery_fw_header *hdr,
		struct ec_sb_fw_update_info *info)
{
	return check_battery_firmware_image_signature(hdr)

	&& check_battery_firmware_ids(hdr, info)

	&& check_battery_firmware_image_version(hdr, info)

	&& check_battery_firmware_image_checksum(hdr);
}

int ec_sb_fw_get_status(struct ec_sb_fw_update_status *status)
{
	int rv = EC_RES_SUCCESS;
	int i = 0;
	struct ec_sb_fw_update_header *hdr =
		(struct ec_sb_fw_update_header *)ec_outbuf;

	hdr->state = EC_CMD_SB_FW_UPDATE_STATUS;
	do {
		DPRINTF("cmd.0x35 get status.%d\n", i);
		rv = ec_command(EC_CMD_SB_FW_UPDATE, 0,
			hdr, sizeof(*hdr), status, sizeof(*status));
	} while ((rv < 0) && (i++ < 3));

	if (rv < 0) {
		fprintf(stderr,
			"Firmware Update Get Status Error\n");
		return -EC_RES_ERROR;
	}

	return EC_RES_SUCCESS;
}

int ec_sb_fw_get_info(struct ec_sb_fw_update_info *info)
{
	int rv = EC_RES_SUCCESS;
	struct ec_sb_fw_update_header *hdr =
		(struct ec_sb_fw_update_header *)ec_outbuf;

	hdr->state = EC_CMD_SB_FW_UPDATE_INFO;
	rv = ec_command(EC_CMD_SB_FW_UPDATE, 0,
		hdr, sizeof(*hdr), info, sizeof(*info));
	if (rv < 0) {
		fprintf(stderr,
			"Firmware Update Get Info Error\n");
		return -EC_RES_ERROR;
	}
#if DEBUG_EC_SB_FW_UPDATE
	printf("cmd:%X ok:%d\n", hdr->state, rv);
	ec_sb_fw_update_print_info(info);
#endif
	return EC_RES_SUCCESS;
}

int ec_sb_fw_update_subcmd(int state)
{
	int rv = EC_RES_SUCCESS;
	struct ec_sb_fw_update_header *hdr =
		(struct ec_sb_fw_update_header *)ec_outbuf;

	hdr->state = state;
	rv = ec_command(EC_CMD_SB_FW_UPDATE, 0,
		hdr, sizeof(*hdr), NULL, 0);
	if (rv < 0) {
		fprintf(stderr,
			"Firmware Update State:%d Error\n", state);
		return -EC_RES_ERROR;
	}
	DPRINTF("cmd:%X ok:%d\n", hdr->state, rv);
	return EC_RES_SUCCESS;
}

static void ec_sb_dump_data(uint8_t *data, int size)
{
	int i = 0;
	for (i = 0; i < size; i++) {
		if ((i%16) == 0)
			printf("\n");
		printf("%02X ", data[i]);
	}
	printf("\n");
}

int ec_sb_firmware_update(const char *fw_image_name)
{
	int rv = EC_RES_SUCCESS, i;
	int size;
	char *buf, *ptr;
	struct smart_battery_fw_header *fw_img_hdr;
	struct ec_sb_fw_update_status status;
	struct ec_sb_fw_update_info info;
	int err_retry_cnt = SB_FW_UPDATE_ERROR_RETRY_CNT;
	int fec_err_retry_cnt = SB_FW_UPDATE_FEC_ERROR_RETRY_CNT;
	int busy_retry_cnt = SB_FW_UPDATE_BUSY_ERROR_RETRY_CNT;

	struct ec_sb_fw_update_write_block *write =
		(struct ec_sb_fw_update_write_block *)ec_outbuf;

	int bsize, step_size = SB_FW_UPDATE_CMD_WRITE_BLOCK_SIZE;

	/* Read the input file */
	printf("\n\n==> Read File:%s\n", fw_image_name);

	buf = read_file(fw_image_name, &size);
	if (!buf) {
		fprintf(stderr,
			"Firmware Update: Load Firmware Image[%s] Error\n",
			fw_image_name);
		return -1;
	}
	ptr = buf;
	fw_img_hdr = (struct smart_battery_fw_header *)ptr;
	print_battery_firmware_image_hdr(fw_img_hdr);

	if (fw_img_hdr->fw_binary_offset >= size || size < 256) {
		fprintf(stderr,
			"Firmware Update: Load Firmware Image[%s] format Error\n",
			fw_image_name);
		return -1;
	}
step0:
	DPRINTF("== step0: size:%04X %d (%d %d %d)\n", size, busy_retry_cnt,
		(int)sizeof(struct ec_sb_fw_update_status),
		(int)sizeof(struct ec_sb_fw_update_info),
		(int)sizeof(struct ec_sb_fw_update_write_block));
	if (busy_retry_cnt == 0)
		goto error_return;

	busy_retry_cnt--;

	rv = ec_sb_fw_get_status(&status);
	if (rv)
		goto error_return;

	ec_sb_fw_update_print_status(&status);
	if (!((status.status.abnormal_condition == 0)
		&& (status.status.fw_update_supported == 1))) {
		fprintf(stderr, "Firmware Udpate is not supported!\n");
		goto error_return;
	}
	if (status.status.busy)
		goto step0;

step1:
	if (err_retry_cnt == 0)
		goto error_return;
	err_retry_cnt--;

	/*Step 1: cmd.0x37 Read Info */
	DPRINTF("cmd.0x37 read info: sz:%ld\n", sizeof(info));
	rv = ec_sb_fw_get_info(&info);
	if (rv)
		goto error_return;

	ec_sb_fw_update_print_info(&info);

	rv = ec_sb_fw_get_status(&status);
	if (rv)
		goto error_return;

#if DEBUG_EC_SB_FW_UPDATE
	if (check_battery_firmware_image_signature(fw_img_hdr))
		printf("img sig ok\n");

	if (check_battery_firmware_ids(fw_img_hdr, &info))
		printf("img IDs ok\n");

	if (check_battery_firmware_image_version(fw_img_hdr, &info))
		printf("img ver ok\n");

	if (check_battery_firmware_image_checksum(fw_img_hdr))
		printf("img chk sum ok\n");
#endif

	rv = ec_sb_check_if_need_update_fw(fw_img_hdr, &info);
	if (rv == 0) {
		printf("ERROR:Battery firmware is not valid to update!\n");
		ec_sb_fw_update_print_info(&info);
		print_battery_firmware_image_hdr(fw_img_hdr);
		rv = EC_RES_INVALID_PARAM;
		goto error_return;
	}
step2:
	/*Step 2: cmd.0x35 write word 0x1000 */
	printf("cmd.0x35 write word 0x1000\n");
	rv = ec_sb_fw_update_subcmd(EC_CMD_SB_FW_UPDATE_PREPARE);
	if (rv)
		goto error_return;
	sleep(1);

	rv = ec_sb_fw_get_status(&status);
	if (rv)
		goto error_return;

	/*Step 4: cmd.0x35 Write Word 0xF000 */
	printf("cmd.0x35 write word 0xF000\n");
	rv = ec_sb_fw_update_subcmd(EC_CMD_SB_FW_UPDATE_BEGIN);
	if (rv)
		goto error_return;

	printf("\nSleep 3 seconds after sending fw update begin\n");
	sleep(3);

	/*Step 5: cmd.0x35 Read Status */
	rv = ec_sb_fw_get_status(&status);
	if (rv)
		goto error_return;
	if (status.status.fw_update_mode == 0)
		goto step2;

	/* Write data in chunks */
	ptr += fw_img_hdr->fw_binary_offset;
	size -= fw_img_hdr->fw_binary_offset;
	printf("Write size 0x%X total_size:0x%X\n", step_size, size);
	for (i = 0; i < size; i += step_size) {
		bsize = MIN(size - i, step_size);
		memcpy(&write->data[0], ptr + i, bsize);
		write->offset = i;
step6:
#if DEBUG_EC_SB_FW_UPDATE
		printf("write offset:0x%X retry:%d\n", i, fec_err_retry_cnt);
		ec_sb_dump_data(write->data, bsize);
#else
		if ((i & 0x7FF) == 0x0C0)
			printf("\n%X\n", i);
		else
			printf(".");
#endif
		if (fec_err_retry_cnt == 0)
			goto error_return;
		fec_err_retry_cnt--;

		/*Step 6: Write block data, 32 bytes */
		write->hdr.state = EC_CMD_SB_FW_UPDATE_WRITE;
		rv = ec_command(EC_CMD_SB_FW_UPDATE, 0,
			write, sizeof(*write), NULL, 0);
		if (rv < 0) {
			fprintf(stderr,
			"Firmware Update Write Error offset@%d\n", i);
			goto error_return;
		}

		if (i <= step_size * 10) {
			printf("sleep %d useconds when i@%X\n", delay_x_us, i);
			usleep(delay_x_us);
		} else
			usleep(delay_y_us);

		/*Step 7: Check Return Status */
		do {
			rv = ec_sb_fw_get_status(&status);
			if (rv) {
				printf("Offset:%X smbus error:%X\n", i, rv);
				ec_sb_dump_data(write->data, bsize);
				ec_sb_fw_update_print_status(&status);
				goto error_return;
			}
		} while (status.status.busy);

#if DEBUG_EC_SB_FW_UPDATE
		if (*((uint16_t *)&status.status))
			ec_sb_fw_update_print_status(&status);
#endif

		if (status.status.fec_error) {
			printf("Offset:%X\n", i);
			ec_sb_dump_data(write->data, bsize);
			ec_sb_fw_update_print_status(&status);
			rv = EC_RES_ERROR;
			goto step6;
		}
		if (status.status.fw_fatal_error) {
			printf("Offset:%X\n", i);
			ec_sb_dump_data(write->data, bsize);
			ec_sb_fw_update_print_status(&status);
			rv = EC_RES_ERROR;
			goto step2;
		}
		if (status.status.permanent_failure ||
			status.status.v_fail_permanent) {
			printf("Offset:%X\n", i);
			ec_sb_dump_data(write->data, bsize);
			ec_sb_fw_update_print_status(&status);
			rv = EC_RES_ERROR;
			goto step8;
		}
		if (status.status.v_fail_maker_id ||
			status.status.v_fail_hw_id    ||
			status.status.v_fail_fw_version ||
			status.status.fw_corrupted   ||
			status.status.cmd_reject ||
			status.status.invalid_data) {
			printf("Offset:%X\n", i);
			ec_sb_dump_data(write->data, bsize);
			ec_sb_fw_update_print_status(&status);
			rv = EC_RES_ERROR;
			goto step1;
		}

		fec_err_retry_cnt = SB_FW_UPDATE_FEC_ERROR_RETRY_CNT;
	}

step8:
	rv = ec_sb_fw_update_subcmd(EC_CMD_SB_FW_UPDATE_END);
	if (rv) {
		printf("SB FW Update End Error\n");
		goto error_return;
	}

	/* Note: Sleep is required! */
	printf("\nSleep 1 seconds after the last FW Update End!\n");
	sleep(1);

/* Pull for completion */
step9:
	rv = ec_sb_fw_get_status(&status);
	if (rv) {
		printf("SB FW Update End get status Error\n");
		goto error_return;
	}
	if ((status.status.fw_update_mode == 1)
		|| (status.status.busy == 1))
		goto step9;

#if 0
	rv = ec_sb_fw_update_subcmd(EC_CMD_SB_FW_UPDATE_PROTECT);
#endif

error_return:

	free(buf);

	if (rv)
		printf("\n\n==> Firmware:%s Update Failed:%d\n",
			fw_image_name, rv);
	else
		printf("\n\n==> Firmware:%s Update Complete.\n",
			fw_image_name);

	return rv;
}

#define GEC_LOCK_TIMEOUT_SECS   30  /* 30 secs */

int main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <fw_filename>\n", argv[0]);
		return -1;
	}

	if (argc >= 3)
		delay_x_us = atoi(argv[2]);

	if (argc >= 4)
		delay_y_us = atoi(argv[3]);

	if (acquire_gec_lock(GEC_LOCK_TIMEOUT_SECS) < 0) {
		fprintf(stderr, "Could not acquire GEC lock.\n");
		exit(1);
	}

	if (comm_init()) {
		fprintf(stderr, "Couldn't find EC\n");
		goto out;
	}

	printf("fw_filename:%s\n", argv[1]);
	ec_sb_firmware_update(argv[1]);

out:
	release_gec_lock();
	return 0;
}

/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Smart battery Firmware Update driver.
 * Ref: Common Smart Battery System Interface Specification v8.0.
 *
 * cmd.0x35, Write Word
 *   0x1000: Prepare to Update
 *   0x2000: End of Update
 *   0xF000: Update Firmware
 *
 * cmd.0x35, Read Word
 *   Firmware Update Status
 *
 * cmd.0x36 Write Block
 *   Send 32 byte firmware image
 *
 * cmd.0x37 Read Word
 *   Get Battery Information
 *   sequence:=b1,b0,b3,b2,b5,b5,b7,b6
 *
 * Command Sequence for Battery FW Update
 *
 *  0. cmd.0x35.read
 *  1. cmd.0x37.read
 *  2. cmd.0x35.write.0x1000
 *  3. cmd.0x35.read.status (optional)
 *  4. cmd.0x35.write.0xF000
 *  5. cmd.0x35.read.status
 *     if bit8-0, go to step 2
 *  6. cmd.0x36.write.32byte
 *  7. cmd.0x35.read.status
 *     if FEC.b13=1, go to step 6
 *     if fatal.b12=1, go to step 2
 *     if b11,b10,b9,b2,b1,b0; go to step 1
 *     if b5,b3; go to step 8
 *    (repeat 6,7)
 *  8. cmd.0x36.write.0x2000
 *  9. cmd.0x35.read.status
 */

#ifndef __CROS_EC_SB_FIRMWARE_UPDATE_H__
#define __CROS_EC_SB_FIRMWARE_UPDATE_H__

struct smart_battery_fw_header {
	uint8_t signature[4]; /* "BTFW" */
	uint16_t hdr_version; /* 0x0100 */
	uint16_t pkg_version_major_minor;

	uint16_t vendor_id; /* 8,9 */
	uint16_t battery_type; /* A B */

	uint16_t fw_version; /* C D */
	uint16_t data_table_version; /* E F */
	uint32_t fw_binary_offset; /*0x10 0x11 0x12 0x13 */
	uint32_t fw_binary_size; /* 0x14 0x15 0x16 0x17 */
	uint8_t  checksum; /* 0x18 */
};

/* if permanent error:b5,b3; go to setp8 */
#define SB_FW_UPDATE_PERMANENT_ERROR_MASK 0x0028

/* if firmware update fatal error:b12; go to step2 */
#define SB_FW_UPDATE_FW_FATAL_ERROR_MASK    0x1000
#define SB_FW_UPDATE_FW_FATAL_ERROR_RETRY_CNT 1  /* Retry Error cnt*/

/* if error:b11,b10,b9 b2,b1,b0; go to step1 */
#define SB_FW_UPDATE_ERROR_MASK           0x0E07
#define SB_FW_UPDATE_ERROR_RETRY_CNT      1  /* Retry Error cnt*/

/* if FEC.b13=1, go to step6 */
#define SB_FW_UPDATE_FEC_ERROR_MASK       0x2000 /* b13 */
#define SB_FW_UPDATE_FEC_ERROR_RETRY_CNT  1  /* b13.FEC retry cnt*/

/* if busy; retry 10 times */
#define SB_FW_UPDATE_BUSY_ERROR_MASK      0x4000 /* b14 */
#define SB_FW_UPDATE_BUSY_ERROR_RETRY_CNT 1  /* b14.busy retry cnt*/

/**
 * Update Smart Battery Firmware
 *
 * @param fw_image_name  firmware image name
 *
 * @return 0 if success, negative if error.
 */
int ec_sb_firmware_update(const char *fw_image_name);

#endif

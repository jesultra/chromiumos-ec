/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#ifndef __CROS_EC_OTP_H
#define __CROS_EC_OTP_H


/*
 * Read an OTP bank
 *
 * CONFIG_OTP_SIZE bytes are read.
 *
 * @param bank          Bank to read.
 * @param size	        Number of bytes to read.
 * @param data          Destination buffer for data.  Must be 32-bit aligned.
 */
int otp_read(uint8_t bank, int size, char *data);

/*
 * Write an OTP bank
 *
 * @param bank          Bank to write.
 * @param size	        Number of bytes to write.
 * @param data          Destination buffer for data.  Must be 32-bit aligned.
 */
int otp_write(uint8_t bank, int size, const char *data);


/*
 * Check if an OTP bank is protected.
 *
 * @param bank          Bank to protect.
 * @return non-zero if that bank is read only.
 */
int otp_get_protect(uint8_t bank);

/*
 * Set a particular OTP banck as read only.
 *
 * @param bank          Bank to protect.
 */
int otp_set_protect(uint8_t bank);


#endif  /* __CROS_EC_OTP_H */


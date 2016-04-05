/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"

#ifdef CRYPTO_TEST_SETUP

#include "extension.h"

enum {
	TEST_ENCRYPT = 0,
	TEST_DECRYPT = 1,
};

#define MAX_OUT_BYTES 256

static void ecies_command_handler(void *cmd_body, size_t cmd_size,
			size_t *response_size)
{
	const uint8_t *cmd = cmd_body;
	uint8_t *out = cmd_body;

	uint8_t op;
	const uint8_t *in;
	size_t in_len;
	p256_int pub_x;
	size_t pub_x_len;
	p256_int pub_y;
	size_t pub_y_len;
	p256_int *d = &pub_x;
	const uint8_t *salt;
	size_t salt_len;
	const uint8_t *info;
	size_t info_len;

	/* Command format.
	 *
	 *   WIDTH	   FIELD
	 *   1		   OP
	 *   1             MSB IN LEN
	 *   1             LSB IN LEN
	 *   IN_LEN        IN
	 *   1             MSB PUB_X LEN
	 *   1             LSB PUB_X LEN
	 *   PUB_X_LEN     PUB_X
	 *   1             MSB PUB_Y LEN
	 *   1             LSB PUB_Y LEN
	 *   PUB_Y_LEN     PUB_Y
	 *   1             MSB SALT LEN
	 *   1             LSB SALT LEN
	 *   SALT_LEN      SALT
	 *   1             MSB INFO LEN
	 *   1             LSB INFO LEN
	 *   INFO_LEN      INFO
	 */

	op = *cmd++;
	in_len = ((uint16_t) (cmd[0] << 8)) | cmd[1];
	cmd += 2;
	in = cmd;
	cmd += in_len;

	pub_x_len = ((uint16_t) (cmd[0] << 8)) | cmd[1];
	cmd += 2;
	p256_from_bin(cmd, &pub_x);
	cmd += pub_x_len;

	pub_y_len = ((uint16_t) (cmd[0] << 8)) | cmd[1];
	cmd += 2;
	if (pub_y_len)
		p256_from_bin(cmd, &pub_y);
	cmd += pub_y_len;

	salt_len = ((uint16_t) (cmd[0] << 8)) | cmd[1];
	cmd += 2;
	salt = cmd;
	cmd += salt_len;

	info_len = ((uint16_t) (cmd[0] << 8)) | cmd[1];
	cmd += 2;
	info = cmd;
	cmd += info_len;

	switch (op) {
	case TEST_ENCRYPT:
		*response_size = DCRYPTO_ecies_encrypt(
			out, MAX_OUT_BYTES, in, in_len,
			&pub_x, &pub_y, salt, salt_len,
			info, info_len);
		break;
	case TEST_DECRYPT:
		*response_size = DCRYPTO_ecies_decrypt(
			out, MAX_OUT_BYTES, in, in_len,
			d, salt, salt_len,
			info, info_len);
		break;
	default:
		*response_size = 0;
	}
}

DECLARE_EXTENSION_COMMAND(EXTENSION_ECIES, ecies_command_handler);

#endif   /* CRYPTO_TEST_SETUP */


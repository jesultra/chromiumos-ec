/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "internal.h"
#include "dcrypto.h"

#include "trng.h"

#define AES_KEY_BYTES  16
#define HMAC_KEY_BYTES 32

#define AES_BLOCK_BYTES 16

/* P256 based hybrid encryption.  The output format is:
 *
 *   0x04 | PUBKEY | IV | AES128_CTR(PLAINTEXT) | HMAC_SHA256(IV || CIPHERTEXT)
 */
size_t DCRYPTO_ecies_encrypt(
	void *out, size_t out_len, const void *in, size_t in_len,
	const p256_int *pub_x, const p256_int *pub_y,
	const uint8_t *salt, size_t salt_len,
	const uint8_t *info, size_t info_len)
{
	p256_int eph_d;
	p256_int eph_x;
	p256_int eph_y;
	uint8_t seed[P256_NBYTES];
	p256_int secret_x;
	p256_int secret_y;
	/* Key bytes to be extracted from HKDF. */
	uint8_t key[AES_KEY_BYTES + HMAC_KEY_BYTES];
	const uint8_t *aes_key;
	const uint8_t *hmac_key;
	uint8_t iv[AES_BLOCK_BYTES];
	struct HMAC_CTX ctx;
	uint8_t *outp = out;

	if (out_len < 1 + P256_NBYTES + P256_NBYTES + sizeof(iv) +
		in_len + SHA256_DIGEST_BYTES)
		return 0;

	/* Generate emphemeral EC key. */
	rand_bytes(seed, sizeof(seed));
	if (!DCRYPTO_p256_key_from_bytes(&eph_x, &eph_y, &eph_d, seed))
		return 0;
	/* Compute DH point. */
	if (!DCRYPTO_p256_points_mul(&secret_x, &secret_y, NULL,
					&eph_d, pub_x, pub_y))
		return 0;
	/* Check for computational errors. */
	if (!DCRYPTO_p256_valid_point(&secret_x, &secret_y))
		return 0;
	/* Convert secret to big-endian. */
	reverse(&secret_x, sizeof(secret_x));
	/* Derive shared secret. */
	if (!DCRYPTO_hkdf(key, sizeof(key), salt, salt_len,
				(uint8_t *) &secret_x, sizeof(secret_x),
				info, info_len))
		return 0;
	aes_key = &key[0];
	hmac_key = &key[AES_KEY_BYTES];

	/* Leave room for EC public key and IV. */
	outp++;
	outp += P256_NBYTES;
	outp += P256_NBYTES;
	outp += sizeof(iv);

	/* Write ciphertext. */
	rand_bytes(iv, sizeof(iv));
	if (!DCRYPTO_aes_ctr(outp, aes_key, AES_KEY_BYTES * 8, iv,
				in, in_len))
		return 0;

	outp = out;
	*outp++ = 0x04;  /* uncompressed EC public key. */
	p256_to_bin(&eph_x, outp);
	outp += P256_NBYTES;
	p256_to_bin(&eph_y, outp);
	outp += P256_NBYTES;
	memcpy(outp, iv, sizeof(iv));

	/* Calculate HMAC(iv || ciphertext). */
	dcrypto_HMAC_SHA256_init(&ctx, hmac_key, HMAC_KEY_BYTES);
	dcrypto_HMAC_update(&ctx, outp, sizeof(iv) + in_len);
	outp += sizeof(iv) + in_len;
	memcpy(outp, dcrypto_HMAC_final(&ctx), SHA256_DIGEST_BYTES);
	outp += SHA256_DIGEST_BYTES;

	return outp - (uint8_t *) out;
}

size_t DCRYPTO_ecies_decrypt(
	void *out, size_t out_len, const void *in, size_t in_len,
	const p256_int *d,
	const uint8_t *salt, size_t salt_len,
	const uint8_t *info, size_t info_len)
{
	p256_int eph_x;
	p256_int eph_y;
	p256_int secret_x;
	p256_int secret_y;
	uint8_t key[AES_KEY_BYTES + HMAC_KEY_BYTES];
	const uint8_t *aes_key;
	const uint8_t *hmac_key;
	uint8_t iv[AES_BLOCK_BYTES];
	struct HMAC_CTX ctx;
	const uint8_t *inp = in;
	uint8_t *outp = out;

	if (in_len < 1 + P256_NBYTES + P256_NBYTES + sizeof(iv) +
		SHA256_DIGEST_BYTES)
		return 0;
	if (inp[0] != 0x04)
		return 0;

	in_len -= 1 + P256_NBYTES + P256_NBYTES + sizeof(iv) +
		SHA256_DIGEST_BYTES;

	inp++;
	p256_from_bin(inp, &eph_x);
	inp += P256_NBYTES;
	p256_from_bin(inp, &eph_y);
	inp += P256_NBYTES;

	/* Verify that the public point is on the curve. */
	if (!DCRYPTO_p256_valid_point(&eph_x, &eph_y))
		return 0;
	/* Compute the DH point. */
	if (!DCRYPTO_p256_points_mul(&secret_x, &secret_y, NULL,
					d, &eph_x, &eph_y))
		return 0;
	/* Check for computational errors. */
	if (!DCRYPTO_p256_valid_point(&secret_x, &secret_y))
		return 0;
	/* Convert secret to big-endian. */
	reverse(&secret_x, sizeof(secret_x));
	/* Derive shared secret. */
	if (!DCRYPTO_hkdf(key, sizeof(key), salt, salt_len,
				(uint8_t *) &secret_x, sizeof(secret_x),
				info, info_len))
		return 0;

	aes_key = &key[0];
	hmac_key = &key[AES_KEY_BYTES];
	dcrypto_HMAC_SHA256_init(&ctx, hmac_key, HMAC_KEY_BYTES);
	dcrypto_HMAC_update(&ctx, inp, sizeof(iv) + in_len);
	/* TODO(ngm): replace with constant time verify. */
	if (memcmp(inp + sizeof(iv) + in_len, dcrypto_HMAC_final(&ctx),
			SHA256_DIGEST_BYTES) != 0)
		return 0;

	memcpy(iv, inp, sizeof(iv));
	inp += sizeof(iv);
	if (!DCRYPTO_aes_ctr(outp, aes_key, AES_KEY_BYTES * 8, iv,
				inp, in_len))
		return 0;
	return in_len;
}

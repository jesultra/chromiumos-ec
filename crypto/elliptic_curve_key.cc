/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "crypto/elliptic_curve_key.h"
#include "openssl/ec_key.h"
#include "openssl/mem.h"
#include "openssl/obj_mac.h"

#include <bitset>
#include <cstddef>
#include <cstdint>

template <size_t Size, size_t BlockSize> struct memory_block {
	static constexpr size_t size = Size;
	static constexpr size_t block_size = BlockSize;
	std::bitset<Size> used;
	uint8_t block[Size][BlockSize];
};

static memory_block<128, 64> small_bucket;
static memory_block<16, 512> medium_bucket;
static memory_block<8, 1024> big_bucket;

template <typename Bucket> void *bucket_alloc(Bucket &bucket, size_t size)
{
	if (size > Bucket::block_size) {
		return nullptr;
	}
	for (size_t i = 0; i < Bucket::size; i++) {
		if (!bucket.used[i]) {
			bucket.used[i] = true;
			return bucket.block[i];
		}
	}
	return nullptr;
}

template <typename Bucket> void bucket_free(Bucket &bucket, void *ptr)
{
	uint8_t *address = static_cast<uint8_t *>(ptr);
	uint8_t *bucket_begin = &bucket.block[0][0];
	if (address < bucket_begin ||
	    address >= bucket_begin + sizeof(bucket.block)) {
		return;
	}

	int i = (address - bucket_begin) / Bucket::block_size;
	bucket.used[i] = false;
}

template <typename Bucket> bool is_this_bucket(Bucket &bucket, void *ptr)
{
	uint8_t *address = static_cast<uint8_t *>(ptr);
	uint8_t *bucket_begin = &bucket.block[0][0];
	if (address < bucket_begin ||
	    address >= bucket_begin + sizeof(bucket.block)) {
		return true;
	}
	return false;
}

extern "C" void *OPENSSL_memory_alloc(size_t size)
{
	void *result = nullptr;
	result = bucket_alloc(small_bucket, size);
	if (result)
		return result;
	result = bucket_alloc(medium_bucket, size);
	if (result)
		return result;
	result = bucket_alloc(big_bucket, size);
	if (result)
		return result;
	return result;
}

extern "C" void OPENSSL_memory_free(void *ptr)
{
	bucket_free(small_bucket, ptr);
	bucket_free(medium_bucket, ptr);
	bucket_free(big_bucket, ptr);
}

extern "C" size_t OPENSSL_memory_get_size(void *ptr)
{
	if (is_this_bucket(small_bucket, ptr))
		return decltype(small_bucket)::block_size;
	if (is_this_bucket(medium_bucket, ptr))
		return decltype(medium_bucket)::block_size;
	if (is_this_bucket(big_bucket, ptr))
		return decltype(big_bucket)::block_size;
	return 0;
}

bssl::UniquePtr<EC_KEY> generate_elliptic_curve_key()
{
	bssl::UniquePtr<EC_KEY> key(
		EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
	if (key == nullptr) {
		return nullptr;
	}

	if (EC_KEY_generate_key(key.get()) != 1) {
		return nullptr;
	}

	return key;
}

/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Fuzzer for the TPM2 and vendor specific Cr50 commands.
 */

#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <vector>

#include "src/libfuzzer/libfuzzer_macro.h"
#include "src/mutator.h"

#include "fuzz/PinweaverModel.h"
#include "fuzz/cr50.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef assert
#undef assert
#endif

#include "fuzz_config.h"
#include "nvmem.h"
#include "nvmem_vars.h"
#include "persistence.h"
#include "pinweaver.h"

#define NVMEM_TPM_SIZE ((sizeof((struct nvmem_partition *)0)->buffer) \
      - NVMEM_CR50_SIZE)

uint32_t nvmem_user_sizes[NVMEM_NUM_USERS] = {
  NVMEM_TPM_SIZE,
  NVMEM_CR50_SIZE
};

const char *__get_prog_name(void) {
  return __FILE__;
}

static uint8_t buffer_[PW_MAX_MESSAGE_SIZE];
PinweaverModel pinweaver_;

int flash_pre_init(void);
void timer_init(void);

void rand_bytes(void *buffer, size_t len) {
  size_t x = 0;
  for (; x < len; ++x) {
    ((uint8_t *)buffer)[x] = rand();
  }
}

void get_storage_seed(void *buf, size_t *len) {
  memset(buf, 0x77, *len);
}

#ifdef __cplusplus
}
#endif

const int STATIC_INITIALIZATION = ([]() -> int {
    remove_persistent_storage("flash");
    flash_pre_init();
    return 0;
})();

void apply_random_bytes(const fuzz::RandomBytes& random_bytes) {
  const auto& value = random_bytes.value();
  if (value.size() >= ARRAY_SIZE(buffer_)) {
    memcpy(buffer_, value.data(), ARRAY_SIZE(buffer_));
  } else {
    memcpy(buffer_, value.data(), value.size());
    memset(buffer_ + value.size(), 0, ARRAY_SIZE(buffer_) - value.size());
  }
}

DEFINE_PROTO_FUZZER(const fuzz::FuzzerInput& input) {
  memset(__host_flash, 0xff, sizeof(__host_flash));
  //system_pre_init();
  //system_common_pre_init();
  timer_init();

  srand(0);
  memset(buffer_, 0, sizeof(buffer_));
  pinweaver_.Reset();
  for (const fuzz::SubAction& action : input.sub_actions()) {
    switch(action.sub_action_case()) {
      case fuzz::SubAction::kRandomBytes:
        apply_random_bytes(action.random_bytes());
        pinweaver_.SendBuffer(buffer_);
        break;
      case fuzz::SubAction::kPinweaver:
        pinweaver_.ApplyPinweaver(action.pinweaver(), buffer_);
        break;
      case fuzz::SubAction::SUB_ACTION_NOT_SET:
        break;
    }
  }
}

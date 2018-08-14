/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Emulator self-reboot procedure */

#include <string.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "host_test.h"
#include "reboot.h"
#include "test_util.h"

/* This allows fuzzers to handle reboot events as exceptions rather than
 * executing the binary again. This is necessary for oss-fuzz integration
 * because it provides its own main function that links against a function the
 * fuzzer target provides.
 */
void emulator_reboot(void) {
  throw "reboot";
}

#ifdef __cplusplus
}
#endif

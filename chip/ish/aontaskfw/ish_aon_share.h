/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ISH_AON_SHARE_H
#define __CROS_EC_ISH_AON_SHARE_H

#include "ia_structs.h"

#define PAGE_SIZE 4096

// macros for manipulating 32bit bitfields.
//macro aggressively casts all parameters. use carefully
#define DW_CHECK_BIT(n, b)                                                     \
	(((uint32_t)(n) & ((uint32_t)(1 << (b)))) == ((uint32_t)(1 << (b))))
#define DW_SET_BIT(n, b) ((n) |= (1 << ((uint32_t)(b))))
#define DW_CLEAR_BIT(n, b) ((n) &= (~(1 << ((uint32_t)(b)))))

#define AON_MAGIC_LINEAR_ADRS 0xFFFFF000

typedef struct {
	tss_t *tss_ptr;
	uint32_t ldt_ptr;
	uint32_t ldt_size;
	int pm_state;
	idt_ptr_t main_fw_idt_ptr;
} __attribute__((packed)) ish_aon_share_t;

#endif /* __CROS_EC_ISH_AON_SHARE_H */

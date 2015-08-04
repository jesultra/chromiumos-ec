/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Atomic operations for Andes */

#ifndef __CROS_EC_ATOMIC_H
#define __CROS_EC_ATOMIC_H

#include "common.h"
#include "cpu.h"
#include "task.h"

static inline void atomic_clear(uint32_t volatile *addr, uint32_t bits)
{
	interrupt_disable();
	*addr &= ~bits;
	interrupt_enable();
}

static inline void atomic_or(uint32_t volatile *addr, uint32_t bits)
{
	interrupt_disable();
	*addr |= bits;
	interrupt_enable();
}

static inline void atomic_add(uint32_t volatile *addr, uint32_t value)
{
	interrupt_disable();
	*addr += value;
	interrupt_enable();
}

static inline void atomic_sub(uint32_t volatile *addr, uint32_t value)
{
	interrupt_disable();
	*addr -= value;
	interrupt_enable();
}

static inline uint32_t atomic_read_clear(uint32_t volatile *addr)
{
	uint32_t val;
	interrupt_disable();
	val = *addr;
	*addr = 0;
	interrupt_enable();
	return val;
}
#endif  /* __CROS_EC_ATOMIC_H */

/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _COMMON_CMSIS_DAP__H_
#define _COMMON_CMSIS_DAP__H_

#include "queue.h"

/*
 * Methods to be implemented by board code:
 */

/* Set the JTAG clock speed. */
int cmsis_dap_set_period(uint32_t new_clock_hz);
/* Busy-wait half a JTAG clock cycle. */
void cmsis_dap_half_clock_delay(void);

/* Enable output on pins, including any level-shifters or buffers. */
void cmsis_dap_enable_jtag_pins(void);
/* Restore pins to state prior to JTAG connection. */
void cmsis_dap_disable_jtag_pins(void);

/*
 * Declaration of handlers of CMSIS-DAP vendor extension commands, implemented
 * in other files besides cmsis-dap.c .
 */
void dap_goog_i2c(size_t peek_c);
void dap_goog_i2c_device(size_t peek_c);
void dap_goog_gpio(size_t peek_c);

/*
 * Methods and variables provided by common code:
 */

extern struct queue const cmsis_dap_tx_queue;
extern struct queue const cmsis_dap_rx_queue;

/* Reset JTAG state to power on defaults. */
void cmsis_dap_reinit(void);

/*
 * If this function returns true, it means that the currently executing
 * handler function on the CMSIS-DAP task must abort and return as soon as
 * possible.
 */
bool cmsis_dap_unwind_requested(void);

/*
 * Routines to be used in the CMSIS-DAP task to add/remove a possibly large
 * number of bytes from the USB queues.  These functions can block waiting for
 * the host computer, and will not return until the given number of bytes has
 * been transferred, except if cmsis_dap_unwind_requested() returns true.
 */
void queue_blocking_add(struct queue const *q, const void *src, size_t count);
void queue_blocking_remove(struct queue const *q, void *dest, size_t count);
void queue_blocking_discard(struct queue const *q, size_t count);

#endif

/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USBC PD Default Policies  */

#ifndef __CROS_EC_USBC_PD_POLICY_H
#define __CROS_EC_USBC_PD_POLICY_H

#include "usb_pe_sm.h"

/**
 * Port Discovery Swap Policy
 *
 * Different boards can implement its own swap policy during a port discovery
 * by implementing this function.
 *
 * @param port USB-C port number
 * @param dr   current port data role
 * @param *flags  pointer to Policy Engine flags
 * @param dr_swap_flag   Data Role Swap Flag bit
 * @param vconn_swap_to_on_flag Vconn Swap to On Flag bit
 * @param return True if a state transition occurred, elsf False
 */
__override_proto bool port_discovery_swap_policy(int port, enum pd_data_role dr,
	uint32_t *flags, uint32_t dr_swap_flag, uint32_t vconn_swap_on_flag);

#endif /* __CROS_EC_USBC_PD_POLICY_H */


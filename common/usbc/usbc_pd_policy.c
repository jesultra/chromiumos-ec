/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "usb_pe_sm.h"
#include "usb_tc_sm.h"
/*
 * Default Port Discovery Swap Policy.
 *
 * 1) If dr_swap_to_dfp_flag == true and port data role is UFP,
 *    transition to pe_drs_send_swap
 * 2) If vconn_swap_to_on_flag == true and vconn is currently off,
 *    transition to pe_vcs_send_swap
 */
__overridable bool port_discovery_swap_policy(int port,
		enum pd_data_role dr, uint32_t *flags, uint32_t dr_swap_flag,
		uint32_t vconn_swap_on_flag)
{
	if ((*flags & dr_swap_flag) && dr == PD_ROLE_UFP) {
		/* Clear the dr_swap_flag bit */
		*flags &= ~dr_swap_flag;
		/* Transition to pe_drs_send_swap */
		pe_transition_to_pe_drs_send_swap(port);
		return true;
	}

	if (IS_ENABLED(CONFIG_USBC_VCONN) &&
		(*flags & vconn_swap_on_flag) && !tc_is_vconn_src(port)) {
		/* Clear the vconn_swap_on_flag bit */
		*flags &= ~vconn_swap_on_flag;
		/* Transition to pe_vcs_send_swap */
		pe_transition_to_pe_vcs_send_swap(port);
		return true;
	}

	/* Transition did not occur */
	return false;
}

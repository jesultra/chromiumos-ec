/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_POWER_MGT_H
#define __CROS_EC_POWER_MGT_H

/* power states for ISH */
enum { ISH_PM_STATE_D0 = 0, /* D0 state */
       ISH_PM_STATE_D0I0, /* sleep state: halt*/
       ISH_PM_STATE_D0I1, /* deep sleep state 1: TCG, halt*/
       ISH_PM_STATE_D0I2, /* deep sleep state 2: TCG, SRAM retention, halt*/
       ISH_PM_STATE_D0I3, /* deep sleep state 3: TCG, SRAM power off, halt*/
       ISH_PM_STATE_D3, /* D3 state */
       ISH_PM_STATE_RESET_PREP,
       ISH_PM_STATE_NUM };

static inline void ish_halt(void)
{
	/* make sure interrupts are enabled before halting */
	__asm__ volatile("sti;\n\t"
			 "hlt;\n\t");
}

void ish_pm_init(void);

#endif /* __CROS_EC_POWER_MGT_H */

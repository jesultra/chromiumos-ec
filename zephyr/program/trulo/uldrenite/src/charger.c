/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charger.h"
#include "driver/charger/bq257x0_regs.h"
#include "extpower.h"
#include "hooks.h"
#include "usb_pd.h"

static void bq25728_current_limit_own_by_iin_dpm(void)
{
	int reg;

	i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
		   BQ25710_REG_CHARGE_OPTION_2, &reg);
	reg = reg & (~BIT(7));
	i2c_write16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
		    BQ25710_REG_CHARGE_OPTION_2, reg);
}

static void bq25728_current_limit_own_by_ilim_hiz(void)
{
	int reg;

	i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
		   BQ25710_REG_CHARGE_OPTION_2, &reg);
	reg = reg | BIT(7);
	i2c_write16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
		    BQ25710_REG_CHARGE_OPTION_2, reg);
}

static void bq25728_input_current_own_change(void)
{
	if (extpower_is_present())
		bq25728_current_limit_own_by_iin_dpm();
	else
		bq25728_current_limit_own_by_ilim_hiz();
}
DECLARE_HOOK(HOOK_AC_CHANGE, bq25728_input_current_own_change,
	     HOOK_PRIO_DEFAULT);

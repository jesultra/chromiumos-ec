/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>

#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "util.h"

struct usb_pd_bist_shared_fixture {
	struct tcpci_partner_data sink_5v_500ma;
	struct tcpci_snk_emul_data snk_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

static void *usb_pd_bist_shared_setup(void)
{
	static struct usb_pd_bist_shared_fixture test_fixture;

	/* Get references for the emulators */
	test_fixture.tcpci_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(tcpci_emul)));
	test_fixture.charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));

	return &test_fixture;
}

static void usb_pd_bist_shared_before(void *data)
{
	struct usb_pd_bist_shared_fixture *test_fixture = data;

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	/* Initialized the sink to request 5V and 500mA */
	tcpci_partner_init(&test_fixture->sink_5v_500ma, PD_REV30);
	test_fixture->sink_5v_500ma.extensions = tcpci_snk_emul_init(
		&test_fixture->snk_ext, &test_fixture->sink_5v_500ma, NULL);
	test_fixture->snk_ext.pdo[0] = PDO_FIXED(5000, 500, 0);

	connect_sink_to_port(&test_fixture->sink_5v_500ma,
			     test_fixture->tcpci_emul,
			     test_fixture->charger_emul);
}

static void usb_pd_bist_shared_after(void *data)
{
	struct usb_pd_bist_shared_fixture *fixture = data;

	disconnect_sink_from_port(fixture->tcpci_emul);
}

ZTEST_SUITE(usb_pd_bist_shared, drivers_predicate_post_main,
	    usb_pd_bist_shared_setup, usb_pd_bist_shared_before,
	    usb_pd_bist_shared_after, NULL);

ZTEST_F(usb_pd_bist_shared, verify_bist_shared_mode)
{
	uint32_t bist_data;
	uint32_t f5v_cap;

	/*
	 * Verify we were offered the 1.5A source cap because of our low current
	 * needs initially
	 */
	f5v_cap = fixture->snk_ext.last_5v_source_cap;
	/* Capability should be 5V fixed, 1.5 A */
	zassert_equal((f5v_cap & PDO_TYPE_MASK), PDO_TYPE_FIXED,
		      "PDO type wrong");
	zassert_equal(PDO_FIXED_VOLTAGE(f5v_cap), 5000, "PDO voltage wrong");
	zassert_equal(PDO_FIXED_CURRENT(f5v_cap), 1500,
		      "PDO initial current wrong");

	/* Start up BIST shared test mode */
	bist_data = BDO(BDO_MODE_SHARED_ENTER, 0);
	zassume_ok(tcpci_partner_send_data_msg(&fixture->sink_5v_500ma,
					       PD_DATA_BIST, &bist_data, 1, 0),
		   "Failed to send BIST enter message");

	/* The DUT has tBISTSharedTestMode (1 second) to offer us 3A now */
	k_sleep(K_SECONDS(1));

	f5v_cap = fixture->snk_ext.last_5v_source_cap;
	/* Capability should be 5V fixed, 3.0 A */
	zassert_equal((f5v_cap & PDO_TYPE_MASK), PDO_TYPE_FIXED,
		      "PDO type wrong");
	zassert_equal(PDO_FIXED_VOLTAGE(f5v_cap), 5000, "PDO voltage wrong");
	zassert_equal(PDO_FIXED_CURRENT(f5v_cap), 3000,
		      "PDO current didn't increase in BIST mode");

	/* Leave BIST shared test mode */
	bist_data = BDO(BDO_MODE_SHARED_EXIT, 0);
	zassume_ok(tcpci_partner_send_data_msg(&fixture->sink_5v_500ma,
					       PD_DATA_BIST, &bist_data, 1, 0),
		   "Failed to send BIST exit message");

	/*
	 * The DUT may now execute ErrorRecovery or simply send a new
	 * Source_Cap.  Either way, we should go back to 1.5 A
	 */
	k_sleep(K_SECONDS(5));

	f5v_cap = fixture->snk_ext.last_5v_source_cap;
	/* Capability should be 5V fixed, 1.5 A */
	zassert_equal((f5v_cap & PDO_TYPE_MASK), PDO_TYPE_FIXED,
		      "PDO type wrong");
	zassert_equal(PDO_FIXED_VOLTAGE(f5v_cap), 5000, "PDO voltage wrong");
	zassert_equal(PDO_FIXED_CURRENT(f5v_cap), 1500,
		      "PDO current didn't decrease after BIST exit");
}

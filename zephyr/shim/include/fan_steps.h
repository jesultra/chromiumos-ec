/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#error "This file must only be included from config_chip.h and it should be" \
	"included in all zephyr builds automatically"
#endif


#define NODE_ID_AND_COMMA(node_id) node_id,

enum fan_steps {
#if DT_NODE_EXISTS(DT_INST(0, cros_ec_fan_steps))
	DT_FOREACH_CHILD(DT_INST(0, cros_ec_fan_steps), NODE_ID_AND_COMMA)
#endif /* cros_ec_fan_steps */
	FAN_STEPS_COUNT,
};

#undef NODE_ID_AND_COMMA
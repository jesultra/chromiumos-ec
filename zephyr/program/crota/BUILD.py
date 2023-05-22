# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for crota."""


def register_npcx9_variant(
    project_name, extra_dts_overlays=(), extra_kconfig_files=()
):
    """Register a variant of a crota, even though this is not named as such."""
    return register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9m3f",
        dts_overlays=[
            "adc.dts",
            "battery.dts",
            "fan.dts",
            "gpio.dts",
            "i2c.dts",
            "interrupts.dts",
            "keyboard.dts",
            "motionsense.dts",
            "pwm_leds.dts",
            "temp_sensors.dts",
            "usbc.dts",
            # Project-specific DTS customization.
            *extra_dts_overlays,
        ],
        kconfig_files=[
            # Common to all projects.
            here / "prj.conf",
            # Project-specific KConfig customization.
            *extra_kconfig_files,
        ],
        inherited_from=["crota"],
    )


crota = register_npcx9_variant(
    project_name="crota",
    extra_dts_overlays=[here / "crota.dts"],
    extra_kconfig_files=[here / "prj_crota.conf"],
)

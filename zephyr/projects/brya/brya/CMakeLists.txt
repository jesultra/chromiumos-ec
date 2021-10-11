# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(brya)

set(PLATFORM_EC_DRIVER_TCPM "${PLATFORM_EC}/driver/tcpm" CACHE PATH
    "Path to the platform/ec driver/tcpm directory")
set(PLATFORM_EC_BOARD "${PLATFORM_EC}/board/brya" CACHE PATH
    "Path to the platform/ec board directory")
set(PLATFORM_EC_BASEBOARD "${PLATFORM_EC}/baseboard/brya" CACHE PATH
    "Path to the platform/ec baseboard directory")

# Include board specific header files
zephyr_include_directories(
  include
  "${PLATFORM_EC_BASEBOARD}"
  "${PLATFORM_EC_BOARD}"
  "${PLATFORM_EC_DRIVER_TCPM}")

# Include selected EC source for the baseboard
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY
  "${PLATFORM_EC_BASEBOARD}/battery_presence.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CBI_EEPROM
  "${PLATFORM_EC_BASEBOARD}/cbi.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_POWER_DELIVERY
  "${PLATFORM_EC_BASEBOARD}/usb_pd_policy.c")

# Include selected EC source for the board
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY
  "${PLATFORM_EC_BOARD}/battery.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC
  "${PLATFORM_EC_BOARD}/usbc_config.c"
  "${PLATFORM_EC_BOARD}/fw_config.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGE_MANAGER
  "${PLATFORM_EC_BOARD}/charger.c"
  "${PLATFORM_EC}/common/math_util.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_NCT38XX
  "${PLATFORM_EC}/driver/tcpm/nct38xx.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_NX20P3483
  "${PLATFORM_EC}/driver/ppc/nx20p348x.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_IO_EXPANDER_NCT38XX
  "${PLATFORM_EC}/driver/ioexpander/ioexpander_nct38xx.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_IO_EXPANDER
  "${PLATFORM_EC}/common/ioexpander.c")

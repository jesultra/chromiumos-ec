# -*- makefile -*-
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32F302R8
CHIP:=lpc
CHIP_FAMILY:=lpc13
CHIP_VARIANT:=lpc1343

board-y=board.o cmsis-dap.o

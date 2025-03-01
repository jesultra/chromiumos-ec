# -*- makefile -*-
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32F072
CHIP:=stm32
CHIP_FAMILY:=stm32f0
CHIP_VARIANT:=stm32f07x

# For __builtin_parity and __aeabi_llsl
ifneq ($(CROSS_COMPILE_CC_NAME),clang)
LDFLAGS_EXTRA+=-static-libgcc -lgcc
endif

# These files are compiled into RO
chip-ro=bkpdata.o system.o

board-rw=board.o board_util.o gpio.o spi.o i2c.o \
	cmsis-dap.o

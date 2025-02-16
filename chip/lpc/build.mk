# -*- makefile -*-
# Copyright 2013 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# LPC chip specific files build
#

ifeq ($(CHIP_FAMILY),lpc13)
# LPC13xx chips have a Cortex-M3 ARM core
CORE:=cortex-m
# Force Cortex-M3 subset of instructions
CFLAGS_CPU+=-mcpu=cortex-m3
else
$(error Unknown chip family: $(CHIP_FAMILY))
endif

chip-$(CONFIG_COMMON_RUNTIME)+=system.o hwtimer32.o gpio.o uart.o clock.o
chip-$(CONFIG_USB)+=usb.o

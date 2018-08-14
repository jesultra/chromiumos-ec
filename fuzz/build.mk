# -*- makefile -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# fuzzer binaries
#

fuzz-list-y ?= cr50

cr50-y=cr50.o PinweaverModel.o

$(out)/RO/fuzz/cr50.o $(out)/RW/fuzz/cr50.o: $(out)/gen/fuzz/cr50.pb.h

$(out)/cr50.fuzz: ro-objs += $(out)/gen/fuzz/cr50.pb.o
$(out)/cr50.fuzz: LDFLAGS_EXTRA += -L$(out)/cryptoc -lcryptoc
$(out)/cr50.fuzz: $(out)/cryptoc/libcryptoc.a $(out)/gen/fuzz/cr50.pb.o

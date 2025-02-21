# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _ec_deps_impl(module_ctx):
    def _coreboot_sdk_subtool(arch, version, sha256, bucket = "chromiumos-sdk"):
        name = "ec-coreboot-sdk-%s" % arch
        toolchain_name = "coreboot-sdk-%s" % arch
        http_archive(
            name = name,
            build_file = "//platform/rules_cros_firmware/cros_firmware:BUILD.gcs_subtool",
            sha256 = sha256,
            url = "https://storage.googleapis.com/%s/toolchains/%s/%s.tar.zst" % (bucket, toolchain_name, version),
        )

    _coreboot_sdk_subtool(
        "nds32le-elf",
        "14.2.0-r3/a42da0080cd482f22b35afff7878a62b63b7f978",
        "fabb9604216a0a9c5b5602b9655b6cac2dbfd3d70a412daaafc90219158bfb56",
    )
    _coreboot_sdk_subtool(
        "i386-elf",
        "14.2.0-r3/5a612e612191e8fc1dcb246b31279c4fa6da2da8",
        "5400530a396891db1b9c9f3016aa2d51ff5d4bc7b1b2d08056aec2e06090f957",
    )
    _coreboot_sdk_subtool(
        "picolibc-i386-elf",
        "14.2.0-r1/432177501fd9307fd8d2a42cad50f5a391431342",
        "a11105e6b95e941a77abe7df0b70aee76acf18004747031128fe976be99408c6",
    )
    _coreboot_sdk_subtool(
        "libstdcxx-i386-elf",
        "14.2.0-r1/7669d6b5d20c58db775ae136aa17e46e74b2765d",
        "1901d9bef995685bef0293d099b55b90fb694fa3af180c74e91acf539c94647a",
    )
    _coreboot_sdk_subtool(
        "arm-eabi",
        "14.2.0-r3/a9e789245bfa4b88b07da734c4bcdcf8eb5348eb",
        "e86687e8e0bd3910912b4bcef5802047a141b4b1ed986499f69cfcf9bc4b6682",
    )
    _coreboot_sdk_subtool(
        "picolibc-arm-eabi",
        "14.2.0-r1/26d1f34f1bd1aff76f67bb17137e4cf13127d2d3",
        "cafd06b93427c3780db2020dbdad27521c394c20e933e201ace9ca7bb84cb140",
    )
    _coreboot_sdk_subtool(
        "libstdcxx-arm-eabi",
        "14.2.0-r1/8ef4441e966344160ccb6cca55a110928c9ff2b1",
        "b1cd7fa25f6dcd1bdccb5b87bffb1f87512bad09d9f7d41f6ad8014fdbdeb4dc",
    )
    _coreboot_sdk_subtool(
        "riscv-elf",
        "14.2.0-r3/bff64f5dcae286565fe425e79acd2d1e6e911d21",
        "a73ebcf2f049291b7e919da5e66a67b0c1f1cb3263c2183335763d15b5983251",
    )

    return module_ctx.extension_metadata(
        root_module_direct_deps = [
            "ec-coreboot-sdk-arm-eabi",
            "ec-coreboot-sdk-picolibc-arm-eabi",
            "ec-coreboot-sdk-libstdcxx-arm-eabi",
            "ec-coreboot-sdk-i386-elf",
            "ec-coreboot-sdk-picolibc-i386-elf",
            "ec-coreboot-sdk-libstdcxx-i386-elf",
            "ec-coreboot-sdk-nds32le-elf",
            "ec-coreboot-sdk-riscv-elf",
        ],
        root_module_direct_dev_deps = [],
        reproducible = True,
    )

ec_deps = module_extension(
    implementation = _ec_deps_impl,
)

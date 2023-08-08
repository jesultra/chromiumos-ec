# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@bazel_skylib//lib:dicts.bzl", "dicts")
load("//platform/ec/bazel:utils.bzl", "gen_shell_wrapper")

def _impl(ctx):
    args = ctx.attr._required_args + ctx.attr.args

    if "--outdir" in args:
        fail("--outdir is not permitted when run with Bazel")

    build_dir = ctx.actions.declare_file("twister-out_build")
    build_env = {
        # TODO(https://github.com/zephyrproject-rtos/zephyr/issues/59453):
        # This ought to be passed as a CMake variable but can't due to how
        # Zephyr calls verify-toolchain.cmake
        "ZEPHYR_TOOLCHAIN_VARIANT": "llvm",
        "TOOLCHAIN_ROOT": ctx.file._TOOLCHAIN_ROOT.path,
        # TODO(b/286589977): Remove this when hermetic clang is added
        "PATH": ":/bin:/usr/bin",
        "BAZEL_TWISTER_CWD": ctx.attr.cwd,
        "BAZEL_TWISTER_OUTDIR": build_dir.path,
    }

    deps = ctx.files._ec + ctx.files._zephyr

    twister_bin_tool_inputs, twister_bin_tool_input_mfs = \
        ctx.resolve_tools(tools = [ctx.attr._twister_bin])

    test_dir_path = "$TEST_TMPDIR/twister-out_test"
    # Uncomment this line to cause the HANG
    #test_dir_path = "$TEST_UNDECLARED_OUTPUTS_DIR/twister-out_test"
    test_env = dicts.add(build_env, {"BAZEL_TWISTER_OUTDIR": test_dir_path})

    # Build test executable that runs twister
    twister_test_script = ""

    # Add modified env variables
    for key, val in test_env.items():
        twister_test_script += "export %s=%s\n" % (key, val)

    # TODO before merge: we're supposed to use expand_template() here.
    # Twister expects to modify build artifacts (illegal by Bazel), so we need to copy them for testing.
    twister_test_script += "touch foo\n"
    twister_test_script += "cp -L -r {} {}\n".format(build_dir.short_path, test_dir_path)
    twister_test_script += "{} {} --test-only\n".format(ctx.executable._twister_bin.short_path, " ".join(ctx.attr.args))

    test_exe = ctx.actions.declare_file("twister_test.exe")
    ctx.actions.write(
        output = test_exe,
        content = twister_test_script,
    )

    ctx.actions.run(
        outputs = [build_dir],
        inputs = deps,
        tools = twister_bin_tool_inputs,
        executable = ctx.executable._twister_bin,
        arguments = args + ["-b"],
        mnemonic = "twister",
        use_default_shell_env = False,
        env = build_env,
        input_manifests = twister_bin_tool_input_mfs,
    )

    return DefaultInfo(
        files = depset([build_dir, test_exe]),
        runfiles = ctx.runfiles(files = [build_dir, test_exe, ctx.executable._twister_bin] + deps).merge(ctx.attr._twister_bin[DefaultInfo].default_runfiles),
        executable = test_exe,
    )

twister_test = rule(
    implementation = _impl,
    doc = "Run a salty delicious pretzel. Also verify the EC code",
    test = True,
    attrs = {
        "cwd": attr.string(default = ""),
        "_ec": attr.label(default = "@ec//:src", allow_files = True),
        "_TOOLCHAIN_ROOT": attr.label(default = "@ec//:zephyr", allow_single_file = True),
        "_zephyr": attr.label(default = "@zephyr//:src", allow_files = True),
        "_required_args": attr.string_list(default = [
            "-x=USE_CCACHE=0",
            "-x=USER_CACHE_DIR=/tmp/twister_cache",
        ]),
        "_twister_bin": attr.label(
            executable = True,
            allow_files = True,
            cfg = "exec",
            default = "@ec//:twister_binary",
        ),
    },
)

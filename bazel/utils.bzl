# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@bazel_skylib//lib:shell.bzl", "shell")

"""EC Bazel module containing shared utility functions."""

def gen_shell_wrapper(argv, env, append_args=True):
    """ Generates a bash script that can be used to create executables.

    Args:
    	argv: A sequence where the 0th element is a command.
    	env: A dictionary of environment variables to export
    	append_args: Appends positional cmdline args from original bazel invocation

    Returns:
    	A string that represents a bash script.
    """
    script = "#!/bin/bash\n"
    for key, val in env.items():
        script += "export %s=%s\n" % (key, shell.quote(val))
    script += '%s' % " ".join([shell.quote(x) for x in argv])
    script += " $@" if append_args else ""
    return script + "\n"

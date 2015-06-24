#!/usr/bin/python2
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Configuration Option Checker.

Script to ensure that all configuration options for the Chrome EC are defined
in config.h.
"""
from __future__ import print_function
import re
import os
import argparse

class MissingConfigOption(object):
  """Missing config option which contains the option, filename, and line number.

  Attributes:
    line_number: Line number within source file where config option appears.
    filename: Filename of source file where config option appears.
    name: Name of the config option.
  """
  def __init__(self, n, l, f):
    """Inits MissingConfigOption with its attributes."""
    self.line_number = l
    self.filename = f
    self.name = n

  def __str__(self):
    """Neatly formats output such that filenames are left-aligned."""
    res = "> "
    res += self.name + " "
    # The longest config option was 40 chars wide.
    for _ in range(45-len(self.name)):
      res += " "
    res += str(self.filename) + ":" + str(self.line_number)
    return res

def main():
  """Searches through all source files for missing config options.

  Checks through every C source and assembler file (not in the build/ and
  private/ directories) for CONFIG_* options. Then checks to make sure that
  all CONFIG_* options are defined in include/config.h. Finally, reports any
  missing config options.
  """
  config_options = []
  missing_config_options = []
  file_list = []

  parser = argparse.ArgumentParser(description='configuration option checker.')
  parser.add_argument('--cl', help='Files from the current CL',
                      action='store_true')
  parser.add_argument('--all_files', help='All files in EC code base',
                      action='store_true')
  args = parser.parse_args()

  # Create list of files to search
  if args.all_files:
    cwd = os.getcwd()
    for (dirpath, dirnames, filenames) in os.walk(cwd, topdown=True):
      # Ignore the build and private directories (taken from .gitignore)
      if "build" in dirnames:
        dirnames.remove("build")
      if "private" in dirnames:
        dirnames.remove("private")
      for f in filenames:
        # Only consider C source and assembler files.
        if f.endswith(('.c', '.h', '.inc', '.S')):
          file_list.append(os.path.join(dirpath, f))
  else:
    # Form list from presubmit environment variable.
    file_list = os.environ['PRESUBMIT_FILES'].split()

  config_option_re = re.compile(r'\s+(CONFIG_[a-zA-Z0-9_]*)\s*')
  with open("include/config.h", 'r') as config_file:
    for line in config_file:
      match = re.search(config_option_re, line)
      if match:
        if match.group(1) not in config_options:
          config_options.append(match.group(1))

  for f in file_list:
    with open(f, 'r') as cur_file:
      line_num = 0
      for line in cur_file:
        line_num += 1
        match = re.search(config_option_re, line)
        if match:
          if match.group(1) not in config_options:
            missing_config_options.append(MissingConfigOption(match.group(1),
                                                              line_num,
                                                              f))

  if missing_config_options:
    print('Please add all new config options to include/config.h along with ' \
          'a description of the option.\n\n' \
          'The following config options were found to be missing ' \
          'from include/config.h.\n')
    for config_option in missing_config_options:
      print(config_option)

    print('\nIt may also be possible that you have a typo.')
    os.sys.exit(1)

if __name__ == '__main__':
  main()

#!/usr/bin/env python
#
# Copyright (C) 2017 Ben Smith
#
# This software may be modified and distributed under the terms
# of the MIT license.  See the LICENSE file for details.
#
from __future__ import print_function
import argparse
import os
import re
import shutil
import sys

import common

MOONEYE_GB_DIR = os.path.join(common.TEST_DIR, 'mooneye-gb')
MOONEYE_GB_GIT_REPO = 'https://github.com/binji/mooneye-gb-tests'
MOONEYE_GB_GIT_SHA = 'fbef416596240ddb4347e208dcb81d845ae874ec'

MOONEYE_GB_WP_DIR = os.path.join(common.TEST_DIR, 'mooneye-gb-wp')
MOONEYE_GB_WP_GIT_REPO = 'https://github.com/binji/mooneye-gb-tests'
MOONEYE_GB_WP_GIT_SHA = '862912f5d22bd624ed11fc244000b0a38630c3d4'

WLA_DX_DIR = os.path.join(common.THIRD_PARTY_DIR, 'wla-dx')
WLA_DX_BUILD_DIR = os.path.join(WLA_DX_DIR, 'build')
WLA_DX_BIN_DIR = os.path.join(WLA_DX_BUILD_DIR, 'binaries')
WLA_DX_GIT_REPO = 'https://github.com/vhelin/wla-dx'
WLA_DX_GIT_SHA = '01f7fd16a111ed2cbb81b4c07ed5e793bff5d6bd'


def Run(exe, *args, **kwargs):
  kwargs['verbose'] = True
  return common.Run(exe, *args, **kwargs)


def GitUpdate(repo, dirname, sha):
  if os.path.exists(dirname):
    Run('git', 'fetch', cwd=dirname)
  else:
    Run('git', 'clone', repo, dirname)
  Run('git', 'checkout', sha, cwd=dirname)


def BuildWlaGb():
  GitUpdate(WLA_DX_GIT_REPO, WLA_DX_DIR, WLA_DX_GIT_SHA)
  if not os.path.exists(WLA_DX_BUILD_DIR):
    os.makedirs(WLA_DX_BUILD_DIR)
  Run('cmake', WLA_DX_DIR, cwd=WLA_DX_BUILD_DIR)
  Run('make', cwd=WLA_DX_BUILD_DIR)
  # Test that wla-gb was build OK.
  Run(os.path.join(WLA_DX_BIN_DIR, 'wla-gb'))


def BuildMooneyeTests():
  env = dict(os.environ)
  env['PATH'] += ':' + WLA_DX_BIN_DIR
  GitUpdate(MOONEYE_GB_GIT_REPO, MOONEYE_GB_DIR, MOONEYE_GB_GIT_SHA)
  Run('make', cwd=MOONEYE_GB_DIR, env=env)

  GitUpdate(MOONEYE_GB_WP_GIT_REPO, MOONEYE_GB_WP_DIR, MOONEYE_GB_WP_GIT_SHA)
  Run('make', cwd=MOONEYE_GB_WP_DIR, env=env)


def main(args):
  parser = argparse.ArgumentParser()

  BuildWlaGb()
  BuildMooneyeTests()


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))

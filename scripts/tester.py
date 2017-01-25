#!/usr/bin/env python
#
# Copyright (C) 2016 Ben Smith
#
# This software may be modified and distributed under the terms
# of the MIT license.  See the LICENSE file for details.
#
from __future__ import print_function
import argparse
import json
import os
import sys

import common

TEST_JSON = os.path.join(common.SCRIPT_DIR, 'test.json')
TEST_RESULT_DIR = os.path.join(common.OUT_DIR, 'test_results')

def RunTest(rom, frames, expected, options):
  ppm = os.path.join(TEST_RESULT_DIR,
                     os.path.basename(os.path.splitext(rom)[0]) + '.ppm')
  common.RunTester(rom, frames, ppm, debug_exe=options.debug_exe)
  actual = common.HashFile(ppm)

  if expected.startswith('!'):
    expect_fail = True
    expected = expected[1:]
  else:
    expect_fail = False

  ok = False
  if actual == expected:
    if not expect_fail:
      if options.verbose:
        print('[OK] %s' % rom)
      ok = True
  else:
    if expected == '' or expect_fail:
      print('[?]  %s => %s' % (rom, actual))
    else:
      print('[X]  %s => %s' % (rom, actual))
  return ok


def main(args):
  parser = argparse.ArgumentParser()
  parser.add_argument('patterns', metavar='pattern', nargs='*',
                      help='test patterns.')
  parser.add_argument('-d', '--debug-exe', action='store_true',
                      help='run debug tester')
  parser.add_argument('-v', '--verbose', action='store_true',
                      help='show more info')
  options = parser.parse_args(args)
  pattern_re = common.MakePatternRE(options.patterns)
  total = 0
  passed = 0
  if not os.path.exists(TEST_RESULT_DIR):
    os.makedirs(TEST_RESULT_DIR)

  TESTS = json.load(open(TEST_JSON))
  for rom, frames, expected in TESTS:
    if pattern_re.match(rom):
      total += 1
      rom = os.path.join(common.ROOT_DIR, rom)
      try:
        if RunTest(rom, frames, expected, options):
          passed += 1
      except common.Error as e:
        print('[X]  %s\n' % rom, e)
  print('Passed %d/%d' % (passed, total))
  if total == passed:
    return 0
  return 1

if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))

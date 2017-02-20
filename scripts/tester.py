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
import multiprocessing
import os
import sys
import time

import common

TEST_JSON = os.path.join(common.SCRIPT_DIR, 'test.json')
TEST_RESULT_DIR = os.path.join(common.OUT_DIR, 'test_results')

def RunTest(rom, frames, expected, options):
  start_time = time.time()
  ppm = os.path.join(TEST_RESULT_DIR,
                     os.path.basename(os.path.splitext(rom)[0]) + '.ppm')
  try:
    common.RunTester(rom, frames, ppm, debug_exe=options.debug_exe)
    actual = common.HashFile(ppm)

    if expected.startswith('!'):
      expect_fail = True
      expected = expected[1:]
    else:
      expect_fail = False

    message = ''
    if actual == expected:
      if options.verbose and not expect_fail:
        message = '[OK] %s' % rom
    else:
      if expected == '' or expect_fail:
        message = '[?]  %s => %s' % (rom, actual)
      else:
        message = '[X]  %s => %s' % (rom, actual)

    ok = actual == expected and not expect_fail
    duration = time.time() - start_time
    return rom, ok, message, duration
  except (common.Error, KeyboardInterrupt) as e:
    duration = time.time() - start_time
    return rom, False, '[X]  %s => %s' % (rom, str(e)), duration


last_message_len = 0
def PrintReplace(s, newline=False):
  global last_message_len
  if sys.stdout.isatty():
    sys.stdout.write('\r' + s)
    if len(s) < last_message_len:
      sys.stdout.write(' ' * (last_message_len - len(s)))
    if newline:
      sys.stdout.write('\n')
    else:
      sys.stdout.flush()
    last_message_len = len(s)
  else:
    sys.stdout.write(s)
    sys.stdout.write('\n')


def main(args):
  parser = argparse.ArgumentParser()
  parser.add_argument('patterns', metavar='pattern', nargs='*',
                      help='test patterns.')
  parser.add_argument('-j', '--num-processes',
                      type=int, default=multiprocessing.cpu_count(),
                      help='num processes.')
  parser.add_argument('-d', '--debug-exe', action='store_true',
                      help='run debug tester')
  parser.add_argument('-v', '--verbose', action='store_true',
                      help='show more info')
  options = parser.parse_args(args)
  pattern_re = common.MakePatternRE(options.patterns)
  passed = 0
  if not os.path.exists(TEST_RESULT_DIR):
    os.makedirs(TEST_RESULT_DIR)

  tests = [test for test in json.load(open(TEST_JSON))
           if pattern_re.match(test[0])]

  start_time = time.time()
  pool = multiprocessing.Pool(options.num_processes)
  try:
    results = [pool.apply_async(RunTest, (rom, frames, expected, options))
               for rom, frames, expected in tests]
    started = 0
    completed = 0
    last_message_len = 0
    while results:
      new_results = []
      for result in results:
        if result.ready():
          completed += 1
          rom, ok, message, duration = result.get(0)
          if ok:
            passed += 1
          if message:
            PrintReplace(message, newline=True)
          PrintReplace('[%d/%d] %s (%.3fs)' % (completed, len(tests), rom,
                                               duration))
        else:
          new_results.append(result)
      time.sleep(0.01)
      results = new_results
    pool.close()
  except KeyboardInterrupt:
    pass
  finally:
    pool.terminate()
    pool.join()

  duration = time.time() - start_time
  PrintReplace('total time: %.3fs' % duration, newline=True)
  print('Passed %d/%d' % (passed, completed))
  if passed == completed:
    return 0
  return 1

if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))

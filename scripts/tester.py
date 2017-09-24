#!/usr/bin/env python
#
# Copyright (C) 2016 Ben Smith
#
# This software may be modified and distributed under the terms
# of the MIT license.  See the LICENSE file for details.
#
from __future__ import print_function
import argparse
import collections
import json
import multiprocessing
import os
import sys
import time

import common

TEST_JSON = os.path.join(common.SCRIPT_DIR, 'test.json')
TEST_RESULT_DIR = os.path.join(common.OUT_DIR, 'test_results')
TEST_RESULTS_MD = os.path.join(common.ROOT_DIR, 'test_results.md')

OK      = '[OK] '
FAIL    = '[X]  '
UNKNOWN = '[?]  '

Test = collections.namedtuple('Test', ['suite', 'rom', 'frames', 'hash'])
TestResult = collections.namedtuple('TestResult',
                                    ['test', 'passed', 'ok', 'message',
                                     'duration'])


def RunTest(test, options):
  start_time = time.time()
  ppm = os.path.join(TEST_RESULT_DIR,
                     os.path.basename(os.path.splitext(test.rom)[0]) + '.ppm')
  try:
    common.RunTester(test.rom, test.frames, ppm, exe=options.exe)
    actual = common.HashFile(ppm)

    if test.hash.startswith('!'):
      expect_fail = True
      expected = test.hash[1:]
    else:
      expect_fail = False
      expected = test.hash

    message = ''
    ok = actual == expected
    if ok:
      if expect_fail and options.verbose > 0:
        message = FAIL + test.rom
      elif options.verbose > 1:
        message = OK + test.rom
    else:
      if expected == '' or expect_fail:
        message = UNKNOWN + '%s => %s' % (test.rom, actual)
      else:
        message = FAIL + '%s => %s' % (test.rom, actual)

    passed = ok and not expect_fail
    duration = time.time() - start_time
    return TestResult(test, passed, ok, message, duration)
  except (common.Error, KeyboardInterrupt) as e:
    duration = time.time() - start_time
    message = FAIL + '%s => %s' % (test.rom, str(e))
    return TestResult(test, False, False, message, duration)


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


def RunAllTests(tests, options):
  results = []
  pool = multiprocessing.Pool(options.num_processes)
  try:
    tasks = [pool.apply_async(RunTest, (test, options)) for test in tests]
    passed = 0
    failed = 0
    while tasks:
      new_tasks = []
      for task in tasks:
        if task.ready():
          result = task.get(0)
          results.append(result)

          if result.ok:
            passed += 1
          else:
            failed += 1

          if result.message:
            PrintReplace(result.message, newline=True)

          PrintReplace('[+%d|-%d|%%%d] %s (%.3fs)' % (
            passed, failed, 100 * ((passed + failed) / len(tests)),
            result.test.rom, result.duration))
        else:
          new_tasks.append(task)
      time.sleep(0.01)
      tasks = new_tasks
    pool.close()
  except KeyboardInterrupt:
    pass
  finally:
    pool.terminate()
    pool.join()

  return results


def MDLink(text, url):
  return '[%s](%s)' % (text, url)

def MDTableRow(*columns):
  return '| %s |\n' % ' | '.join(columns)

def MDTestName(rom, prefix):
  assert rom.startswith(prefix)
  name = rom[len(prefix):]
  name = os.path.splitext(name)[0]
  name = name.replace('_', '\\_')
  return name

def MDTestResult(passed):
  return ':ok:' if passed else ':x:'

def SuiteHeader(out_file, name, url):
  out_file.write('%s:\n\n' % MDLink(name, url))
  out_file.write(MDTableRow('Test', 'Result'))
  out_file.write(MDTableRow('---', '---'))

def Suite(out_file, results, suite, prefix):
  for result in sorted(results, key=lambda x: x.test.rom):
    if result.test.suite != suite:
      continue
    name = MDTestName(result.test.rom, prefix)
    out_file.write(MDTableRow(name, MDTestResult(result.passed)))
  out_file.write('\n')

def GenerateTestResults(results):
  SUITES = [
    {'name': 'Blargg\'s tests',
     'url': 'http://gbdev.gg8.se/wiki/articles/Test_ROMs',
     'suite': 'blargg',
     'prefix': 'test/blargg/'},
    {'name': 'Gekkio\'s mooneye-gb tests',
     'url': 'https://github.com/Gekkio/mooneye-gb',
     'suite': 'mooneye',
     'prefix': 'test/mooneye-gb/build/'},
    {'name': 'Wilbert Pol\'s mooneye-gb tests',
     'url': 'https://github.com/wilbertpol/mooneye-gb',
     'suite': 'wilbertpol',
     'prefix': 'test/mooneye-gb-wp/build/'},
  ]

  with open(TEST_RESULTS_MD, 'w') as out_file:
    out_file.write('# Test status\n\n')
    for suite in SUITES:
      SuiteHeader(out_file, suite['name'], suite['url'])
      Suite(out_file, results, suite['suite'], suite['prefix'])


def main(args):
  parser = argparse.ArgumentParser()
  parser.add_argument('patterns', metavar='pattern', nargs='*',
                      help='test patterns.')
  parser.add_argument('-j', '--num-processes',
                      type=int, default=multiprocessing.cpu_count(),
                      help='num processes.')
  parser.add_argument('-e', '--exe', help='path to tester')
  parser.add_argument('-v', '--verbose', action='count', default=0,
                      help='show more info')
  parser.add_argument('-g', '--generate', action='store_true',
                      help='generate test result markdown')
  options = parser.parse_args(args)
  pattern_re = common.MakePatternRE(options.patterns)
  passed = 0
  if not os.path.exists(TEST_RESULT_DIR):
    os.makedirs(TEST_RESULT_DIR)

  tests = [Test(*test) for test in json.load(open(TEST_JSON))]
  tests = [test for test in tests if pattern_re.match(test.rom)]

  start_time = time.time()
  results = RunAllTests(tests, options)
  duration = time.time() - start_time
  PrintReplace('total time: %.3fs' % duration, newline=True)

  ok = sum(1 for result in results if result.ok)
  passed = sum(1 for result in results if result.passed)
  completed = len(results)
  print('Passed %d/%d' % (passed, completed))

  if options.generate:
    GenerateTestResults(results)

  return 0 if ok == completed else 1

if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))

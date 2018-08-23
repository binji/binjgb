# Copyright (C) 2016 Ben Smith
#
# This software may be modified and distributed under the terms
# of the MIT license.  See the LICENSE file for details.
#
from __future__ import print_function
import fnmatch
import os
import hashlib
import re
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(SCRIPT_DIR)
BIN_DIR = os.path.join(ROOT_DIR, 'bin')
OUT_DIR = os.path.join(ROOT_DIR, 'out')
ROM_DIR = os.path.join(ROOT_DIR, 'rom')
TEST_DIR = os.path.join(ROOT_DIR, 'test')
THIRD_PARTY_DIR = os.path.join(ROOT_DIR, 'third_party')
TESTER = os.path.join(BIN_DIR, 'binjgb-tester')


class Error(Exception):
  pass


def Run(exe, *args, **kwargs):
  cwd = kwargs.get('cwd')
  cmd = [exe] + list(args)

  if kwargs.get('verbose', False):
    print('>', ' '.join(cmd), '[cwd = %s]' % cwd if cwd is not None else '')

  basename = os.path.basename(exe)
  try:
    env = kwargs.get('env')
    PIPE = subprocess.PIPE
    process = subprocess.Popen(cmd, stdin=PIPE, stdout=PIPE, stderr=PIPE,
                               cwd=cwd, env=env)
    stdout, stderr = process.communicate()
    if process.returncode != 0:
      raise Error('Error running "%s":\n%s' % (basename, stderr.decode('ascii')))
  except OSError as e:
    raise Error('Error running "%s": %s' % (basename, str(e)))


def RunTester(rom, frames=None, out_ppm=None, animate=False,
              controller_input=None, exe=None, timeout_sec=None,
              seed=0):
  exe = exe or TESTER
  cmd = []
  if frames:
    cmd.extend(['-f', str(frames)])
  if controller_input:
    cmd.extend(['-i', controller_input])
  if out_ppm:
    cmd.extend(['-o', out_ppm])
  if animate:
    cmd.append('-a')
  if timeout_sec:
    cmd.extend(['-t', str(timeout_sec)])
  cmd.extend(['-s', str(seed)])
  cmd.append(rom)
  Run(exe, *cmd)


def HashFile(filename):
  m = hashlib.sha1()
  m.update(open(filename, 'rb').read())
  return m.hexdigest()


def MakePatternRE(patterns):
  if patterns:
    pattern_re = '|'.join(fnmatch.translate('*%s*' % p) for p in patterns)
  else:
    pattern_re = '.*'
  return re.compile(pattern_re)


def GetMatchedRoms(pattern_re, top_dir=None):
  if top_dir is None:
    top_dir = ROM_DIR
  roms = []
  for root, dirs, files in os.walk(top_dir):
    for file_ in files:
      path = os.path.join(root, file_)
      if not os.path.splitext(path)[1].startswith('.gb'):
        continue
      if not pattern_re.match(path):
        continue
      if 'GBS' in path:
        continue
      roms.append(path)
  return roms

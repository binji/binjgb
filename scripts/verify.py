#!/usr/bin/env python
#
# Copyright (C) 2017 Ben Smith
#
# This software may be modified and distributed under the terms
# of the MIT license.  See the LICENSE file for details.
#
from __future__ import print_function
import argparse
import hashlib
import os
import re
import shutil
import sys

import common

STR = '"([^"]+)"'
DEC = '(\d+)'
HEX = '([0-9A-F]+)'

def ParseDat(path):
  data = {}
  in_game = False
  with open(path) as f:
    for line in f:
      if line[-1] == '\n':
        line = line[:-1]
      if line == 'game (':
        in_game = True
      elif in_game:
        if line.startswith('\tname'):
          name = line[len('\tname "'):-1]
        elif line.startswith('\trom'):
          m = re.match(r'\trom \( name %s size %s crc %s md5 %s sha1 %s' %
            (STR, DEC, HEX, HEX, HEX), line)
          sha1 = m.group(5)
          data[sha1.lower()] = {
            'name': m.group(1),
            'size': int(m.group(2)),
            'crc': m.group(3),
            'md5': m.group(4),
            'sha1': sha1,
          }
        elif line == ')':
          in_game = False
  return data


def HashFile(path):
  h = hashlib.sha1()
  with open(path, 'rb') as f:
    h.update(f.read())
  return h.hexdigest()


def MoveFile(src, dst):
  print('Moving %s -> %s' % (src, dst))
  try:
    shutil.move(src, dst)
  except shutil.Error as e:
    print(e)


def main(args):
  parser = argparse.ArgumentParser()
  parser.add_argument('dat', help='dat-o-matic file')
  parser.add_argument('patterns', metavar='pattern', nargs='*',
                      help='test patterns.')
  options = parser.parse_args(args)
  pattern_re = common.MakePatternRE(options.patterns)
  roms = common.GetMatchedRoms(pattern_re)
  dat = ParseDat(options.dat)

  unverified_dir = os.path.join(common.ROM_DIR, 'unverified')
  if not os.path.isdir(unverified_dir):
    os.makedirs(unverified_dir)

  for rom in sorted(roms):
    h = HashFile(rom)
    if h in dat:
      rom_data = dat[h]
      MoveFile(rom, os.path.join(common.ROM_DIR, rom_data['name']))
    else:
      MoveFile(rom, unverified_dir)

if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))

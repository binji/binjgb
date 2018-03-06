#!/usr/bin/env python
#
# Copyright (C) 2018 Ben Smith
#
# This software may be modified and distributed under the terms
# of the MIT license.  See the LICENSE file for details.
#
from __future__ import print_function
import argparse
import os
import sys

import common

USAGE_STRING = {0: 'Unknown', 2: 'Data', 3: 'Code'}


def AddrFromLoc(loc):
  bank = loc >> 14
  addr = (loc & 0x3fff) + (0 if bank == 0 else 0x4000)
  return bank, addr


def LocString(loc):
  bank, addr = AddrFromLoc(loc)
  return '%02x:%04x' % (bank, addr)


def main(args):
  parser = argparse.ArgumentParser()
  parser.add_argument('usage', help='usage file')
  parser.add_argument('-o', '--output', help='output file')
  options = parser.parse_args(args)

  with open(options.usage, 'rb') as file:
    rom_usage = bytearray(file.read())

  if options.output:
    outfile = open(options.output, 'w')
  else:
    outfile = sys.stdout

  counts = {
    0: 0,
    2: 0,
    3: 0,
  }

  last_usage = None
  loc = 0
  start = 0

  def Print():
    outfile.write('%s..%s: %s\n' % (
      LocString(start), LocString(loc - 1), USAGE_STRING[last_usage]))

  while loc < len(rom_usage):
    usage = rom_usage[loc] & 3
    if last_usage is not None and usage != last_usage:
      Print()
      start = loc

    counts[usage] += 1
    last_usage = usage
    loc += 1

  Print()

  sys.stderr.write('Unknown: %d\nData: %d\nCode: %d\n' % (
      counts[0], counts[2], counts[3]))


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))

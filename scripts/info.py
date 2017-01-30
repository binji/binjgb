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
import sys

import common

MINIMUM_ROM_SIZE = 2**15

LOGO_CHECKSUM = 0xe06c8834

CGB_FLAG = {
    0: 'none',
    0x80: 'supported',
    0xc0: 'required',
}

SGB_FLAG = {
    0: 'none',
    3: 'supported',
}

CARTRIDGE_TYPE = {
  0x0: 'rom_only',
  0x1: 'mbc1',
  0x2: 'mbc1_ram',
  0x3: 'mbc1_ram_battery',
  0x5: 'mbc2',
  0x6: 'mbc2_battery',
  0x8: 'rom_ram',
  0x9: 'rom_ram_battery',
  0xb: 'mmm01',
  0xc: 'mmm01_ram',
  0xd: 'mmm01_ram_battery',
  0xf: 'mbc3_timer_battery',
  0x10: 'mbc3_timer_ram_battery',
  0x11: 'mbc3',
  0x12: 'mbc3_ram',
  0x13: 'mbc3_ram_battery',
  0x15: 'mbc4',
  0x16: 'mbc4_ram',
  0x17: 'mbc4_ram_battery',
  0x19: 'mbc5',
  0x1a: 'mbc5_ram',
  0x1b: 'mbc5_ram_battery',
  0x1c: 'mbc5_rumble',
  0x1d: 'mbc5_rumble_ram',
  0x1e: 'mbc5_rumble_ram_battery',
  0xfc: 'pocket_camera',
  0xfd: 'bandai_tama5',
  0xfe: 'huc3',
  0xff: 'huc1_ram_battery',
}

ROM_SIZE = {
  0: 2**15,
  1: 2**16,
  2: 2**17,
  3: 2**18,
  4: 2**19,
  5: 2**20,
  6: 2**21,
  7: 2**22,
  8: 2**23,
}

EXT_RAM_SIZE = {
  0: 0,
  1: 2**11,
  2: 2**13,
  3: 2**15,
  4: 2**17,
  5: 2**16,
}


def SimpleChecksum(data):
  result = 0
  for x in data:
    result = (result << 1) ^ x
  return result & 0xffffffff


def RomInfo(path):
  with open(path, 'rb') as f:
    data = f.read()
    for offset in range(0, len(data), MINIMUM_ROM_SIZE):
      if SimpleChecksum(data[offset+0x104:offset+0x134]) != LOGO_CHECKSUM:
        continue
      yield {
        'title': data[offset+0x134:offset+0x144],
        'cgb': CGB_FLAG.get(data[offset+0x143], 'unknown'),
        'sgb': SGB_FLAG.get(data[offset+0x146], 'unknown'),
        'cartridge_type': CARTRIDGE_TYPE.get(data[offset+0x147], 'unknown'),
        'rom_size': ROM_SIZE.get(data[offset+0x148], -1),
        'ext_ram_size': EXT_RAM_SIZE.get(data[offset+0x149], -1),
        'start': hex(offset)
      }


def main(args):
  parser = argparse.ArgumentParser()
  parser.add_argument('patterns', metavar='pattern', nargs='*',
                      help='test patterns.')
  parser.add_argument('-C', '--dir', help='search for ROMs in dir')
  options = parser.parse_args(args)
  pattern_re = common.MakePatternRE(options.patterns)
  roms = common.GetMatchedRoms(pattern_re, options.dir)

  cols = ('filename', 'title', 'cartridge_type', 'rom_size', 'ext_ram_size',
          'cgb', 'sgb', 'start')
  rows = [cols]
  for rom in sorted(roms):
    for i, info in enumerate(RomInfo(rom)):
      title = info['title']
      title = title[:title.find(b'\0')].decode('ascii', 'replace')
      filename = os.path.basename(rom) if i == 0 else ' *** '
      rows.append((
        filename,
        '"%s"' % title,
        info['cartridge_type'],
        str(info['rom_size']),
        str(info['ext_ram_size']),
        info['cgb'],
        info['sgb'],
        info['start']
      ))

  max_cols = [0] * len(rows[0])
  for row in rows:
    for col, item in enumerate(row):
      max_cols[col] = max(max_cols[col], len(item))

  fmt = '%%-%ds' % max_cols[0]
  for col in range(1, len(max_cols)):
    fmt += '  %%%ds' % max_cols[col]

  rows.insert(1, tuple('_' * v for v in max_cols))

  for row in rows:
    print(fmt % row)

if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))

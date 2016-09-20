#!/usr/bin/env python
#
# Copyright (C) 2016 Ben Smith
#
# This software may be modified and distributed under the terms
# of the MIT license.  See the LICENSE file for details.
#
from __future__ import print_function
import argparse
import os
import struct
import sys

import common

def ReadFile(path):
  with open(path, 'rb') as f:
    return f.read()

class IPS(object):
  EOF = struct.unpack('>I', b'\0EOF')[0]

  def __init__(self, data):
    self.data = data
    if self.data[:5] != b'PATCH':
      raise common.Error('Bad IPS file.')
    self.offset = 5

  def Apply(self, file_data):
    result = bytearray(file_data)
    while True:
      patch_off = self.ReadI()
      if patch_off == IPS.EOF:
        break
      patch_len = self.ReadH()
      if patch_len == 0:
        # RLE
        patch_len = self.ReadH()
        patch_data = bytearray([self.ReadB()]) * patch_len
      else:
        patch_data = self.ReadBytes(patch_len)

      patch_end = patch_off + patch_len
      if patch_end > len(result):
        patch_extra = patch_end - len(result)
        patch_len -= patch_extra
        result += patch_data[patch_len:]
      result[patch_off:patch_off+patch_len] = patch_data[:patch_len]
    return result

  def ReadB(self):
    return struct.unpack('B', self.ReadBytes(1))[0]

  def ReadH(self):
    return struct.unpack('>H', self.ReadBytes(2))[0]

  def ReadI(self):
    return struct.unpack('>I', b'\0' + self.ReadBytes(3))[0]

  def ReadBytes(self, length):
    if self.offset + length > len(self.data):
      raise common.Error('Bad IPS file.')
    result = self.data[self.offset:self.offset+length]
    self.offset += length
    return result


def main(args):
  parser = argparse.ArgumentParser()
  parser.add_argument('file', help='input file')
  parser.add_argument('ips', help='input IPS')
  parser.add_argument('-o', '--output', help='output file')
  options = parser.parse_args(args)

  file_data = ReadFile(options.file)
  ips_data = ReadFile(options.ips)

  ips = IPS(ips_data)
  new_file_data = ips.Apply(file_data)
  if options.output:
    with open(options.output, 'wb') as f:
      f.write(new_file_data)

  return 1

if __name__ == '__main__':
  try:
    sys.exit(main(sys.argv[1:]))
  except KeyboardInterrupt as e:
    print(e)

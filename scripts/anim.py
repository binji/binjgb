#!/usr/bin/env python
#
# Copyright (C) 2016 Ben Smith
#
# This software may be modified and distributed under the terms
# of the MIT license.  See the LICENSE file for details.
#
from __future__ import print_function
import argparse
import multiprocessing
import os
import shutil
import sys
import tempfile
import time

import common

OUT_ANIM_DIR = os.path.join(common.OUT_DIR, 'anim')

DEFAULT_FRAMES = 2400
CONTROLLER_INPUT_FILE = os.path.join(common.SCRIPT_DIR, 'input_move_right.txt')


def ChangeExt(path, new_ext):
  return os.path.splitext(path)[0] + new_ext


def ChangeDir(new_dir, path):
  return os.path.join(new_dir, os.path.basename(path))


def ConvertPPMstoMP4(tempdir, src):
  srcs = ChangeExt(src, '.%08d.ppm')
  dst = ChangeDir(OUT_ANIM_DIR, ChangeExt(src, '.mp4'))
  common.Run('ffmpeg',
             '-y',
             '-framerate', '60',
             '-i', srcs,
             '-c:v', 'libx264',
             '-r', '30',
             '-pix_fmt', 'yuv420p',
             dst)


def Run(rom):
  start_time = time.time()
  tempdir = None
  try:
    tempdir = tempfile.mkdtemp(prefix='rom_anims')
    default_img = ChangeDir(tempdir, ChangeExt(rom, '.ppm'))
    try:
      common.RunTester(rom, DEFAULT_FRAMES, default_img,
                       controller_input=CONTROLLER_INPUT_FILE, animate=True)
      ConvertPPMstoMP4(tempdir, default_img)
    except common.Error as e:
      print(str(e))
  finally:
    if tempdir:
      shutil.rmtree(tempdir)
  duration = time.time() - start_time
  return rom, duration


def main(args):
  parser = argparse.ArgumentParser()
  parser.add_argument('-l', '--list', action='store_true',
                      help='list matching ROMs')
  parser.add_argument('-j', '--num-processes',
                      type=int, default=multiprocessing.cpu_count(),
                      help='num processes.')
  parser.add_argument('patterns', metavar='pattern', nargs='*',
                      help='test patterns.')
  options = parser.parse_args(args)
  pattern_re = common.MakePatternRE(options.patterns)

  roms = []
  for root, dirs, files in os.walk(common.ROM_DIR):
    for file_ in files:
      path = os.path.join(root, file_)
      if not os.path.splitext(path)[1].startswith('.gb'):
        continue
      if not pattern_re.match(path):
        continue
      if 'GBS' in path:
        continue
      roms.append(path)

  if options.list:
    for rom in roms:
      print(rom)
    return 0

  try:
    os.makedirs(OUT_ANIM_DIR)
  except OSError as e:
    print(e)

  start_time = time.time()
  pool = multiprocessing.Pool(options.num_processes)
  try:
    results = [pool.apply_async(Run, (rom,)) for rom in roms]
    started = 0
    completed = 0
    while results:
      new_results = []
      for result in results:
        if result.ready():
          completed += 1
          rom, duration = result.get(0)
          print('[%d/%d] %s (%.3fs)' % (completed, len(roms), rom, duration))
        else:
          new_results.append(result)
      time.sleep(0.01)
      results = new_results
    pool.close()
  finally:
    pool.terminate()
    pool.join()
  duration = time.time() - start_time
  print('total time: %.3fs' % duration)
  return 0


if __name__ == '__main__':
  try:
    sys.exit(main(sys.argv[1:]))
  except KeyboardInterrupt as e:
    print(e)

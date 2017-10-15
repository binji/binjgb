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

SDL2_NAME = 'SDL2-2.0.5'
SDL2_FILE = '%s.tar.gz' % SDL2_NAME
SDL2_URL = 'https://libsdl.org/release/' + SDL2_FILE
SDL2_DIR = os.path.join(common.ROOT_DIR, SDL2_NAME)
PREFIX = os.path.join(common.ROOT_DIR, 'sdl2')

def Run(exe, *args, **kwargs):
  kwargs['verbose'] = True
  return common.Run(exe, *args, **kwargs)


def main(args):
  parser = argparse.ArgumentParser()

  Run('wget', SDL2_URL)
  Run('tar', 'xf', SDL2_FILE)
  Run('./configure', '--prefix=%s' % PREFIX, cwd=SDL2_DIR)
  Run('make', cwd=SDL2_DIR)
  Run('make', 'install', cwd=SDL2_DIR)


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))


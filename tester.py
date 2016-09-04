#!/usr/bin/env python
#
# Copyright (C) 2016 Ben Smith
#
# This software may be modified and distributed under the terms
# of the MIT license.  See the LICENSE file for details.
#
from __future__ import print_function
import argparse
import fnmatch
import hashlib
import os
import re
import subprocess
import sys

ACCEPTANCE = 'test/mooneye/acceptance/'
BITS = ACCEPTANCE + 'bits/'
GPU = ACCEPTANCE + 'gpu/'
TIMER = ACCEPTANCE + 'timer/'
TESTS = [
  (ACCEPTANCE + 'add_sp_e_timing.gb', 1, '91c16f7ebc814cd9202a870e8f85ef99ff53bf35'),
  (ACCEPTANCE + 'boot_hwio-G.gb', 1, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),
  (ACCEPTANCE + 'boot_regs-dmg.gb', 1, 'e4da33ca86b93941597c8d218b9d8f9b56892b1d'),
  (ACCEPTANCE + 'call_cc_timing.gb', 1, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),
  (ACCEPTANCE + 'call_cc_timing2.gb', 1, '4ff5fa09fe988f48144183136bd19fba5cc62dec'),
  (ACCEPTANCE + 'call_timing.gb', 1, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),
  (ACCEPTANCE + 'call_timing2.gb', 1, '2c3629389b4a3d278f0cd1f931e6374f849708f5'),
  (ACCEPTANCE + 'di_timing-GS.gb', 6, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),
  (ACCEPTANCE + 'div_timing.gb', 1, '07fff0c8bbddd2a2d1385bb44010c42523997e87'),
  (ACCEPTANCE + 'ei_timing.gb', 1, 'c675f7dac849d2fc8b0f6591a80fe3b92ea8e500'),
  (ACCEPTANCE + 'halt_ime0_ei.gb', 1, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),
  (ACCEPTANCE + 'halt_ime0_nointr_timing.gb', 1, '29076d95f684cc0e36486f743b4e40298436821c'),
  (ACCEPTANCE + 'halt_ime1_timing.gb', 1, 'be5d42552c3bf9135e415dbe2e5933b2b4814ab5'),
  (ACCEPTANCE + 'halt_ime1_timing2-GS.gb', 6, '7288573b156b9f6620829d43966fa87085e61315'),
  (ACCEPTANCE + 'if_ie_registers.gb', 1, 'a73f7b6492b660632a8a2235c474aa60bb22cfc0'),
  (ACCEPTANCE + 'intr_timing.gb', 1, 'fbf8884d0648f9ab566460e51fec911e7a073030'),
  (ACCEPTANCE + 'jp_cc_timing.gb', 1, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),
  (ACCEPTANCE + 'jp_timing.gb', 1, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),
  (ACCEPTANCE + 'ld_hl_sp_e_timing.gb', 1, '43886294ef43336dcf127b3cba58e696911f9616'),
  (ACCEPTANCE + 'oam_dma_restart.gb', 1, 'b394406adf44d00cfef91502412d24be6b4e756c'),
  (ACCEPTANCE + 'oam_dma_start.gb', 1, 'fa81e5fb4847943bd919691d3a302d5cc924c6b1'),
  (ACCEPTANCE + 'oam_dma_timing.gb', 1, 'b394406adf44d00cfef91502412d24be6b4e756c'),
  (ACCEPTANCE + 'pop_timing.gb', 1, 'b98432763121e45b47e949f10358f5c1399c84b2'),
  (ACCEPTANCE + 'push_timing.gb', 1, '47ceac0a1b51f77ccc24db9d5af6382f285e0f6f'),
  (ACCEPTANCE + 'rapid_di_ei.gb', 1, 'e4684f86ff1d7aa326fbd20b829b325d18668f22'),
  (ACCEPTANCE + 'ret_cc_timing.gb', 1, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),
  (ACCEPTANCE + 'ret_timing.gb', 1, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),
  (ACCEPTANCE + 'reti_intr_timing.gb', 1, '92ece52e55854710a57390f26a8f6af85493fc0f'),
  (ACCEPTANCE + 'reti_timing.gb', 1, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),
  (ACCEPTANCE + 'rst_timing.gb', 1, '4c8c495b6860ab271c6892409e86a8221fc09b72'),

  (BITS + 'mem_oam.gb', 1, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),
  (BITS + 'reg_f.gb', 1, '6252ab378e3e3d4f0c8d52b88fa1aa08663d8c3e'),
  (BITS + 'unused_hwio-GS.gb', 1, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),

  (GPU + 'intr_0_timing.gb', 2, '16ea3ff60cf26b408fe014b27bc92b85617ad004'),
  (GPU + 'intr_1_2_timing-GS.gb', 6, 'facd6c9c63e44b4b3f41bbce29e665d63b150c9d'),
  (GPU + 'intr_1_timing.gb', 2, 'b6e1acefdababf4c053789912e4408e1fcb1b8e7'),
  (GPU + 'intr_2_0_timing.gb', 2, '66b8bd35827b39a5778780693844ed8ef9796f67'),
  (GPU + 'intr_2_mode0_timing.gb', 2, '094840252ecd6b5321a9b9812767e435bdce3464'),
  (GPU + 'intr_2_mode3_timing.gb', 2, '369978e18825ec7a3fc5f39bdee1dff4c9a0e9af'),
  (GPU + 'intr_2_oam_ok_timing.gb', 2, 'a10a51597f97912c59e1006a23eda5852a39f199'),
  (GPU + 'intr_2_timing.gb', 6, '92db41dde5c25f8627f307d7866db7348717e91d'),
  (GPU + 'lcdon_mode_timing.gb', 6, '7db47d7c8468f07804e1f16c616f1b1e1a97f98c'),
  (GPU + 'ly00_01_mode0_2.gb', 12, '25962e2789012b949399e507eb2127a71cebbafb'),
  (GPU + 'ly00_mode0_2-GS.gb', 12, '599aca65b5055f35cf44ca42a82b9410ca4e6fcd'),
  (GPU + 'ly00_mode1_0-GS.gb', 12, 'cb59485cc8abe0624a3a9e7a0f1a73b73d0fb423'),
  (GPU + 'ly00_mode2_3.gb', 12, '3934e9c20cdb72aa31c36f495c90699067b71aa1'),
  (GPU + 'ly00_mode3_0.gb', 12, '56bfc0e101aa6e0d67864b18ffcf402b966b8beb'),
  (GPU + 'ly143_144_145.gb', 12, 'd0f1edc5479123386c09129bba004fd9b434433c'),
  (GPU + 'ly143_144_152_153.gb', 12, '5e9838ad4a052495b10d7924c5c2bd97b3bcb99f'),
  (GPU + 'ly143_144_mode0_1.gb', 6, 'df239da84d69b424b4a6dc810b15fbe9f4c1c5ea'),
  (GPU + 'ly143_144_mode3_0.gb', 6, 'c28a76dccba07a034401f11a4b8c7e67a27087b9'),
  (GPU + 'ly_lyc-GS.gb', 6, 'b6fe1abb95ad4e8cc8320c38c14f0802c8e0f618'),
  (GPU + 'ly_lyc_0-GS.gb', 24, '8025aa05fc0e2122c5e34194101947c240e5f44a'),
  (GPU + 'ly_lyc_0_write-GS.gb', 10, 'b760f10b54b8624c6c5a63527c0278082007287c'),
  (GPU + 'ly_lyc_144-GS.gb', 6, 'f27220994fdb33d69d0e0cbde01f91bbc9140167'),
  (GPU + 'ly_lyc_153-GS.gb', 12, 'd1218303206175b9a64363db5fbaf72b97d5d038'),
  (GPU + 'ly_lyc_153_write-GS.gb', 10, '737859a8866c8cd2929dcce4a74f257eb2ce5e51'),
  (GPU + 'ly_lyc_write-GS.gb', 10, 'dbb4741d3314417ae6edca0eaeaa838ffbf9b0e8'),
  (GPU + 'ly_new_frame-GS.gb', 12, '407bff6902e048c307868b1feddcdc4cfa79d3f8'),
  (GPU + 'stat_write_if-GS.gb', 300, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),
  (GPU + 'stat_irq_blocking.gb', 6, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),
  (GPU + 'vblank_if_timing.gb', 6, 'dbd9b17120938b29f38da7d6b0004afff70b0419'),
  (GPU + 'vblank_stat_intr-GS.gb', 6, '1f9d2db67edbce5952e0ad85cd28839aab7390c7'),

  (TIMER + 'div_write.gb', 42, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),
  (TIMER + 'rapid_toggle.gb', 1, 'b95fb07380632305e7edf30777064877198d64d8'),
  (TIMER + 'tim00_div_trigger.gb', 1, '245aea6bc6462ff810e4ed6e4398def1480613b8'),
  (TIMER + 'tim00.gb', 1, '245aea6bc6462ff810e4ed6e4398def1480613b8'),
  (TIMER + 'tim01_div_trigger.gb', 1, '5bd14cf7193a9c9d128c960779101b5111ce5a96'),
  (TIMER + 'tim01.gb', 1, 'a15c86b3ce77a1c5a0a4bf8d0b76ec9f039de0a7'),
  (TIMER + 'tim10_div_trigger.gb', 1, 'ccb709bd2f0db40783efc73e189dcfd8b53a491b'),
  (TIMER + 'tim10.gb', 1, '245aea6bc6462ff810e4ed6e4398def1480613b8'),
  (TIMER + 'tim11_div_trigger.gb', 1, '245aea6bc6462ff810e4ed6e4398def1480613b8'),
  (TIMER + 'tim11.gb', 1, '245aea6bc6462ff810e4ed6e4398def1480613b8'),
  (TIMER + 'tima_reload.gb', 1, '23c9a0b2139784fc457c39dc6b730702a85d506f'),
  (TIMER + 'tima_write_reloading.gb', 10, '240a3a3ca5c0aa62f323fc212bc7088c4369d316'),
  (TIMER + 'timer_if.gb', 1, '2ff8a38f7421feb1e86de059ed47fb908501ed8a'),
  (TIMER + 'tma_write_reloading.gb', 10, 'cd316e9257b1f954f09bf6f34a9a9deaedb3883d'),

  ('test/mooneye/emulator-only/mbc1_rom_4banks.gb', 1, 'd2c5e9902e751f0ac0dc9cd2438c9b54d76dc125'),
  ('test/mooneye/manual-only/sprite_priority.gb', 1, 'e055a00339efdde563ba1d0698809abfbed8ff82'),

  ('test/blargg/cpu_instrs/cpu_instrs.gb', 3200, 'd531b21af9b4589f456bf640a52edd4b0d845fd9'),
  ('test/blargg/dmg_sound-2/dmg_sound.gb', 2200, 'f68479b3c0de0e8d749a695541422bd29f62e1a3'),
  ('test/blargg/instr_timing/instr_timing.gb', 42, '98e64480d3637d51683cc0d5ae1a3fa266e634e3'),
  ('test/blargg/mem_timing-2/mem_timing.gb', 170, 'f2f2cd0a196587d7b77a762eafd6a26bd95a68c1'),
  ('test/blargg/oam_bug-2/oam_bug.gb', 1000, '!33fe95494d7eb02b36a4d19a1dbf949dcab25c1c'),
  ('test/blargg/halt_bug.gb', 102, 'bb6282f423d9a7e14f2ef61bd30c403a24648da1'),

  ('test/oam_count_v5.gb', 6, '9046cd1217fd36ef9ab715bd0983bcbd3d058c73'),
]


class Error(Exception):
  pass


def Run(exe, *args):
  cmd = [exe] + list(args)
  basename = os.path.basename(exe)
  try:
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE)
    stdout, stderr = process.communicate()
    if process.returncode != 0:
      raise Error('Error running "%s":\n%s' % (basename, stderr))
  except OSError as e:
    raise Error('Error running "%s": %s' % (basename, str(e)))


def HashFile(filename):
  m = hashlib.sha1()
  m.update(open(filename, 'rb').read())
  return m.hexdigest()


def RunTest(rom, frames, expected):
  ppm = os.path.basename(os.path.splitext(rom)[0]) + '.ppm'
  Run('out/tester', rom, str(frames), ppm)
  actual = HashFile(ppm)
  if expected.startswith('!'):
    expect_fail = True
    expected = expected[1:]
  else:
    expect_fail = False
  if actual == expected:
    if expect_fail:
      print('[X]  %s => %s' % (rom, actual))
    else:
      print('[OK] %s' % rom)
      os.remove(ppm)
      return True
  else:
    if expected == '' or expect_fail:
      print('[?]  %s => %s' % (rom, actual))
    else:
      print('[X]  %s => %s' % (rom, actual))
    return False


def main(args):
  parser = argparse.ArgumentParser()
  parser.add_argument('patterns', metavar='pattern', nargs='*',
                      help='test patterns.')
  options = parser.parse_args(args)
  if options.patterns:
    pattern_re = '|'.join(fnmatch.translate('*%s*' % p)
                          for p in options.patterns)
  else:
    pattern_re = '.*'
  total = 0
  passed = 0
  for rom, frames, expected in TESTS:
    if re.match(pattern_re, rom):
      total += 1
      if RunTest(rom, frames, expected):
        passed += 1
  print('Passed %d/%d' % (passed, total))
  if total == passed:
    return 0
  return 1

if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))

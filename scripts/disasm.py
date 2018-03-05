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
import struct
import sys

from common import Error


OPCODE_BYTES = [
    1, 3, 1, 1, 1, 1, 2, 1, 3, 1, 1, 1, 1, 1, 2, 1,
    1, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 3, 3, 3, 1, 2, 1, 1, 1, 3, 2, 3, 3, 2, 1,
    1, 1, 3, 0, 3, 1, 2, 1, 1, 1, 3, 0, 3, 0, 2, 1,
    2, 1, 1, 0, 0, 1, 2, 1, 2, 1, 3, 0, 0, 0, 2, 1,
    2, 1, 1, 1, 0, 1, 2, 1, 2, 1, 3, 1, 0, 0, 2, 1,
]

OPCODE_MNEMONIC = [
    "nop", "ld bc,%s", "ld [bc],a", "inc bc", "inc b", "dec b", "ld b,%u",
    "rlca", "ld [%s],sp", "add hl,bc", "ld a,[bc]", "dec bc", "inc c",
    "dec c", "ld c,%u", "rrca", "stop", "ld de,%s", "ld [de],a", "inc de",
    "inc d", "dec d", "ld d,%u", "rla", "jr %s", "add hl,de", "ld a,[de]",
    "dec de", "inc e", "dec e", "ld e,%u", "rra", "jr nz,%s", "ld hl,%s",
    "ld [hl+],a", "inc hl", "inc h", "dec h", "ld h,%u", "daa", "jr z,%s",
    "add hl,hl", "ld a,[hl+]", "dec hl", "inc l", "dec l", "ld l,%u", "cpl",
    "jr nc,%s", "ld sp,%s", "ld [hl-],a", "inc sp", "inc [hl]", "dec [hl]",
    "ld [hl],%u", "scf", "jr c,%s", "add hl,sp", "ld a,[hl-]", "dec sp",
    "inc a", "dec a", "ld a,%u", "ccf", "ld b,b", "ld b,c", "ld b,d",
    "ld b,e", "ld b,h", "ld b,l", "ld b,[hl]", "ld b,a", "ld c,b", "ld c,c",
    "ld c,d", "ld c,e", "ld c,h", "ld c,l", "ld c,[hl]", "ld c,a", "ld d,b",
    "ld d,c", "ld d,d", "ld d,e", "ld d,h", "ld d,l", "ld d,[hl]", "ld d,a",
    "ld e,b", "ld e,c", "ld e,d", "ld e,e", "ld e,h", "ld e,l", "ld e,[hl]",
    "ld e,a", "ld h,b", "ld h,c", "ld h,d", "ld h,e", "ld h,h", "ld h,l",
    "ld h,[hl]", "ld h,a", "ld l,b", "ld l,c", "ld l,d", "ld l,e", "ld l,h",
    "ld l,l", "ld l,[hl]", "ld l,a", "ld [hl],b", "ld [hl],c", "ld [hl],d",
    "ld [hl],e", "ld [hl],h", "ld [hl],l", "halt", "ld [hl],a", "ld a,b",
    "ld a,c", "ld a,d", "ld a,e", "ld a,h", "ld a,l", "ld a,[hl]", "ld a,a",
    "add a,b", "add a,c", "add a,d", "add a,e", "add a,h", "add a,l",
    "add a,[hl]", "add a,a", "adc a,b", "adc a,c", "adc a,d", "adc a,e",
    "adc a,h", "adc a,l", "adc a,[hl]", "adc a,a", "sub a,b", "sub a,c",
    "sub a,d", "sub a,e", "sub a,h", "sub a,l", "sub a,[hl]", "sub a,a",
    "sbc a,b", "sbc a,c", "sbc a,d", "sbc a,e", "sbc a,h", "sbc a,l",
    "sbc a,[hl]", "sbc a,a", "and a,b", "and a,c", "and a,d", "and a,e",
    "and a,h", "and a,l", "and a,[hl]", "and a,a", "xor a,b", "xor a,c",
    "xor a,d", "xor a,e", "xor a,h", "xor a,l", "xor a,[hl]", "xor a,a",
    "or a,b", "or a,c", "or a,d", "or a,e", "or a,h", "or a,l", "or a,[hl]",
    "or a,a", "cp a,b", "cp a,c", "cp a,d", "cp a,e", "cp a,h", "cp a,l",
    "cp a,[hl]", "cp a,a", "ret nz", "pop bc", "jp nz,%s", "jp %s",
    "call nz,%s", "push bc", "add a,%u", "rst $00", "ret z", "ret", "jp z,%s",
    None, "call z,%s", "call %s", "adc a,%u", "rst $08", "ret nc", "pop de",
    "jp nc,%s", None, "call nc,%s", "push de", "sub a,%u", "rst $10", "ret c",
    "reti", "jp c,%s", None, "call c,%s", None, "sbc a,%u", "rst $18",
    "ldh [%s],a", "pop hl", "ld [$ff00+c],a", None, None, "push hl", "and a,%u",
    "rst $20", "add sp,%d", "jp hl", "ld [%s],a", None, None, None, "xor a,%u",
    "rst $28", "ldh a,[%s]", "pop af", "ld a,[$ff00+c]", "di", None, "push af",
    "or a,%u", "rst $30", "ld hl,sp%+d", "ld sp,hl", "ld a,[%s]", "ei", None,
    None, "cp a,%u", "rst $38",
]

CB_OPCODE_MNEMONIC = [
    "rlc b",      "rlc c",   "rlc d",      "rlc e",   "rlc h",      "rlc l",
    "rlc [hl]",   "rlc a",   "rrc b",      "rrc c",   "rrc d",      "rrc e",
    "rrc h",      "rrc l",   "rrc [hl]",   "rrc a",   "rl b",       "rl c",
    "rl d",       "rl e",    "rl h",       "rl l",    "rl [hl]",    "rl a",
    "rr b",       "rr c",    "rr d",       "rr e",    "rr h",       "rr l",
    "rr [hl]",    "rr a",    "sla b",      "sla c",   "sla d",      "sla e",
    "sla h",      "sla l",   "sla [hl]",   "sla a",   "sra b",      "sra c",
    "sra d",      "sra e",   "sra h",      "sra l",   "sra [hl]",   "sra a",
    "swap b",     "swap c",  "swap d",     "swap e",  "swap h",     "swap l",
    "swap [hl]",  "swap a",  "srl b",      "srl c",   "srl d",      "srl e",
    "srl h",      "srl l",   "srl [hl]",   "srl a",   "bit 0,b",    "bit 0,c",
    "bit 0,d",    "bit 0,e", "bit 0,h",    "bit 0,l", "bit 0,[hl]", "bit 0,a",
    "bit 1,b",    "bit 1,c", "bit 1,d",    "bit 1,e", "bit 1,h",    "bit 1,l",
    "bit 1,[hl]", "bit 1,a", "bit 2,b",    "bit 2,c", "bit 2,d",    "bit 2,e",
    "bit 2,h",    "bit 2,l", "bit 2,[hl]", "bit 2,a", "bit 3,b",    "bit 3,c",
    "bit 3,d",    "bit 3,e", "bit 3,h",    "bit 3,l", "bit 3,[hl]", "bit 3,a",
    "bit 4,b",    "bit 4,c", "bit 4,d",    "bit 4,e", "bit 4,h",    "bit 4,l",
    "bit 4,[hl]", "bit 4,a", "bit 5,b",    "bit 5,c", "bit 5,d",    "bit 5,e",
    "bit 5,h",    "bit 5,l", "bit 5,[hl]", "bit 5,a", "bit 6,b",    "bit 6,c",
    "bit 6,d",    "bit 6,e", "bit 6,h",    "bit 6,l", "bit 6,[hl]", "bit 6,a",
    "bit 7,b",    "bit 7,c", "bit 7,d",    "bit 7,e", "bit 7,h",    "bit 7,l",
    "bit 7,[hl]", "bit 7,a", "res 0,b",    "res 0,c", "res 0,d",    "res 0,e",
    "res 0,h",    "res 0,l", "res 0,[hl]", "res 0,a", "res 1,b",    "res 1,c",
    "res 1,d",    "res 1,e", "res 1,h",    "res 1,l", "res 1,[hl]", "res 1,a",
    "res 2,b",    "res 2,c", "res 2,d",    "res 2,e", "res 2,h",    "res 2,l",
    "res 2,[hl]", "res 2,a", "res 3,b",    "res 3,c", "res 3,d",    "res 3,e",
    "res 3,h",    "res 3,l", "res 3,[hl]", "res 3,a", "res 4,b",    "res 4,c",
    "res 4,d",    "res 4,e", "res 4,h",    "res 4,l", "res 4,[hl]", "res 4,a",
    "res 5,b",    "res 5,c", "res 5,d",    "res 5,e", "res 5,h",    "res 5,l",
    "res 5,[hl]", "res 5,a", "res 6,b",    "res 6,c", "res 6,d",    "res 6,e",
    "res 6,h",    "res 6,l", "res 6,[hl]", "res 6,a", "res 7,b",    "res 7,c",
    "res 7,d",    "res 7,e", "res 7,h",    "res 7,l", "res 7,[hl]", "res 7,a",
    "set 0,b",    "set 0,c", "set 0,d",    "set 0,e", "set 0,h",    "set 0,l",
    "set 0,[hl]", "set 0,a", "set 1,b",    "set 1,c", "set 1,d",    "set 1,e",
    "set 1,h",    "set 1,l", "set 1,[hl]", "set 1,a", "set 2,b",    "set 2,c",
    "set 2,d",    "set 2,e", "set 2,h",    "set 2,l", "set 2,[hl]", "set 2,a",
    "set 3,b",    "set 3,c", "set 3,d",    "set 3,e", "set 3,h",    "set 3,l",
    "set 3,[hl]", "set 3,a", "set 4,b",    "set 4,c", "set 4,d",    "set 4,e",
    "set 4,h",    "set 4,l", "set 4,[hl]", "set 4,a", "set 5,b",    "set 5,c",
    "set 5,d",    "set 5,e", "set 5,h",    "set 5,l", "set 5,[hl]", "set 5,a",
    "set 6,b",    "set 6,c", "set 6,d",    "set 6,e", "set 6,h",    "set 6,l",
    "set 6,[hl]", "set 6,a", "set 7,b",    "set 7,c", "set 7,d",    "set 7,e",
    "set 7,h",    "set 7,l", "set 7,[hl]", "set 7,a",
]

CONTROL_OPCODES = {}
for op in [0x18, 0x20, 0x28, 0x30, 0x38]: CONTROL_OPCODES[op] = 'jr'
for op in [0xc2, 0xc3, 0xca, 0xd2, 0xda]: CONTROL_OPCODES[op] = 'jp'
for op in [0xc4, 0xcc, 0xcd, 0xd4, 0xdc]: CONTROL_OPCODES[op] = 'call'
for op in [0xc7, 0xcf, 0xd7, 0xdf, 0xe7, 0xef, 0xf7, 0xff]:
  CONTROL_OPCODES[op] = 'rst'


ADDR_OPCODES = {}
for op in [0x01, 0x08, 0x11, 0x21, 0x31, 0xea, 0xfa]: ADDR_OPCODES[op] = '4'
for op in [0xe0, 0xf0]: ADDR_OPCODES[op] = 'ff'
for op in [0x18, 0x20, 0x28, 0x30, 0x38]: ADDR_OPCODES[op] = 'jr'
for op in [0xc2, 0xc3, 0xca, 0xd2, 0xda]: ADDR_OPCODES[op] = 'jp'
for op in [0xc4, 0xcc, 0xcd, 0xd4, 0xdc]: ADDR_OPCODES[op] = 'call'

TERMINATOR_OPCODES = set([0x18, 0xc3, 0xc9, 0xd9, 0xe9])

UNCOMMON_OPCODES = set([
  0x00,  # nop
  0x10,  # stop
  0x27,  # daa
  0x40,  # ld b,b
  0x49,  # ld c,c
  0x52,  # ld d,d
  0x5b,  # ld e,e
  0x64,  # ld h,h
  0x6d,  # ld l,l
  0x7f,  # ld a,a
  0xc7,  # rst $0
  0xd9,  # reti
  0xf7,  # rst $30
  0xff,  # rst $38
])

KNOWN_ADDRS = {
  0x8000: '_VRAM',
  0x9800: '_SCRN0',
  0x9c00: '_SCRN1',
  0xc000: '_RAM',
  0xfe00: '_OAMRAM',
  0xff00: 'rP1',
  0xff01: 'rSB',
  0xff02: 'rSC',
  0xff04: 'rDIV',
  0xff05: 'rTIMA',
  0xff06: 'rTMA',
  0xff07: 'rTAC',
  0xff0f: 'rIF',
  0xff10: 'NR10',
  0xff11: 'NR11',
  0xff12: 'NR12',
  0xff13: 'NR13',
  0xff14: 'NR14',
  0xff16: 'NR21',
  0xff17: 'NR22',
  0xff18: 'NR23',
  0xff19: 'NR24',
  0xff1a: 'NR30',
  0xff1b: 'NR31',
  0xff1c: 'NR32',
  0xff1d: 'NR33',
  0xff1e: 'NR34',
  0xff20: 'NR41',
  0xff21: 'NR42',
  0xff22: 'NR43',
  0xff23: 'NR44',
  0xff24: 'NR50',
  0xff25: 'NR51',
  0xff26: 'NR52',
  0xff30: '_AUD3WAVERAM',
  0xff40: 'rLCDC',
  0xff41: 'rSTAT',
  0xff42: 'rSCY',
  0xff43: 'rSCX',
  0xff44: 'rLY',
  0xff45: 'rLYC',
  0xff46: 'rDMA',
  0xff47: 'rBGP',
  0xff48: 'rOBP0',
  0xff49: 'rOBP1',
  0xff4a: 'rWY',
  0xff4b: 'rWX',
  0xff80: '_HRAM',
  0xffff: 'rIE',
}


def ReadUsage(file, length):
  HEX = r'[\da-fA-F]'
  LOC = r'(%s{2}):(%s{4})' % (HEX, HEX)
  RE = r'^%s\.\.%s:\s+(.*)$' % (LOC, LOC)
  KINDS = {'Unknown': 0, 'Data': 2, 'Code': 3, 'Addr': 4}
  usage = bytearray(length)
  for i, line in enumerate(file.readlines()):
    m = re.match(RE, line)
    if m:
      bank0, addr0, bank1, addr1, kind = m.groups()
      loc0 = LocFromBankAddr(int(bank0, 16), int(addr0, 16))
      loc1 = LocFromBankAddr(int(bank1, 16), int(addr1, 16))
      if loc0 > loc1:
        raise Error('Invalid range %s:%s..%s:%s' % (bank0, addr0, bank1, addr1))
      if kind not in KINDS:
        raise Error('Invalid kind: %s' % kind)
      kind = KINDS[kind]
      for l in range(loc0, loc1 + 1):
        usage[l] = kind
    else:
      raise Error('Invalid line: %s' % line)
  return usage


def ReadSymbols(file):
  symbols = {}
  for line in file.readlines():
    m = re.match(r'^([\da-fA-F]{2}|xx):([\da-fA-F]{4})\s+(.*)$', line)
    if m:
      bank, addr, name = m.groups()
      if bank == 'xx':
        bank = -1
      else:
        bank = int(bank, 16)

      addr = int(addr, 16)
      if addr not in symbols:
        symbols[addr] = {}
      symbols[addr][bank] = name
  return symbols


def LocFromBankAddr(bank, addr):
  if bank == 0:
    return addr
  else:
    return (bank << 14) + (addr - 0x4000)


def AddrFromLoc(loc):
  bank = loc >> 14
  addr = (loc & 0x3fff) + (0 if bank == 0 else 0x4000)
  return bank, addr


def BankFromAddr(addr, src_bank):
  if addr < 0x4000:
    return 0
  elif src_bank > 0 and 0x4000 <= addr < 0x8000:
    return src_bank
  else:
    return -1


class ROM(object):
  def __init__(self, data, usage, symbols):
    self.data = data
    self.usage = usage
    assert len(usage) == len(data)
    self.targets = self.FindBranchTargets()
    for addr, name in KNOWN_ADDRS.items():
      self.targets[addr] = {-1: name}

    if symbols:
      for addr in symbols.keys():
        if addr not in self.targets:
          self.targets[addr] = {}
        self.targets[addr].update(symbols[addr])

  def ReadU8(self, loc):
    return self.data[loc]

  def ReadS8(self, loc):
    x = self.data[loc]
    if x >= 128: x = x - 256
    return x

  def ReadU16(self, loc):
    return (self.data[loc+1] << 8) | self.data[loc]

  def ReadOpcode(self, loc):
    opcode = self.ReadU8(loc)
    oplen = OPCODE_BYTES[opcode]
    return opcode, oplen

  def GetBranchTarget(self, loc):
    bank, addr = AddrFromLoc(loc)
    opcode, oplen = self.ReadOpcode(loc)
    kind = CONTROL_OPCODES.get(opcode)
    if not kind:
      return None, None

    if kind == 'jr':
      target_addr = addr + oplen + self.ReadS8(loc + 1)
    elif kind in ('jp', 'call'):
      target_addr = self.ReadU16(loc + 1)
    else:
      assert kind == 'rst'
      target_addr = opcode - 0xc7

    return BankFromAddr(target_addr, bank), target_addr

  def GetPointerTarget(self, loc):
    bank, _ = AddrFromLoc(loc)
    target_addr = self.ReadU16(loc)
    return BankFromAddr(target_addr, bank), target_addr

  def IsCode(self, loc):
    return not (self.IsData(loc) or self.IsAddr(loc))

  def IsData(self, loc):
    _, oplen = self.ReadOpcode(loc)
    usage = self.usage[loc:loc + oplen]
    return (oplen == 0 or
            usage[0] == 2 or
            (usage[0] == 0 and any(u != 0 for u in usage[1:])) or
            0x104 <= loc < 0x150)

  def IsAddr(self, loc):
    return self.usage[loc] == 4 and self.usage[loc+1] == 4

  def FindBranchTargets(self):
    loc = 0
    targets = {}
    while loc < len(self.data):
      if self.IsCode(loc):
        _, oplen = self.ReadOpcode(loc)
        target_bank, target_addr = self.GetBranchTarget(loc)
        loc += oplen
      elif self.IsAddr(loc):
        target_bank, target_addr = self.GetPointerTarget(loc)
        loc += 2
      else:
        loc += 1
        continue

      if target_addr:
        if target_addr not in targets:
          targets[target_addr] = {}

        if target_bank != -1:
          targets[target_addr][target_bank] = (
              'B%02u_%04x' % (target_bank, target_addr))
        else:
          targets[target_addr][-1] = 'Bxx_%04x' % target_addr

    return targets

  def GetAddrSymbol(self, bank, addr):
    if bank is not None:
      target = self.targets.get(addr)
      if target:
        if bank in target:
          return target[bank]
        elif -1 in target:
          return target[-1]
    return None

  def GetAddrText(self, bank, addr):
    symbol = self.GetAddrSymbol(bank, addr)
    if symbol is not None:
      return symbol
    return '$%04x' % addr

  def Disassemble(self, loc):
    opcode, oplen = self.ReadOpcode(loc)
    if oplen == 0:
      s = 'db $%02x' % opcode
    elif oplen == 1:
      s = OPCODE_MNEMONIC[opcode]
    elif oplen == 2:
      byte = self.ReadU8(loc + 1)
      if opcode == 0xcb:
        s = CB_OPCODE_MNEMONIC[byte]
      else:
        fmt = OPCODE_MNEMONIC[opcode]
        kind = ADDR_OPCODES.get(opcode)
        if kind:
          if kind in 'jr':
            target_bank, target_addr = self.GetBranchTarget(loc)
          else:
            assert(kind == 'ff')
            target_addr = 0xff00 + byte
            target_bank = -1
          name = self.GetAddrText(target_bank, target_addr)
          s = fmt % name
        else:
          s = fmt % byte
    elif oplen == 3:
      fmt = OPCODE_MNEMONIC[opcode]
      word = self.ReadU16(loc + 1)
      kind = ADDR_OPCODES.get(opcode)
      if kind:
        bank, _ = AddrFromLoc(loc)
        name = self.GetAddrText(BankFromAddr(word, bank), word)
        s = fmt % name
      else:
        s = fmt % word

    return s, opcode, oplen

  def DisassembleBank(self, file, bank):
    loc = bank << 14
    next_bank_loc = (bank + 1) << 14

    pending_data = []
    def FlushPendingData():
      nonlocal pending_data
      while pending_data:
        row = pending_data[:16]
        pending_data = pending_data[16:]
        file.write('  db %s\n' % ', '.join('$%02x' % x for x in row))

    was_terminator = False

    while loc < next_bank_loc:
      _, addr = AddrFromLoc(loc)
      symbol = self.GetAddrSymbol(bank, addr)
      if symbol:
        FlushPendingData()
        file.write('%s:\n' % symbol)
      elif was_terminator:
        FlushPendingData()
        file.write('; ?? %02x:%04x:\n' % (bank, addr))

      was_terminator = False

      if self.IsAddr(loc):
        FlushPendingData()
        file.write('  dw %s\n' % self.GetAddrText(*self.GetPointerTarget(loc)))
        loc += 2
      elif self.IsData(loc):
        pending_data.append(self.ReadU8(loc))
        loc += 1
      else:
        FlushPendingData()
        maybe_code = self.usage[loc] != 3
        s, opcode, oplen = self.Disassemble(loc)

        if opcode in UNCOMMON_OPCODES or oplen == 0:
          s += ' ; **TODO** '

        if oplen == 0: oplen += 1
        op_bytes = self.data[loc:loc+oplen]

        if maybe_code:
          file.write('  %s%s; %02x:%04x: db %s\n' % (
            s, ' ' * (36 - len(s)),
            bank, addr,
            ', '.join('$%02x' % x for x in op_bytes)))
        else:
          file.write('  %s\n' % s)

        if opcode in TERMINATOR_OPCODES:
          was_terminator = True
          file.write('\n')

        loc += oplen

    FlushPendingData()


def main(args):
  parser = argparse.ArgumentParser()
  parser.add_argument('-u', '--usage', metavar='FILE', help='usage file')
  parser.add_argument('-s', '--sym', metavar='FILE', help='sym file')
  parser.add_argument('-o', '--output', metavar='FILE', help='output file')
  parser.add_argument('rom', help='rom file')
  options = parser.parse_args(args)

  with open(options.rom, 'rb') as file:
    rom_data = bytearray(file.read())

  if options.usage:
    with open(options.usage, 'r') as file:
      rom_usage = ReadUsage(file, len(rom_data))
  else:
    rom_usage = '\x00' * len(rom_data)

  if options.sym:
    with open(options.sym, 'r') as file:
      symbols = ReadSymbols(file)
  else:
    symbols = None

  if options.output:
    outfile = open(options.output, 'w')
  else:
    outfile = sys.stdout

  rom = ROM(rom_data, rom_usage, symbols)
  banks = len(rom_data) >> 14
  for bank in range(banks):
    if bank != 0:
      outfile.write('\n')
    outfile.write('SECTION "Bank%d", ' % bank)
    if bank == 0:
      outfile.write('ROM0[$0000]')
    else:
      outfile.write('ROMX[$4000], BANK[%d]' % bank)
    outfile.write('\n\n')

    rom.DisassembleBank(outfile, bank)

  outfile.close()


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))

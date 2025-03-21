#!/usr/bin/python3
#coding: utf-8

import sys

font_file = sys.argv[1] if len(sys.argv) > 1 else 'ter-u16.bdf'

charlist = {}
char = []
into = False
enc = 0
for l in open(font_file, 'r').readlines():
  l = l.strip()
  if into:
    if l == 'ENDCHAR':
      charlist[enc] = char
      char = []
      into = False
    else:
        char += [int(l, 16)]
  elif l == 'BITMAP':
    into = True
  elif l.startswith('ENCODING'):
    enc = int(l[9:])

print('.global charmap')
print('.align 2')
print('charmap:')
print('// ASCII 32-126')
for c in range(32, 127):
  if c in charlist:
    data = charlist[c]
  else:
    data = [0] * 16
  print('    .byte', ', '.join([('0x%02X' % b) for b in data]))


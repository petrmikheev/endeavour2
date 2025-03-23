#!/usr/bin/python3
#coding: utf-8

import struct, sys

bytes_per_word = int(sys.argv[2])
fmt = '%%0%dX' % (bytes_per_word*2)

bytes = open(sys.argv[1], 'rb').read()
bytes += b'\0' * ((bytes_per_word - len(bytes) % bytes_per_word) % bytes_per_word)

assert len(bytes) % bytes_per_word == 0
count = len(bytes) // bytes_per_word
for c in range(count):
  v = 0
  for b in reversed(bytes[c*bytes_per_word:c*bytes_per_word+bytes_per_word]):
    v = (v << 8) + b
  print(fmt % v)

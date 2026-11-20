#!/usr/bin/env python3
"""Deja el ROM mas compatible: rellena hasta 2 MB y calcula el CRC de la cabecera (CIC-6102)."""
import struct, sys
src, dst = sys.argv[1], sys.argv[2]
d = bytearray(open(src, "rb").read())
size = max(0x200000, (len(d) + 0xFFFFF) & ~0xFFFFF)
d += b"\0" * (size - len(d))
M = 0xFFFFFFFF
def rol(v, n):
    n &= 31
    return ((v << n) | (v >> (32 - n))) & M if n else v
t1 = t2 = t3 = t4 = t5 = t6 = 0xF8CA4DDC
for i in range(0x1000, 0x101000, 4):
    c = struct.unpack(">I", d[i:i+4])[0]
    k = (t6 + c) & M
    if k < t6: t4 = (t4 + 1) & M
    t6 = k; t3 ^= c; r = rol(c, c); t5 = (t5 + r) & M
    t2 = (t2 ^ r) & M if t2 > c else (t2 ^ t6 ^ c) & M
    t1 = (t1 + (c ^ t5)) & M
d[0x10:0x18] = struct.pack(">II", t6 ^ t4 ^ t3, t5 ^ t2 ^ t1)
open(dst, "wb").write(d)
print("ROM", dst, len(d), "bytes, CRC %08X %08X" % (t6 ^ t4 ^ t3, t5 ^ t2 ^ t1))

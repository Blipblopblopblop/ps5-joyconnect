#!/usr/bin/env python3
"""
Patch ghost-toothAPI-134.elf to cap ACL credits at 5 (was 7).
This leaves headroom for Joy-Con connections and DualSense keep-alives.

Usage:  python3 patch_ghost_credits.py
Output: ghost-toothAPI-134-patched.elf
"""
import struct, sys, shutil

SRC  = '../GhostTooth/ghost-toothAPI-134.elf'
DST  = '../GhostTooth/ghost-toothAPI-134-patched.elf'

shutil.copy(SRC, DST)
data = bytearray(open(DST, 'rb').read())

# file_offset = RVA + 0x4000
# bt_start patches (RVA → file offset):
#   0x2d69 → 0x6d69 : movl $0x7, g_credits_max  (immediate bytes = 07 00 00 00 at +6)
#   0x2d73 → 0x6d73 : movl $0x7, g_credits
#   0x2d9d → 0x6d9d : mov  $0x7, %esi             (immediate byte at +1)
#   0x2dac → 0x6dac : movl $0x7, g_credits_max   (at +6)
#   0x2db6 → 0x6db6 : movl $0x7, g_credits       (at +6)

patches = [
    (0x6d6f + 6, b'\x07\x00\x00\x00', b'\x05\x00\x00\x00'),  # movl $7,g_credits_max (branch1)
    (0x6d73 + 6, b'\x07\x00\x00\x00', b'\x05\x00\x00\x00'),  # movl $7,g_credits (branch1)
    (0x6d9d + 1, b'\x07',              b'\x05'),               # mov $7,%esi
    (0x6dac + 6, b'\x07\x00\x00\x00', b'\x05\x00\x00\x00'),  # movl $7,g_credits_max (default)
    (0x6db6 + 6, b'\x07\x00\x00\x00', b'\x05\x00\x00\x00'),  # movl $7,g_credits (default)
]

ok = 0
for off, old, new in patches:
    actual = data[off:off+len(old)]
    if actual == old:
        data[off:off+len(new)] = new
        print(f'  patched 0x{off:x}: {old.hex()} → {new.hex()}')
        ok += 1
    else:
        print(f'  SKIP 0x{off:x}: expected {old.hex()} found {actual.hex()}')

open(DST, 'wb').write(data)
print(f'\n{ok}/{len(patches)} patches applied → {DST}')

# SPDX-License-Identifier: GPL-3.0-only
"""Find a 32-bit constant (error code, frame rate, size) in the code of a PS5 ELF from .cache/re.

usage: immsearch.py <ElfName> <hex-value>
Prints every place in the executable segments where the four little-endian bytes occur, with the function around it
(.eh_frame_hdr). Used for the 0x80c7xxxx error codes of libSceVenc and the frame-rate values 0xbb5/0x176a/0x2ed4
(docs/re/encoder-options.md). A hit can be data inside an instruction; read the function with funcdump.py.
"""
import sys, os, struct, bisect
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfdump import Elf, func_starts

CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".cache", "re")

if __name__ == "__main__":
    e = Elf(os.path.join(CACHE, sys.argv[1] + ".elf"))
    fs = func_starts(e)
    needle = struct.pack("<I", int(sys.argv[2], 16) & 0xFFFFFFFF)
    for p in e.ph:
        if p["type"] != 1 or not p["flags"] & 1:
            continue
        seg = e.d[p["off"]:p["off"] + p["fsz"]]
        i = seg.find(needle)
        while i >= 0:
            va = p["va"] + i
            k = bisect.bisect_right(fs, va) - 1
            print("%#x in func %#x" % (va, fs[k] if k >= 0 else 0))
            i = seg.find(needle, i + 1)

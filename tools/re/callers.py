# SPDX-License-Identifier: GPL-3.0-only
"""List the direct calls and jumps (E8/E9 rel32) to one or more addresses of a PS5 ELF from .cache/re.

usage: callers.py <ElfName> <hex-addr> [<hex-addr> ...]
The scan is byte-wise over the executable segments, so a hit inside another instruction's bytes is possible; check each
one with funcdump.py. Calls through the PLT or a vtable are not found here (disasm.py annotates the PLT ones).
"""
import sys, os, struct, bisect
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfdump import Elf, func_starts

CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".cache", "re")

if __name__ == "__main__":
    e = Elf(os.path.join(CACHE, sys.argv[1] + ".elf"))
    fs = func_starts(e)
    for target in (int(a, 16) for a in sys.argv[2:]):
        for p in e.ph:
            if p["type"] != 1 or not p["flags"] & 1:
                continue
            seg = e.d[p["off"]:p["off"] + p["fsz"]]
            for j in range(len(seg) - 5):
                if seg[j] not in (0xE8, 0xE9):
                    continue
                (disp,) = struct.unpack_from("<i", seg, j + 1)
                if p["va"] + j + 5 + disp == target:
                    va = p["va"] + j
                    i = bisect.bisect_right(fs, va) - 1
                    print("%#x: %s at %#x in func %#x" % (target, "call" if seg[j] == 0xE8 else "jmp", va, fs[i] if i >= 0 else 0))

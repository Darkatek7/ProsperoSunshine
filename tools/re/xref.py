# SPDX-License-Identifier: GPL-3.0-only
"""Find code that refers to an address or a string through a RIP-relative operand (lea/mov/cmp ...).

usage: xref.py <ElfName> <hex-addr | "text">
A text argument is looked up as a NUL-terminated string first; every place where it occurs is searched. The scan is
byte-wise over the executable segments: a ModRM byte with mod=00, rm=101 followed by a disp32 that points at the target,
taking the next instruction to start right after the displacement (true for lea and plain mov; for an instruction with an
immediate after the displacement the hit is missed). Prints the function start (.eh_frame_hdr) for each hit.
"""
import sys, os, struct, bisect
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfdump import Elf, func_starts

CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".cache", "re")

def targets(e, arg):
    try:
        return [int(arg, 16)]
    except ValueError:
        pass
    needle = arg.encode() + b"\0"
    out = []
    for p in e.ph:
        if p["type"] != 1:
            continue
        seg = e.d[p["off"]:p["off"] + p["fsz"]]
        i = seg.find(needle)
        while i >= 0:
            if i == 0 or seg[i - 1] == 0:
                out.append(p["va"] + i)
            i = seg.find(needle, i + 1)
    return out

def xrefs(e, target):
    hits = []
    for p in e.ph:
        if p["type"] != 1 or not p["flags"] & 1:
            continue
        seg = e.d[p["off"]:p["off"] + p["fsz"]]
        for j in range(1, len(seg) - 4):
            if seg[j - 1] & 0xC7 != 0x05:
                continue
            (disp,) = struct.unpack_from("<i", seg, j)
            if p["va"] + j + 4 + disp == target:
                hits.append(p["va"] + j - 1)
    return hits

if __name__ == "__main__":
    e = Elf(os.path.join(CACHE, sys.argv[1] + ".elf"))
    fs = func_starts(e)
    for t in targets(e, sys.argv[2]):
        for h in xrefs(e, t):
            i = bisect.bisect_right(fs, h) - 1
            print("target %#x  ref ~%#x  in func %#x" % (t, h, fs[i] if i >= 0 else 0))

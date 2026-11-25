#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Did every frame the client kept have the picture it is decoded from?

For an HEVC stream that stream_test recorded with --rfi and --timing-log while packets were dropped on the way
(loss_test.sh). After a loss the client drops frames until the host sends one marked as coming after the invalidation,
and the dump holds only the frames it kept. This reads each kept frame's references from its slice header, as the host
does (src/video/hevc_refs.hpp), and checks that the frame it is decoded from is in the dump and was itself decodable,
and that no more pictures are missing in front of it than a decoder can count over. A frame that fails would have shown
a broken picture until the next keyframe.

Frame numbers come from the timing log; a stream counts every frame the encoder made for it, so a reference "two
pictures back" is the frame numbered two lower.

Usage: rfi_verify.py <dump.h265> <timing.csv>
Exit code 1 when a kept frame had no reference, 2 when the files don't fit together or the stream's layout isn't the
console's.
"""
import sys


def rbsp(nal, limit):
    out, zeros = bytearray(), 0
    for byte in nal:
        if len(out) >= limit:
            break
        if zeros >= 2 and byte == 3:
            zeros = 0
            continue
        zeros = zeros + 1 if byte == 0 else 0
        out.append(byte)
    return bytes(out)


class Bits:
    def __init__(self, data):
        self.data, self.at = data, 0

    def bits(self, count):
        value = 0
        for _ in range(count):
            byte = self.at >> 3
            if byte >= len(self.data):
                raise ValueError("header cut short")
            value = (value << 1) | ((self.data[byte] >> (7 - (self.at & 7))) & 1)
            self.at += 1
        return value

    def flag(self):
        return self.bits(1) == 1

    def ue(self):
        zeros = 0
        while not self.flag():
            zeros += 1
            if zeros > 31:
                raise ValueError("no Exp-Golomb value")
        return (1 << zeros) - 1 + self.bits(zeros) if zeros else 0


def nals(data):
    """(type, payload without the two header bytes) for each NAL unit of an Annex B buffer."""
    starts, i = [], 0
    while True:
        i = data.find(b"\x00\x00\x01", i)
        if i < 0:
            break
        starts.append(i + 3)
        i += 3
    for n, start in enumerate(starts):
        end = starts[n + 1] - 3 if n + 1 < len(starts) else len(data)
        while end > start and data[end - 1] == 0:
            end -= 1
        if end - start >= 2:
            yield (data[start] >> 1) & 0x3f, data[start + 2:end]


def layout(frame):
    """What a slice header's layout depends on, from the parameter sets in front of a keyframe; None for one this
    doesn't read."""
    found = {}
    for kind, payload in nals(frame[:600]):
        if kind == 33 and "poc_bits" not in found:
            b = Bits(rbsp(payload, 512))
            b.bits(4)
            if b.bits(3) != 0:
                return None
            b.bits(1)
            b.bits(88)
            b.bits(8)
            b.ue()
            chroma = b.ue()
            found["planes"] = b.flag() if chroma == 3 else False
            b.ue()
            b.ue()
            if b.flag():
                for _ in range(4):
                    b.ue()
            b.ue()
            b.ue()
            poc = b.ue()
            b.flag()
            for _ in range(9):
                b.ue()
            if b.flag() and b.flag():
                return None
            b.flag()
            b.flag()
            if b.flag():
                b.bits(8)
                b.ue()
                b.ue()
                b.flag()
            if b.ue() != 0 or b.flag() or poc > 12:
                return None
            found["poc_bits"] = poc + 4
        elif kind == 34 and "extra" not in found:
            b = Bits(rbsp(payload, 64))
            b.ue()
            b.ue()
            b.flag()
            found["output"] = b.flag()
            found["extra"] = b.bits(3)
    return found if "poc_bits" in found and "extra" in found else None


def refs(lay, frame):
    """(keyframe, order, used distances, kept distances) of a frame's first slice, None when it can't be read."""
    for kind, payload in nals(frame):
        if kind >= 32:
            continue
        if kind > 21 or 9 < kind < 16:
            return None
        try:
            b = Bits(rbsp(payload, 96))
            if not b.flag():
                return None
            if kind >= 16:
                b.flag()
            b.ue()
            b.bits(lay["extra"])
            if b.ue() > 2:
                return None
            if lay["output"]:
                b.flag()
            if lay["planes"]:
                b.bits(2)
            if kind in (19, 20):
                return True, 0, [], []
            order = b.bits(lay["poc_bits"])
            if b.flag():
                return None
            negative, positive = b.ue(), b.ue()
            if negative > 16 or positive != 0:
                return None
            used, kept, distance = [], [], 0
            for _ in range(negative):
                distance -= b.ue() + 1
                (used if b.flag() else kept).append(distance)
            return False, order, used, kept
        except ValueError:
            return None
    return None


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    rows = []
    with open(sys.argv[2]) as f:
        for line in f:
            part = line.strip().split(",")
            if part[0] == "V" and len(part) >= 6:
                rows.append((int(part[1]), int(part[2]), int(part[5])))
    if not rows or sum(size for _, _, size in rows) != len(data):
        print(f"the timing log accounts for {sum(size for _, _, size in rows)} bytes in {len(rows)} frames, the dump has "
              f"{len(data)}: not from the same run")
        sys.exit(2)
    lay = layout(data[:rows[0][2]])
    if lay is None:
        print("the stream's parameter sets are none this reads (or the first frame is no keyframe)")
        sys.exit(2)
    most_missing = (1 << (lay["poc_bits"] - 1)) - 1
    at = 0
    decodable = set()
    last = last_order = None
    gaps = went_on_idr = went_on_p = bad = unread = 0
    lost_frames = 0
    in_broken_run = False
    worst = []
    histogram = {}
    for number, idr, size in rows:
        frame = data[at:at + size]
        at += size
        r = refs(lay, frame)
        gap = number - last - 1 if last is not None else 0
        if gap:
            gaps += 1
            lost_frames += gap
        if r is None:
            unread += 1
            if len(worst) < 12:
                worst.append(f"frame {number}: slice header not read")
            last = number
            continue
        keyframe, order, used, kept = r
        if keyframe:
            decodable.add(number)
            in_broken_run = False
            if gap:
                went_on_idr += 1
            last, last_order = number, None
            continue
        problem = None
        if not used:
            problem = "no reference"
        else:
            missing = [number + d for d in used if number + d not in decodable]
            if missing:
                problem = f"decoded from frame {', '.join(map(str, missing))}, which the client didn't have"
            elif gap > most_missing:
                problem = f"{gap} pictures missing in front of it, a decoder counts over {most_missing} at most"
            elif last_order is not None and (order - last_order) % (1 << lay["poc_bits"]) != (number - last) % (1 << lay["poc_bits"]):
                problem = f"its picture order count {order} doesn't follow frame {last}'s {last_order} by {number - last}"
        if gap:
            went_on_p += 1
            histogram[gap] = histogram.get(gap, 0) + 1
        if problem:
            bad += 1
            # Every frame behind a broken one is broken too, until the next keyframe: only the first is listed.
            if not in_broken_run and len(worst) < 12:
                worst.append(f"frame {number} (after {gap} missing, references {used}): {problem}")
            in_broken_run = True
        else:
            decodable.add(number)
            in_broken_run = False
        last, last_order = number, order
    print(f"{len(rows)} frames kept, {gaps} gaps ({lost_frames} frames missing); the stream went on from a keyframe {went_on_idr} "
          f"times, from a P frame {went_on_p} times")
    if histogram:
        print("  frames missing in front of the P frame it went on from: " +
              ", ".join(f"{gap}: {count}x" for gap, count in sorted(histogram.items())))
    for line in worst:
        print("  ! " + line)
    if unread:
        print(f"  ! {unread} frames whose header wasn't read")
    if bad or unread:
        print(f"FAIL: {bad} kept frames without their reference")
        sys.exit(1)
    print("OK: every kept frame had the picture it is decoded from")


if __name__ == "__main__":
    main()

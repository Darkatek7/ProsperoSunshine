#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reads what rfi_probe.elf left in /data/prosperosunshine/rfi_probe (src/probes/rfi_probe.cpp): does the encoder get
over a loss without a keyframe once it was told which frame was lost?

For each call the probe made, the frames that counted as lost are taken out of the stream, and then one frame after
the other behind them, until the pictures decoded from the rest are the same as those of the whole stream. That first
frame is where a client could go on. If it is no keyframe, the call worked. The same is done at a frame where the
probe called nothing (the control): there the pictures must stay different until the next keyframe, or the picture
didn't move enough for the test to say anything.

Measured on 13.00 (2026-11-25): HEVC goes on at the very next frame after the call, which references the newest
picture before the loss, as long as fewer than 8 pictures are missing (the stream's picture order count has 4 bits).
H.264 keeps one reference frame and the call changes nothing.

Needs ffmpeg on the PATH. Usage: rfi_check.py <folder> [--trace]
--trace also writes <variant>.trace.txt (ffmpeg's trace_headers: every field of every slice header) and prints the
reference fields of the frames around each call.
"""
import csv
import os
import re
import statistics
import subprocess
import sys
import tempfile

WINDOW = 40  # frames compared behind the gap
REACH = 12  # how many frames behind the loss are tried as the first one kept
CONTROL = (50, 3)  # a loss nobody told the encoder about


def decode_md5(stream, codec):
    """The MD5 of every picture ffmpeg decodes from an elementary stream, and how many lines it complained."""
    with tempfile.NamedTemporaryFile(suffix="." + codec) as f:
        f.write(stream)
        f.flush()
        # output_corrupt, as Moonlight's decoder is set up: without it ffmpeg keeps back every picture whose reference
        # set names a missing one, also when the picture itself uses none of them (HEVC after an invalidation).
        run = subprocess.run(["ffmpeg", "-v", "warning", "-nostdin", "-flags", "+output_corrupt", "-f", codec, "-i", f.name,
                              "-f", "framemd5", "-"], capture_output=True, text=True)
    md5 = [line.rsplit(",", 1)[1].strip() for line in run.stdout.splitlines() if line and not line.startswith("#")]
    return md5, len([line for line in run.stderr.splitlines() if line.strip()])


def load(folder, name):
    with open(os.path.join(folder, name + ".csv")) as f:
        first = f.readline()
        headers = int(re.search(r"headers=(\d+)", first).group(1))
        codec = re.search(r"codec=(\w+)", first).group(1)
        rows = list(csv.DictReader(f))
    with open(os.path.join(folder, name + (".h265" if codec == "hevc" else ".h264")), "rb") as f:
        data = f.read()
    frames, at = [], headers
    for row in rows:
        size = int(row["bytes"])
        frames.append(data[at:at + size])
        at += size
    if at != len(data):
        print(f"  ! the stream has {len(data)} bytes, the table accounts for {at}: cut short, or not from the same run")
    return codec, data[:headers], frames, rows


def first_clean(codec, head, frames, full, at, lost):
    """Frames at-lost+1..at are lost. Returns (k, mismatches per k): frame at+k is the first one from which the decoded
    pictures match the whole stream's, None if none within REACH."""
    tried = []
    for k in range(1, REACH + 1):
        keep_from = at + k  # 1-based frame number
        if keep_from + 5 > len(frames):
            break
        gapped = head + b"".join(frames[:at - lost]) + b"".join(frames[keep_from - 1:])
        md5, complaints = decode_md5(gapped, codec)
        # The pictures behind the gap, counted from the end: a decoder may give fewer pictures before it.
        behind = len(frames) - keep_from + 1
        window = min(WINDOW, behind)
        mismatches = 0
        for i in range(window):
            src = keep_from - 1 + i  # index into `full`
            out = len(md5) - behind + i
            if out < 0 or out >= len(md5) or src >= len(full) or md5[out] != full[src]:
                mismatches += 1
        tried.append((k, mismatches, window, complaints))
        if mismatches == 0:
            return k, tried
    return None, tried


TRACE_FIELDS = {
    "h264": ["nal_unit_type", "slice_type", "frame_num", "idr_pic_id", "pic_order_cnt_lsb", "num_ref_idx_active_override_flag",
             "num_ref_idx_l0_active_minus1", "ref_pic_list_modification_flag_l0", "modification_of_pic_nums_idc",
             "abs_diff_pic_num_minus1", "long_term_pic_num", "long_term_reference_flag", "adaptive_ref_pic_marking_mode_flag",
             "memory_management_control_operation", "difference_of_pic_nums_minus1", "long_term_frame_idx"],
    "hevc": ["nal_unit_type", "slice_type", "slice_pic_order_cnt_lsb", "short_term_ref_pic_set_sps_flag",
             "short_term_ref_pic_set_idx", "num_negative_pics", "delta_poc_s0_minus1", "used_by_curr_pic_s0_flag",
             "num_long_term_pics", "num_long_term_sps", "used_by_curr_pic_lt_flag", "num_ref_idx_active_override_flag",
             "num_ref_idx_l0_active_minus1"],
}


def trace(folder, name, codec, head, frames):
    """ffmpeg's trace_headers of the whole stream, split per frame: a list of {field: [values]}."""
    with tempfile.NamedTemporaryFile(suffix="." + codec) as f:
        f.write(head + b"".join(frames))
        f.flush()
        run = subprocess.run(["ffmpeg", "-v", "verbose", "-nostdin", "-f", codec, "-i", f.name, "-c", "copy", "-bsf:v", "trace_headers",
                              "-f", "null", "-"], capture_output=True, text=True)
    lines = [line for line in run.stderr.splitlines() if "trace_headers" in line]
    with open(os.path.join(folder, name + ".trace.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    wanted = TRACE_FIELDS[codec]
    packets, current = [], None
    for line in lines:
        text = line.split("] ", 1)[-1]
        if text.startswith("Packet:"):
            current = {}
            packets.append(current)
            continue
        if current is None:
            continue
        m = re.match(r"\d+\s+([A-Za-z0-9_\[\]]+)\s+[01]*\s*=\s*(-?\d+)", text)
        if m and re.sub(r"\[.*", "", m.group(1)) in wanted:
            current.setdefault(re.sub(r"\[.*", "", m.group(1)), []).append(m.group(2))
    return packets


def check(folder, name, with_trace):
    codec, head, frames, rows = load(folder, name)
    full, complaints = decode_md5(head + b"".join(frames), codec)
    sizes = [int(r["bytes"]) for r in rows]
    idr = [i + 1 for i, r in enumerate(rows) if r["type"] == "1"]
    p_sizes = [s for s, r in zip(sizes, rows) if r["type"] != "1"]
    changes = sum(1 for a, b in zip(full, full[1:]) if a != b)
    print(f"\n== {name}: {codec}, {len(frames)} frames, {len(full)} pictures decoded ({complaints} complaints), keyframes at {idr}")
    print(f"   sizes: P median {int(statistics.median(p_sizes)) if p_sizes else 0} B, keyframes "
          f"{[sizes[i - 1] for i in idr]} B; the picture changed in {changes} of {max(1, len(full) - 1)} steps")
    if len(full) != len(frames):
        print("   ! not one picture per frame: the frame numbers below may be off")
    if changes < len(full) * 0.8:
        print("   ! the picture stood still for much of the run: a frame without its reference may decode the same")
    counters = [int(r["counter"]) for r in rows]
    if any(b != a + 1 for a, b in zip(counters, counters[1:])):
        print(f"   ! the frame counter doesn't rise by one everywhere: {[c for a, c in zip(counters, counters[1:]) if c != a + 1][:8]}")
    points = [("control (no call)", CONTROL[0], CONTROL[1], "")]
    for i, r in enumerate(rows):
        if r["event"]:
            lost = 1 if r["event"] == "keyframe" else int(r["counter"]) - int(r["arg"]) + 1
            points.append((f"{r['event']}({r['arg']}) rc={r['rc']}", i + 1, lost, r["event"]))
    packets = trace(folder, name, codec, head, frames) if with_trace else None
    for label, at, lost, event in sorted(points, key=lambda p: p[1]):
        k, tried = first_clean(codec, head, frames, full, at, lost)
        nearby = [f"{n}:{'I' if rows[n - 1]['type'] == '1' else 'P'}{sizes[n - 1]}" for n in range(at - 1, min(len(frames), at + 8) + 1)]
        print(f"   frame {at}, {label}, frames {at - lost + 1}..{at} lost")
        print(f"     frame:type+bytes  {' '.join(nearby)}")
        print(f"     kept from +k: mismatches of window  {' '.join(f'+{t[0]}:{t[1]}/{t[2]}' for t in tried)}")
        if k is None:
            print(f"     -> no frame within {REACH} decodes clean without the lost ones")
        else:
            first = at + k
            kind = "a keyframe" if rows[first - 1]["type"] == "1" else "NOT a keyframe"
            print(f"     -> clean from frame {first} (+{k}), {kind}, {sizes[first - 1]} B, descriptor type {rows[first - 1]['type']}")
        if packets and len(packets) >= len(frames):
            offset = len(packets) - len(frames)
            for n in range(at, min(len(frames), at + 6) + 1):
                print(f"     trace {n}: " + " ".join(f"{key}={','.join(v)}" for key, v in packets[n - 1 + offset].items()))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        sys.exit(__doc__)
    folder = args[0]
    names = sorted(f[:-4] for f in os.listdir(folder) if f.endswith(".csv"))
    if not names:
        sys.exit("no .csv in " + folder)
    for name in names:
        check(folder, name, "--trace" in sys.argv)


if __name__ == "__main__":
    main()

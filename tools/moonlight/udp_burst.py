#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Counts what arrives of src/probes/udp_burst_spike.cpp's frame-sized bursts, to tell network loss from host problems.

usage: udp_burst.py <console> [packets per burst ...]
"""
import socket
import struct
import sys

host = sys.argv[1]
sizes = [int(x) for x in sys.argv[2:]] or [60, 100, 150, 250]
bursts = 180
for packets in sizes:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 * 1024 * 1024)
    s.settimeout(3)
    s.sendto(struct.pack("<II", packets, bursts), (host, 47901))
    counts = {}
    try:
        while True:
            data = s.recv(2048)
            burst, _ = struct.unpack_from("<II", data)
            if burst == 0xFFFFFFFF:
                break
            counts[burst] = counts.get(burst, 0) + 1
    except socket.timeout:
        print("  (timed out)")
    got = sum(counts.values())
    short = sum(1 for b in range(bursts) if counts.get(b, 0) < packets)
    print(f"{packets} packets per burst: {got}/{packets * bursts} received, {short}/{bursts} bursts incomplete, "
          f"worst burst {min((counts.get(b, 0) for b in range(bursts)), default=0)}, buffer {s.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)}")
    s.close()

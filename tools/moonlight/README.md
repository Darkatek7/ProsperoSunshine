<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Moonlight test client

Automated, Moonlight-compatible client for checking a ProsperoSunshine host on a console from the PC.

- `pair.py` - GameStream pairing and HTTPS client (Python 3, `cryptography`). Follows moonlight-qt's `nvpairingmanager.cpp`.
- `stream_test.c` - streaming smoke test on top of moonlight-common-c (RTSP, video, audio, control, input).
- `build.sh` - builds moonlight-common-c (pinned source in `.cache/deps/src`) and `stream_test` in the SDK container.
- `build_windows.sh` - cross-compiles `stream_test.exe` in the same container, statically, from the same pinned sources.
- `run_smoke.sh` - one command: `/launch`, run `stream_test`, verify with ffmpeg, `/cancel`.
- `start_series.sh` - many short streams in a row, to catch rare failed starts.

The host must be running (ELF loaded) and the dev control socket (TCP 47995) reachable. Pass the console address as the first argument or set `PS5_HOST`.
The client identity (key, certificate, unique id, pinned server certificate) is kept in `out/moonlight-client/`.

## Pair

```sh
py -3 tools/moonlight/pair.py pair            # random PIN, approved through the control socket
py -3 tools/moonlight/pair.py info            # /serverinfo + /applist over HTTPS, expects PairStatus=1
py -3 tools/moonlight/pair.py negative        # a fresh, unpaired certificate must be rejected
py -3 tools/moonlight/pair.py cover --appid 1 # the app's box art must be a PNG; --out saves it
```

`pair` starts `phrase=getservercert` in a thread (it blocks until a PIN is approved), sends `PIN <pin> <name>` to the
control socket, then completes the challenge/response steps and `pairchallenge` over HTTPS, verifying the host's
signature and PIN hash. Use `--manual-pin` to approve the PIN yourself, `--host-pin`/`--no-verify` for the wrong-PIN
test, `--host` for another address, `--dir` for another identity directory.

## Smoke test

```sh
bash tools/moonlight/build.sh                         # once; needs Docker image prosperosunshine-sdk:v0.45
bash tools/moonlight/run_smoke.sh <console-ip> 20       # host, seconds
bash tools/moonlight/run_smoke.sh <console-ip> 20 --corever   # also use encrypted RTSP (rtspenc://), like Moonlight
```

Outputs: `out/moonlight-test.h264` (received stream), `out/moonlight-test.s16` (decoded audio, 48 kHz stereo s16le),
`out/moonlight/stream_test.log`. The script prints frame/IDR/audio counts, then `ffprobe -count_frames` and
`ffmpeg -xerror` results for the file. Exit code is non-zero if the connection failed, no frame arrived, or the host
terminated the stream with an error. A failed connection reports the failing stage and error code.

## Loss, and going on without a keyframe

`loss_test.sh <console-ip> <seconds> <drop probability>` streams while the client's container drops that share of the
incoming video packets; `STREAM_ARGS` passes options on to the client as in the smoke test.

With `--rfi` the client says its decoder can go on from a frame that follows a reference frame invalidation, as
Moonlight does for most HEVC decoders. Where the host announces that too (`reference_frame_invalidation = 1` in
`sunshine.conf`), the client names the frames it lost instead of asking for an IDR, and drops what arrives until the
host sends a frame marked as coming after the invalidation. The summary's `rfi` line counts how the stream went on
after each loss (from a marked P frame, from an IDR), the complete frames dropped while waiting, and whether the client
gave up waiting (120 dropped frames).

```sh
STREAM_ARGS="--hevc --rfi --no-script --timing-log out/rfi.csv" VIDEO_FORMAT=hevc bash tools/moonlight/loss_test.sh <console-ip> 30 0.03
python3 tools/moonlight/rfi_verify.py out/moonlight-test.h264 out/rfi.csv
```

`rfi_verify.py` reads each frame the client kept from the dump, with its number from the timing log, and its
references from the slice header as the host does (`src/video/hevc_refs.hpp`): the frame it is decoded from must be in
the dump and decodable itself, and no more frames may be missing in front of it than a decoder counts over (7 with the
console's stream). `OK` means the client never showed a picture built on one it didn't have; a failure names the first
frame of each broken run. It needs the console's HEVC layout and Python 3, nothing else.

## Audio/video synchronisation (ROADMAP P5.2)

`av_sync.py` measures how far apart picture and sound arrive at the client for one real event, and the long-term drift.
The PS5 home menu moves its focus highlight and plays a navigation click when D-pad left/right is pressed; that is the
simultaneous picture+sound event. The home screen must be in front (the script does not launch titles, and it passes
`--no-script` because the scripted startup input presses A, which would start the focused tile).

```sh
py -3 tools/moonlight/av_sync.py --host <console-ip> --duration 60 --ticks 30     # capture (run_smoke.sh) + analysis
py -3 tools/moonlight/av_sync.py --analyze-only                                    # re-analyse the last capture
py -3 tools/moonlight/av_sync.py --kind ps ...                                     # PS button (control centre) instead of D-pad
```

HEVC: `STREAM_ARGS="--hevc" VIDEO_FORMAT=hevc tools/moonlight/run_smoke.sh`. The output file keeps its `.h264` name.

Prefer the Windows client: run `bash tools/moonlight/build_windows.sh` once, and `run_smoke.sh` and the scripts on top of it
use it from then on. The ffmpeg check still runs in the container. Scripts that shape packets inside the container
(`loss_test.sh`, through `DOCKER_ARGS` and `PRE_CMD`) keep using the Linux client; `NATIVE=0` asks for it elsewhere.

**Limit of this client in Docker:** Docker Desktop's network drops large bursts. Frames above about 70–85 packets arrive
incomplete, so the stream stalls at bitrates above roughly 30 Mbit/s with motion, although the console sends them
correctly. `udp_burst.py` measures it against `udp_burst_spike.elf`; run it natively and in the container to compare.
Use the Windows client for anything about loss or high bitrates.

`stream_test` gained `--ui-ticks N [--tick-interval-ms 1500] [--tick-hold-ms 120] [--tick-kind dpad|ps|back] [--tick-seq left,cross,...] [--tick-start S] [--no-script]` and
`--timing-log <csv>`. Without them nothing changes. The log has one line per received video frame
(`V,frame,idr,arrival_us,presentationTimeUs,bytes`), audio packet (`A,index,arrival_us,rms,peak`) and press
(`P,tick,action,sent_us`) on a single CLOCK_MONOTONIC microsecond clock (arrival = when the moonlight-common-c callback
ran, i.e. after reassembly/FEC, so network jitter affects both streams alike).

`av_sync.py` decodes the h264/s16 files in the SDK container with ffmpeg filters only (no numpy): per decoded frame the mean
absolute luma difference to the previous frame (`tblend=difference`, `signalstats`, 480x270, region `--roi`), and the
level of the audio after a 1.5 kHz high-pass per 5 ms packet. A visual event is the first frame after a press whose
difference exceeds median + 8 MAD of the run; an audio onset is the first packet whose high-passed power is 12 dB above
the mean of the previous 200 ms. It prints per press: press to picture (input-to-photon at the client), press to sound,
and sound minus picture; median/min/max/sd and the trend over the run; and, without any events, the slope of both
streams' arrival times against their own timeline (video `presentationTimeUs`, audio 5 ms per packet), which is the
clock drift of the host streams against the client clock. Events with no detectable change are listed and excluded.

The picture detector skips keyframes (once a second) and the frame after them, since a keyframe changes the whole
picture a little. Disconnect other virtual pads first (`ps5_detach` for RemoteEye): the console's own UI takes input
from every pad, but the app was seen to read only the one connected first. To measure inside the app, limit `--roi` to
the region that changes.

Caveats: times are client arrival times, not presentation times. Video is quantised to the 16.7 ms frame interval; the
host sends audio in FEC blocks of 4 x 5 ms packets, so audio arrives in bursts every ~20 ms (the script also prints a
burst-aligned estimate that subtracts 5 ms per packet still to come in the burst). The press time is when
`LiSendMultiControllerEvent` returned on the client; the real input path adds network and host latency.

Manual steps: `pair.py launch` prints JSON (`sessionUrl0`, `rikey`, `rikeyid`, `appversion`, `gfe`, `codec_mode`) that
`stream_test` takes as arguments (`stream_test --help` lists them); `pair.py cancel` ends the host session. With
`close_game_on_quit = enabled` in `sunshine.conf`, that `/cancel`, also the one at the end of `run_smoke.sh`, closes the game the
session started or found in front.
`stream_test` scripted input: controller arrival, A press/release, stick moves, D-pad and R2, `LiRequestIdrFrame()` at
half time.

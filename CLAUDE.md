# CLAUDE.md

Notes for coding agents working in this repository. User-facing docs: `README.md`; plan: `ROADMAP.md`;
verified state with evidence: `STATUS.md` (both German).

## Layout

- `src/sunshine/` – PS5 replacements for Sunshine parts (video, audio, input, process, main).
- `src/platform/ps5/` – Avcap2 capture (`capture.c`), `SharedCapture`, backend glue.
- `src/capture/`, `src/video/` – portable logic with unit tests in `tests/`.
- `tests/deps/` – tests built against the patched dependency sources (`scripts/test_deps.sh`, not part of `scripts/test.sh`).
- `src/payloads/` – standalone payloads that ship: `firmware_probe` and `host_restart` (release archive), `relaunch_helper`
  (embedded into the host).
- `src/probes/` – probes and spikes, standalone research and test payloads built into `out/` but not shipped.
- `src/web/` – the web page (HTTPS on base port + 1, off without a login): `web_auth.hpp`, `web_lines.hpp`, `web_plain.hpp` (plain HTTP gets a redirect) portable with
  tests in `tests/`; `web_login.cpp`, `web_server.cpp` host-only; `page/` HTML/JS/CSS embedded into the ELF by `CMakeLists.txt`.
- `patches/<dep>/` – patches applied on top of pinned sources; `deps.lock` pins every source by SHA256.
- `app/` – the PS5 settings app `PPSA47989` (ps5-homebrew-ui, ShadowMountPlus title folder).
- `tools/moonlight/` – automated Moonlight test client (pairing, smoke, reconnect, loss, A/V sync).
- `docs/` – reverse-engineering notes (German), ADRs.
- `assets/app/` – sources for the app icon (`icon0.png` 512²) and home-screen background (`pic0.png` 1920×1080).

## Build and test

Development runs on Linux with Docker, from the repository root. Build the SDK image once with
`docker build -t prosperosunshine-sdk:v0.45 -f scripts/Dockerfile .`, then:

```sh
docker run --rm -e PROSPERO_COMMIT=$(git rev-parse --short=8 HEAD) --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/build.sh   # host + probes -> out/
docker run --rm -e PROSPERO_COMMIT=$(git rev-parse --short=8 HEAD) -e PROSPERO_DEBUG=1 --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/build.sh   # the debug flavor -> out/debug/
docker run --rm --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/test.sh
docker run --rm --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/fuzz.sh [seconds] [target ...]   # libFuzzer, 0 = replay the seeds
docker run --rm --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/test_deps.sh   # Simple-Web-Server as patched, as the host configures it
```

`PROSPERO_COMMIT` names the commit the host reports; in a git worktree the container can't find it itself. On a machine with
little RAM, add `--memory=6g --memory-swap=6g` to the build so that a memory spike stops the container, not the machine. On
Windows, `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 [-Debug]` does the same.

`scripts/fuzz.sh` fuzzes the portable parsers (`tests/fuzz/`, seeds in `tests/fuzz/corpus/<target>/`); CI runs it for 5 s per
target. A crash leaves its input in `out/fuzz/crash-<target>-*` (reproduce with `out/fuzz/<target> <file>`): fix the bug, add
a case to that file's unit test and move the input into the seeds. Don't edit the script while it runs.

`scripts/test_deps.sh` (`tests/deps/`) and the fuzz targets `sws_parse` and `sws_server` run against the patched
Simple-Web-Server and Boost's headers: `scripts/dep_headers.sh` fetches and patches only those (`scripts/deps.sh --sources`,
nothing is built for the PS5), and a patch that no longer applies fails them. A bug in the library is fixed with a patch
in `patches/simple-web-server/` and a case in `tests/deps/sws_server_test.cpp`.

The app and its tests (the app bundles `out/prospero_sunshine.elf`, so build the host first). Build its image once with
`docker build -t prosperosunshine-app:2 -f app/Dockerfile app`; the volume `prosperosunshine-app-cache` needs the ps5-opengl
SDK once at `/cache/ps5-opengl-sdk` (`app/README.md`, "Building"; CI keeps it in `.cache/app`):

```sh
docker run --rm --mount type=bind,source="$PWD",target=/repo --mount type=volume,source=prosperosunshine-app-cache,target=/cache prosperosunshine-app:2 bash app/build.sh   # -> build/app/PPSA47989 and .zip
docker run --rm -e APP_FLAVOR=debug --mount type=bind,source="$PWD",target=/repo --mount type=volume,source=prosperosunshine-app-cache,target=/cache prosperosunshine-app:2 bash app/build.sh   # -> build/app-debug/PPSA47989-debug.zip
```

On Windows, `powershell -NoProfile -ExecutionPolicy Bypass -File app/build.ps1 [-Debug]` builds the image, copies the SDK into
the volume and runs the same.

The debug flavor (`-DPROSPERO_DEBUG=ON`, README "Debug build") is the same code with the `->` trace (`ps_diag_trace`), the
self-check (`src/sunshine/self_check.cpp`), `debug` as the default log level, masked addresses, held-back floods, the remote
log and `-g`. Keep the two flavors as alike as that: a bug that shows in one should show in the other. Test builds for an
issue are debug builds.

```sh
docker run --rm --mount type=bind,source="$PWD",target=/repo prosperosunshine-app:2 bash app/tests/run.sh
```

Stream test against the console. On Linux, build the client once with `bash tools/moonlight/build.sh` and run it in the SDK
container on the host's network:

```sh
DOCKER_ARGS="--network host" STREAM_ARGS="--no-script" bash tools/moonlight/run_smoke.sh <console> <seconds>
DOCKER_ARGS="--network host" STREAM_ARGS="--no-script --hevc" VIDEO_FORMAT=hevc bash tools/moonlight/run_smoke.sh <console> <seconds>
```

HEVC needs both `--hevc` and `VIDEO_FORMAT=hevc`; without `--hevc` the client asks for H.264. On Windows, from Git Bash, build the native client once with `bash tools/moonlight/build_windows.sh`; `run_smoke.sh`
uses it as long as `DOCKER_ARGS` and `PRE_CMD` are unset. `--no-script` presses nothing; without it the client presses
buttons on the console. Docker Desktop's network on Windows drops large bursts and falsifies loss and high-bitrate results.
With `--network host` on Linux there is no NAT in between, but whether large bursts arrive complete there is not measured
yet; `tools/moonlight/udp_burst.py` against `udp_burst_spike.elf` checks it (`tools/moonlight/README.md`). `loss_test.sh`
always uses the container client. The home screen is too quiet to load the encoder; the Mario 64 demo moves without input.

Release archive from an existing build: `bash scripts/package.sh [version]` (on the host) -> `dist/`.

Toolchain: PS5 Payload SDK v0.45 in the Docker image `prosperosunshine-sdk:v0.45` (clang 18, target
`x86_64-sie-ps5`). Dependencies build once into `out/deps` via `scripts/deps.sh`.

## Rules

- **The repo will be public.** No console IP, local paths, e-mail addresses or names in tracked files.
  Test tools take the console address from `PS5_HOST`. `scripts/check_private.sh` finds LAN addresses, home folders and
  e-mail addresses (CI runs it on the whole tree; exceptions with their reason in `scripts/private_allowlist.txt`); enable
  it for every commit once per clone with `git config core.hooksPath scripts/hooks`. Names and handles go one per line into
  the untracked `.private-words`. It only knows patterns, so still read the diff before every commit.
- Firmware 13.00 and 10.20 are verified. Capture refuses other versions unless the ResourceArbitrator calls
  resolve by NID, and then logs them as unverified; see `docs/firmware-porting.md` before porting.
- New or changed third-party sources: pin them in `deps.lock` (SHA256), patch via `patches/<dep>/`. Archives from servers
  outside GitHub (today ffmpeg and Opus) are mirrored in the `deps-mirror` pre-release, which `scripts/deps.sh` falls back
  to; upload the new archive there under its original file name when bumping one (needs the owner's OK, it publishes).
- `STATUS.md` records only what was verified live, with how it was measured. Plans go in `ROADMAP.md`.
- Commit only when asked; never push without being asked.
- Do not test rest mode on the console without asking first (risk to the homebrew environment). One test
  ended in a hung console that had to be switched off at the power button. Close all games first.
- Publishing (push, GitHub release, store catalog PR, issue comments) needs the owner's OK each time. No
  attribution lines for agents in commits or PRs.
- EULAs and terms of games and apps on the PS5 may be accepted for the owner; accounts, purchases and anything off the
  console still need asking. Don't save games.

## Releasing

0. Before starting a feature and again before a release: `gh pr list` and `gh issue list` (open ones, and what changed since the
   last release). A contributor may already be working on the same thing; PR 16 built surround in parallel with 0.3.1.
1. Set the version in `CMakeLists.txt` (`PROJECT_VERSION`, `PROJECT_VERSION_PATCH`), `contentVersion` in
   `app/sce_sys/param.json` (must rise, format `01.000.004`), the status line in `README.md` and `CHANGELOG.md`.
2. Build host and app, run both test suites, `scripts/package.sh <version>`. Check `SHA256SUMS` and grep the ELF
   for private strings (`10.0.0.0/8` in there is Sunshine's own LAN range).
3. Install the build on the console, re-register the app (below), restart the host, stream H.264 and HEVC. Before the first
   release with a change to the debug flavor, stream H.264 and HEVC at 1080p60 for 30 s on both flavors and compare the
   missing frames.
4. Push, then tag `v<version>` on that commit and push the tag: CI builds both flavors and the app from it and attaches
   `PPSA47989.zip`, `PPSA47989-debug.zip`, `prosperosunshine-<version>.tar.gz`, `prosperosunshine-<version>-debug.tar.gz`
   and `SHA256SUMS` to a draft release. Check it on the console, write the notes, then publish it. By hand, as before:
   `gh release create v<version> build/app/PPSA47989.zip dist/prosperosunshine-<version>.tar.gz` with
   `--target <full SHA>` (a short SHA fails with HTTP 422).
5. Catalog: a PR to `darkxex/ps5-homebrew-catalog` updating `apps/PPSA47989.json` (`version`, `artifact_url`, `sha256`,
   `icon_url`) from a fork branch. The store API at `/api/v1/apps/PPSA47989.json` shows when it is live.

## Working on the console

- The host is an ELF payload (ELF loader on port 9021); state lives in `/data/prosperosunshine/`.
  Control socket on TCP 47995, 127.0.0.1 only (the empty file `control_lan` there opens it to the LAN):
  `STATUS`, `PAIRINGS`, `PIN [<id>] <pin> [name]`, `DEVICES`, `STREAMS`, `ENDSTREAM <uuid>`, `RENAME <uuid> <name>`, `UNPAIR <uuid>`, `USERS`, `PROFILES`, `SETPROFILES <uuid> <profiles|unset>`, `APPS`, `SETAPPS <title id> <hidden|shown>`, `REPORT`, `AUTOLOAD [ON|OFF]`, `RELISTEN`, `PROBE`, `FILE`, `INSTALL`, `CANCELPIN <id>`, `WEBLOGIN [SET <user> <password>|ON|OFF|FORGET]`, `QUIT` (one client at a time). `STREAMS` lists the running streams, one `OK uuid=... address=... codec=... hdr=... mode=... kbps=... secs=... pads=... role=... channel=... name=...` line each, then `END` (format in `src/control/stream_list.hpp`); `ENDSTREAM` ends one device's stream while the game and the others go on. `USERS` lists the console's profiles (`OK <id> <name>` lines, then `END`), `PROFILES` the profiles each paired device plays as (`OK <uuid> <free|none|ids[,guest]>`), and `SETPROFILES` sets or unsets one device's (format in `src/control/device_profiles.hpp`); the web page uses them. `REPORT` makes the
  host answer on TCP 47996 for some seconds with `diagnostics.txt` (a title may not listen on the network).
  `RELISTEN` starts the host's services anew as after rest mode took its ports; `out/relisten_probe.elf` sends it.
  `PROBE` sends `/data/prosperosunshine/firmware_probe.elf` to the ELF loader unless a stream runs (`docs/firmware-probe.md`).
- Before touching the console, check `http://<console>:47989/serverinfo`: go on only at `SUNSHINE_SERVER_FREE`, someone may
  be streaming. Don't take screenshots during a stream.
- Logs: `sunshine.log` (older ones `.1`, `.2`, ...; `min_log_level = debug` in `sunshine.conf` also logs motors, light bar
  and trigger blocks), and `host_diag.log`, which the app's diagnostic report reads. The host reads `sunshine.conf` at
  start; to restart it, use "Restart host" in the app.
- Key presses from a tool add a virtual controller next to the host's pad, and it stays. Remove it afterwards, or the
  owner's input in a game may not work. After waking from rest mode the console waits on "Press the PS button" and the
  user selection.
- Rest mode closes the host's listening sockets and stops its log files; `wait_or_ports_lost` in `main_ps5.cpp` notices
  refused ports and starts the services anew in the same process.
- Whether a game sends trigger effects: only some do (Teardown yes, GTA III not in the sessions measured), and among the official clients only
  Moonlight for PC passes them to a DualSense (a contributor reports R3VALATiON's Artemis fork on Android too, issue 30). See `docs/re/adaptive-triggers.md`.
- Autoload through the Payload Manager: `/data/pldmgr/autoload.txt`, slot `local/prospero-sunshine`.
- A title may have no `/data` (9.60): then the app reaches the files listed in `src/control/file_bridge.cpp` through `FILE` and `INSTALL` on
  the control socket, while the host runs, and the host closes it through `/app0/app.quit`.
- The app is installed at `/data/homebrew/PPSA47989`. A ShadowMountPlus scan does **not** pick up changes to
  `sce_sys` (param.json, icon, background) of an already registered title. Re-register it:
  `POST http://<console>:10101/api/v1/games/uninstall {"title_id":"PPSA47989"}` (keeps the source folder),
  then `POST /api/v1/scan {}`.
- The app sits in the Media tab like Prospero Themes: `applicationCategoryType` 65536, while keeping
  `contentBadgeType` 1 and the `launchActivity` intent. A Media-kind title must not carry the `amm` block
  (launch fails with 0x80020016 / CE-100022-5).
- The ProsperoStore catalog is cached on the console under `/data/prosperostore/cache/`; check a title ID
  against it before a release.
- Close the app (write `/data/prosperosunshine/app.quit`) before uploading to it or starting another title. Once a title
  started while the app was still alive, and the app was started again later: from then on every title failed with `0x80940033`,
  ShadowMountPlus logging `launch mount unavailable ... Device busy`, until the console restarted (`docs/status-archive.md`, "Titelstarts blockiert").
- Don't open the real Moonlight on the PC without asking, its window takes the focus and the owner may be playing there.
  `stream_test.exe` has no window.
- `ps5_launch` and `ps5_open_url` sometimes fail with `0x80960003` while nothing is wrong; try again. A browser card must be
  closed first (hold Options, Close Card). For encoder load without a game, open a noise page in the browser card.

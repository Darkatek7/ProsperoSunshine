# Third-party notices for the release archive (ROADMAP P7.2)

This file describes what the release step (P7.4) must bundle with `prospero_sunshine.elf`; `scripts/package.sh` builds
that archive and fails if a licence text is missing. Licence analysis: `docs/licenses.md`.

## Archive layout

```
prospero_sunshine.elf
firmware_probe.elf           docs/firmware-probe.md; sent to the ELF loader by hand or by the host's PROBE
host_restart.elf             restarts the host without the app
SHA256SUMS
CHANGELOG.md
LICENSE                      GPL-3.0 text of ProsperoSunshine (repository file LICENSE)
THIRD_PARTY.md               the notice, repository file licenses/THIRD_PARTY.md
licenses/
  prosperosunshine.txt       copy of LICENSE
  sunshine.txt  moonlight-common-c.txt  enet.txt  nanors.txt  simple-web-server.txt
  nlohmann-json.txt  libdisplaydevice.txt  boost.txt  openssl.txt  opus.txt  ffmpeg.txt
                             all produced by scripts/deps.sh into out/deps/licenses/
  ps5-payload-sdk-runtime/   from this repository, see below
```

Also state in the release notes the tag that contains the corresponding source (`deps.lock`, `patches/`, scripts).

## SDK runtime

The SDK zip (v0.45) ships no licence file, so the texts are kept in this repository under
`licenses/ps5-payload-sdk-runtime/`; `scripts/package.sh` and `app/build.sh` copy that directory into their output.

| File | Source | Covers |
| --- | --- | --- |
| `ps5-payload-sdk-LICENSE.txt` | `ps5-payload-dev/sdk`, tag v0.45, `LICENSE` (GPL-3.0) | the SDK |
| `freebsd-11.4-COPYRIGHT.txt` | `freebsd-src`, tag `release/11.4.0`, `COPYRIGHT`; the SDK headers report `__FreeBSD_version` 1104001 | FreeBSD-derived `libc.a`, `libpthread.a` and start files |
| `llvm-18.1-libcxx-LICENSE.txt`, `-libcxxabi-`, `-libunwind-` | `llvm-project`, tag `llvmorg-18.1.0`; the SDK headers report `_LIBCPP_VERSION` 180100 | `libc++.a`, `libc++abi.a`, `libunwind.a` |
| `llvm-18.1-compiler-rt-LICENSE.txt` | same tag | compiler-rt builtins, if linked (same terms; kept either way) |

The tags were derived from the version macros in the SDK headers, because the SDK does not state which upstream
release its runtime was built from. Individual libc source files can carry further BSD notices that the FreeBSD
`COPYRIGHT` file does not reproduce one by one.

## Checks before publishing

- `licenses/` contains all 11 texts listed above plus `prosperosunshine.txt` and the SDK runtime directory.
- No file contains local paths, addresses or personal data.
- The tag referenced in the release notes contains `deps.lock`, `patches/` and `scripts/`.

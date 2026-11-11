#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Asks OSV (https://osv.dev) whether a known vulnerability lists one of the sources pinned in deps.lock, and prints
# what it finds. Exit code: 0 nothing new, 1 at least one hit that is not triaged, 2 a lookup failed or deps.lock has a
# name this script does not know (add it to REPOS below, or the pin would go unwatched).
#
#   bash scripts/check_advisories.sh              # query OSV for every pin
#   bash scripts/check_advisories.sh --dry-run    # print what would be sent, send nothing
#
# What leaves the machine: per pin one request to api.osv.dev with either {"commit": "<sha>"} (git pins) or
# {"version": "<tag>", "package": {"name": "<upstream repository URL>", "ecosystem": "GIT"}} (release tarballs): the
# upstream project's public address and the pinned version. Nothing else; the request has no client name or this
# repository's name. --dry-run shows every body.
#
# OSV matches by repository and version range, so it also reports advisories of other programs from the same repository
# (miniupnpd's CVEs under the miniupnp repository, Boost.Graph's under boostorg/boost). Those are no hit for us, but only
# a person can say so by reading the advisory and the build scripts: record them in scripts/advisories_triaged.txt
# (optional, one `<id> <reason>` per line, `#` starts a comment) and they print as "triaged" and do not fail the run.
# A triaged line is a decision about our build, so it should say why ("not built", "not reachable") and be looked at
# again when the pin moves. OSV and the advisories' own text are the only source; what a hit means here still has to
# be read in the code.
#
# Only needs bash, python3 and network access to api.osv.dev. Not part of CI; run it before a release and, for a
# standing watch, monthly from a scheduled job. OSV lags upstream: also read the security advisories of
# LizardByte/Sunshine and moonlight-stream/moonlight-common-c and OpenSSL's vulnerability page then.
set -euo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
dry_run=0
case "${1:-}" in
    "") ;;
    --dry-run) dry_run=1 ;;
    *) echo "usage: check_advisories.sh [--dry-run]" >&2; exit 2 ;;
esac

exec python3 -I - "$repo/deps.lock" "$repo/scripts/advisories_triaged.txt" "$dry_run" <<'PY'
import json
import re
import sys
import urllib.error
import urllib.request

lock_path, triaged_path, dry_run = sys.argv[1], sys.argv[2], sys.argv[3] == "1"
OSV = "https://api.osv.dev/v1/query"

# deps.lock name -> (upstream repositories OSV knows the project by, tag of a release version).
# FFmpeg's CVEs are filed under its own git host; the GitHub mirror is asked too.
REPOS = {
    "openssl": (["https://github.com/openssl/openssl"], lambda v: f"openssl-{v}"),
    "opus": (["https://github.com/xiph/opus"], lambda v: f"v{v}"),
    "boost": (["https://github.com/boostorg/boost"], lambda v: f"boost-{v}"),
    "ffmpeg": (["https://git.ffmpeg.org/ffmpeg.git", "https://github.com/FFmpeg/FFmpeg"], lambda v: f"n{v}"),
    "miniupnpc": (["https://github.com/miniupnp/miniupnp"], lambda v: "miniupnpc_" + v.replace(".", "_")),
    "nlohmann-json": (["https://github.com/nlohmann/json"], lambda v: f"v{v}"),
}
# Git pins (the version column is a commit): queried by commit, whatever repository OSV filed the advisory under.
SHA = re.compile(r"^[0-9a-f]{40}$")


def triaged():
    result = {}
    try:
        with open(triaged_path, encoding="utf-8") as handle:
            for line in handle:
                line = line.split("#", 1)[0].strip()
                if line:
                    ident, _, reason = line.partition(" ")
                    result[ident] = reason.strip()
    except FileNotFoundError:
        pass
    return result


def post(body):
    """All pages of OSV's answer to one query."""
    vulns = []
    while True:
        request = urllib.request.Request(OSV, data=json.dumps(body).encode(), headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=60) as response:
            answer = json.load(response)
        vulns += answer.get("vulns", [])
        token = answer.get("next_page_token")
        if not token:
            return vulns
        body = {**body, "page_token": token}


def queries(name, version):
    if SHA.match(version):
        return [{"commit": version}]
    if name not in REPOS:
        return None
    urls, tag = REPOS[name]
    return [{"version": tag(version), "package": {"name": url, "ecosystem": "GIT"}} for url in urls]


pins = []
with open(lock_path, encoding="utf-8") as handle:
    for line in handle:
        fields = line.split("#", 1)[0].split()
        if len(fields) >= 2:
            pins.append((fields[0], fields[1]))

known = triaged()
status = 0
hits = 0
for name, version in pins:
    bodies = queries(name, version)
    if bodies is None:
        print(f"{name} {version}: no repository mapping in this script; the pin is not watched")
        status = 2
        continue
    found = {}
    for body in bodies:
        if dry_run:
            print(f"{name} {version}: would send {json.dumps(body)}")
            continue
        try:
            for vuln in post(body):
                found[vuln["id"]] = vuln
        except (urllib.error.URLError, TimeoutError, ValueError) as error:
            print(f"{name} {version}: OSV lookup failed: {error}")
            status = 2
    if dry_run:
        continue
    label = version[:8] if SHA.match(version) else version
    if not found:
        print(f"{name} {label}: no advisories")
        continue
    print(f"{name} {label}: {len(found)} advisory(ies)")
    for ident, vuln in sorted(found.items()):
        summary = (vuln.get("summary") or vuln.get("details") or "").strip().splitlines()
        aliases = ",".join(vuln.get("aliases", []))
        note = known.get(ident)
        # The advisory's own fixed-in commit, when OSV lists one: a bump past it clears the hit.
        fixed = sorted({event["fixed"][:12] for affected in vuln.get("affected", []) for rng in affected.get("ranges", [])
                        for event in rng.get("events", []) if "fixed" in event})
        print(f"  {'triaged' if note is not None else 'HIT    '} {ident}"
              f"{' (' + aliases + ')' if aliases else ''}: {summary[0][:110] if summary else ''}")
        print(f"          published {vuln.get('published', '?')[:10]}"
              f"{', fixed in ' + ' '.join(fixed) if fixed else ''}")
        if note is not None:
            print(f"          triaged: {note or '(no reason given)'}")
        else:
            hits += 1

if not dry_run:
    print(f"{len(pins)} pins checked, {hits} advisory(ies) not triaged")
if hits and status == 0:
    status = 1
sys.exit(status)
PY

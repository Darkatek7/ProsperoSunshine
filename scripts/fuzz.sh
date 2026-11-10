#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# libFuzzer targets for the portable parsers (tests/fuzz/*_fuzz.cpp), under AddressSanitizer and UndefinedBehaviorSanitizer,
# built like scripts/test.sh builds the unit tests. Run it in the SDK image:
#   docker run --rm --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/fuzz.sh [seconds] [target ...]
# seconds     per target, default 20; 0 only replays the seed corpus and the crash regressions once (a few seconds in all)
# target      one or more of the names below; default all of them
# FUZZ_JOBS   targets run at the same time, default 1 (each uses one core)
# FUZZ_ARGS   more libFuzzer options, e.g. FUZZ_ARGS="-max_len=512 -seed=7"
# Seeds are tests/fuzz/corpus/<target>/ (tracked: hand-written starting points and one file for every bug a fuzzer found).
# What a run learns goes to out/fuzz/work/<target>/ and is kept for the next run; a crash, hang or sanitizer report leaves its
# input in out/fuzz/crash-<target>-* (reproduce with out/fuzz/<target> <that file>). Move a fixed bug's input into the seeds.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
seconds=${1:-20}
shift || true
out="$root/out/fuzz"
mkdir -p "$out/work" "$out/include/app"
# The app's code includes itself as "app/psapp/...", as in app/tests/run.sh.
ln -sfn "$root/app/src/psapp" "$out/include/app/psapp"

declare -A sources=(
    [mdns]="src/net/mdns.cpp"
    [upnp]=""
    [web_plain]=""
    [http_guard]=""
    [web_lines]=""
    [web_auth]=""
    [device_files]=""
    [file_bridge]="src/control/file_bridge.cpp"
    [conf]="app/src/psapp/conf.cpp app/src/psapp/web.cpp"
    [title_profile]=""
    [titles]="src/apps/titles.cpp"
    [log]=""
    [input]=""
    [app_diag]="app/src/psapp/diag.cpp app/src/psapp/autoload.cpp"
)
# Longest input a target uses; mDNS packets and the files are short, a param.sfo can be longer.
declare -A max_len=([mdns]=600 [titles]=1500 [web_plain]=1500 [file_bridge]=600 [web_auth]=1500 [log]=1500 [app_diag]=1500 [input]=1500)
all=(mdns upnp web_plain http_guard web_lines web_auth device_files file_bridge conf title_profile titles log app_diag input)
targets=("$@")
[ ${#targets[@]} -gt 0 ] || targets=("${all[@]}")
for t in "${targets[@]}"; do
    [ -f "$root/tests/fuzz/${t}_fuzz.cpp" ] && [ "${sources[$t]+set}" ] || { echo "unknown target: $t (known: ${all[*]})" >&2; exit 2; }
done

build() {
    local name=$1 extra=()
    for s in ${sources[$name]}; do extra+=("$root/$s"); done
    # -fno-sanitize-recover: an undefined-behavior report stops the run and leaves its input, as a crash does.
    clang++-18 -std=c++23 -stdlib=libstdc++ -Wall -Wextra -Werror -O1 -g -UNDEBUG \
        -fsanitize=fuzzer,address,undefined,float-cast-overflow -fno-sanitize-recover=undefined,float-cast-overflow \
        -I "$root/src" -I "$out/include" -I "$root/tests/fuzz" "$root/tests/fuzz/${name}_fuzz.cpp" "${extra[@]}" -o "$out/$name"
}
run() {
    local name=$1 stop=() seeds=()
    [ -d "$root/tests/fuzz/corpus/$name" ] && seeds=("$root/tests/fuzz/corpus/$name")
    mkdir -p "$out/work/$name"
    if [ "$seconds" -gt 0 ]; then stop=("-max_total_time=$seconds"); else stop=("-runs=0"); fi
    # The run's corpus first (where new inputs are written), then the tracked seeds.
    ASAN_OPTIONS=detect_leaks=1:symbolize=1:abort_on_error=0 UBSAN_OPTIONS=print_stacktrace=1 \
        "$out/$name" "${stop[@]}" -max_len="${max_len[$name]:-400}" -timeout=10 -rss_limit_mb=2048 -print_final_stats=1 \
        -artifact_prefix="$out/crash-$name-" ${FUZZ_ARGS:-} "$out/work/$name" "${seeds[@]}" >"$out/$name.log" 2>&1
}

# Build all targets, a few at a time.
cores=$(nproc)
for t in "${targets[@]}"; do
    while [ "$(jobs -rp | wc -l)" -ge "$cores" ]; do wait -n || { echo "build failed" >&2; exit 1; }; done
    build "$t" &
done
while [ "$(jobs -rp | wc -l)" -gt 0 ]; do wait -n || { echo "build failed" >&2; exit 1; }; done

jobs_max=${FUZZ_JOBS:-1}
failed=()
declare -A pids=()
finish() {
    local name=$1
    if wait "${pids[$name]}"; then status=ok; else status=FAILED; failed+=("$name"); fi
    local done_line cov
    done_line=$(grep -E '^Done [0-9]+ runs' "$out/$name.log" | tail -1 || true)
    cov=$(grep -E '^#[0-9]+.* cov: ' "$out/$name.log" | tail -1 | sed -E 's/.*cov: ([0-9]+) ft: ([0-9]+).*/edges \1 features \2/' || true)
    printf '%-14s %-7s %s | %s\n' "$name" "$status" "${done_line:-no result}" "${cov:-no coverage line}"
    if [ "$status" = FAILED ]; then grep -E 'FUZZ_CHECK failed|ERROR: |SUMMARY: |Test unit written|crash-|timeout' "$out/$name.log" | head -8 | sed 's/^/    /'; fi
}
for t in "${targets[@]}"; do
    while [ "${#pids[@]}" -ge "$jobs_max" ]; do
        for n in "${!pids[@]}"; do
            if ! kill -0 "${pids[$n]}" 2>/dev/null; then finish "$n"; unset "pids[$n]"; fi
        done
        [ "${#pids[@]}" -ge "$jobs_max" ] && sleep 0.5
    done
    run "$t" &
    pids[$t]=$!
done
for n in "${!pids[@]}"; do finish "$n"; done
if [ ${#failed[@]} -gt 0 ]; then
    echo "fuzz: failed: ${failed[*]} (inputs in out/fuzz/crash-*, logs in out/fuzz/<target>.log)" >&2
    exit 1
fi
echo "fuzz: ${#targets[@]} targets ok"

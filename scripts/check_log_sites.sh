#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# A log line that names an address, a device, a router or a person can end up in a public issue. Every log statement of the
# host's own sources (and of the lines our patches add to Sunshine) whose arguments can hold such a value is listed in
# scripts/log_sites.txt with the reason it is fine; a new or changed one fails here until someone has looked at it. What to check:
#   1. Which file does it end up in (sunshine.log, host_diag.log, the kernel log, the debug build's remote log)?
#   2. What can the value be? An address and a name are masked (app/src/psapp/mask.hpp, src/log/log_filter.hpp); anything else
#      that names someone needs a rule there.
#   3. Add a sample line of its shape (made-up values) to tests/privacy_leak_test.cpp, then the statement to scripts/log_sites.txt.
# Usage: scripts/check_log_sites.sh            check (scripts/test.sh runs it)
#        scripts/check_log_sites.sh --list     print every statement that needs an entry, in the format of the list
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
list=scripts/log_sites.txt

# "<file>|<statement>" for each log statement, whitespace and trailing comments taken out. A statement may span lines.
statements() {
    {
        find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.c' -o -name '*.h' \) \
            ! -path 'src/probes/*' ! -path 'src/payloads/*' | LC_ALL=C sort | xargs awk "$extract"
        find patches/sunshine -type f -name '*.patch' | LC_ALL=C sort | xargs awk "$extract"
    }
}
extract='
function flush() {
    if (stmt != "") {
        sub(/;[ \t]*\/\/.*$/, ";", stmt)
        gsub(/[ \t]+/, " ", stmt)
        sub(/^ /, "", stmt)
        print FILENAME "|" stmt
    }
    stmt = ""
}
FNR == 1 { flush() }
{
    line = $0
    if (FILENAME ~ /\.patch$/) {
        if (line !~ /^\+/ || line ~ /^\+\+\+/) { flush(); next }
        sub(/^\+/, "", line)
    }
    if (stmt == "" && line ~ /^[ \t]*#/) next   # a macro definition is not a statement
    if (stmt == "" && line ~ /(BOOST_LOG|ps_diag_log|ps_diag_trace)\(/) stmt = line
    else if (stmt != "") stmt = stmt " " line
    if (stmt != "" && stmt ~ /;[ \t]*(\/\/.*)?$/) flush()
}
END { flush() }'

# The ones whose arguments (outside string literals) can hold a value that names a device, an address or a person.
risky() {
    while IFS= read -r entry; do
        code=$(printf '%s' "${entry#*|}" | sed -E 's/"([^"\\]|\\.)*"//g')
        if printf '%s' "$code" | grep -Eq '\b(addr|address|peer|router|external|ip|url|URL|remote|uuid|mac|name|fingerprint|title|host|client|user|cert|device|path|what|text|line|message|describe|which|forwarded|problem|reason|detail)\b'; then
            printf '%s\n' "$entry"
        fi
    done
}

if [[ ${1:-} == --list ]]; then
    statements | risky | LC_ALL=C sort -u
    exit 0
fi

# Lines of the list that start with # are the reasons, for the entries below them.
known=$(sed -E '/^[[:space:]]*(#|$)/d' "$list" | LC_ALL=C sort -u)
current=$(statements | risky | LC_ALL=C sort -u)
new=$(comm -13 <(printf '%s\n' "$known") <(printf '%s\n' "$current") | sed '/^$/d')
if [[ -n $new ]]; then
    echo "FAIL: log statements that can name an address, a device or a person, not in $list:" >&2
    printf '  %s\n' "$new" >&2
    echo "Check where each line ends up and what it can hold (see the top of scripts/check_log_sites.sh), add a sample to" >&2
    echo "tests/privacy_leak_test.cpp, then list the statement with the reason it is fine." >&2
    exit 1
fi
# An entry that no statement matches any more is stale: it would let a changed statement through unnoticed.
gone=$(comm -23 <(printf '%s\n' "$known") <(printf '%s\n' "$current") | sed '/^$/d')
if [[ -n $gone ]]; then
    echo "FAIL: entries in $list that match no log statement any more (remove them):" >&2
    printf '  %s\n' "$gone" >&2
    exit 1
fi
echo "PASS: log sites ($(printf '%s\n' "$current" | sed '/^$/d' | wc -l) statements that can hold an address or a name, all reviewed)"

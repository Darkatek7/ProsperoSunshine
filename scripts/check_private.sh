#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# The repo is public: this looks for LAN addresses, home folders, e-mail addresses and the owner's own words in what
# git tracks. scripts/check_private.sh scans every tracked text file (CI); with --staged, only the lines the next
# commit adds (scripts/hooks/pre-commit). Addresses and paths that belong in the tree are listed in
# scripts/private_allowlist.txt. Names and handles go one per line into .private-words at the top of the repo: it is
# untracked, so the list itself stays private, and matches ignore case and whole words only.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

mode=tree
case "${1:-}" in
    "") ;;
    --staged) mode=staged ;;
    *) echo "usage: scripts/check_private.sh [--staged]" >&2; exit 2 ;;
esac

# A host on the console's kind of LAN; a range such as 10.0.0.0/8 keeps its prefix so the allowlist can tell the two
# apart. The character in front keeps 110.0.0.1 out and is cut off again below.
address_re='(^|[^0-9.])(10\.0\.0\.[0-9]{1,3}|192\.168\.[0-9]{1,3}\.[0-9]{1,3})(/[0-9]{1,2})?'
# A user's home folder on Linux, macOS and Windows (also written with doubled backslashes, as in C strings).
# Placeholders such as /home/<user> don't match.
path_re='/home/[A-Za-z0-9._-]+|/Users/[A-Za-z0-9._-]+|[A-Za-z]:(\\+|/)[Uu]sers(\\+|/)[A-Za-z0-9._-]+'
mail_re='[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}'

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
lines="$tmp/lines"   # file <TAB> line <TAB> text

if [[ $mode == tree ]]; then
    # -I skips binary files (images, fonts).
    git -c core.quotePath=false grep -nI --null -e '' | tr '\0' '\t' > "$lines" || true
else
    # Only the added lines, numbered as in the staged file. Header lines count only before a file's first hunk, so an
    # added line that itself starts with "++" is still read as content.
    git -c core.quotePath=false diff --cached -U0 --no-color --no-ext-diff --no-textconv --diff-filter=ACMR |
        awk '
            /^diff --git / { header = 1; file = ""; next }
            header && /^\+\+\+ / { file = substr($0, 5); sub(/^b\//, "", file); next }
            /^@@ / { header = 0; split($0, part, " "); sub(/^\+/, "", part[3]); split(part[3], start, ","); n = start[1]; next }
            header { next }
            /^\+/ { print file "\t" n "\t" substr($0, 2); n++ }
        ' > "$lines"
fi

# Allowlist lines: <path glob> <regular expression for the matched text>; # starts a comment.
allow_globs=()
allow_res=()
while read -r glob re _; do
    [[ -z $glob || $glob == \#* ]] && continue
    allow_globs+=("$glob")
    allow_res+=("$re")
done < scripts/private_allowlist.txt

allowed() {
    local file=$1 token=$2 i
    for i in "${!allow_globs[@]}"; do
        # shellcheck disable=SC2053 # the glob is meant to match
        [[ $file == ${allow_globs[$i]} && $token =~ ${allow_res[$i]} ]] && return 0
    done
    return 1
}

found=0
# scan <what> <grep options and pattern>: every match on a candidate line that the allowlist doesn't cover.
scan() {
    local what=$1 file line text token
    shift
    while IFS=$'\t' read -r file line text; do
        while IFS= read -r token; do
            [[ $what == address ]] && token=${token#[^0-9]}
            [[ -n $token ]] || continue
            if [[ $what == word ]] || ! allowed "$file" "$token"; then
                printf '%s:%s: %s %s\n' "$file" "$line" "$what" "$token"
                found=$((found + 1))
            fi
        done < <(printf '%s\n' "$text" | grep -o "$@" || true)
    done < <(grep "$@" "$lines" || true)
}

scan address -E "$address_re"
scan "home folder" -E "$path_re"
scan e-mail -E "$mail_re"
if [[ -f .private-words ]]; then
    grep -v -e '^[[:space:]]*#' -e '^[[:space:]]*$' .private-words | sed 's/^[[:space:]]*//; s/[[:space:]]*$//' > "$tmp/words" || true
    [[ -s "$tmp/words" ]] && scan word -i -w -F -f "$tmp/words"
fi

if ((found > 0)); then
    echo "check_private: $found private-looking string(s) in tracked files. Remove them; an example address or path that" >&2
    echo "belongs in the tree goes into scripts/private_allowlist.txt with the reason." >&2
    exit 1
fi
echo "check_private: nothing found ($mode)"

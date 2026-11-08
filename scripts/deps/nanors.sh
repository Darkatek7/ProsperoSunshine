#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck disable=SC2154  # sdk, prefix, work, src_* and the rest come from scripts/deps.sh
# nanors: the same three sources Sunshine compiles directly. Its SIMD kernels use per-function
# target attributes, but clang's immintrin.h hides AVX2/AVX-512 types on __SCE__ targets unless the
# feature is enabled for the whole file, so the macro is masked for that one include.
# Its dispatch uses __builtin_cpu_supports, whose compiler-rt support the SDK does not link;
# the prelude answers the same questions from CPUID and XCR0 instead.
set -euo pipefail
n="$src_nanors"
b="$work/nanors-build"
rm -rf "$b" && mkdir -p "$b" "$prefix/include/nanors"
cat > "$b/prelude.h" <<'PRELUDE'
#include <stdint.h>
#include <string.h>
#include <cpuid.h>
#pragma push_macro("__SCE__")
#undef __SCE__
#include <immintrin.h>
#pragma pop_macro("__SCE__")
static inline int prospero_cpu_supports(const char *feature) {
    unsigned a, b, c, d, b7 = 0, c7 = 0, xcr0 = 0;
    if (!__get_cpuid(1, &a, &b, &c, &d)) return 0;
    const unsigned c1 = c;
    if (__get_cpuid_count(7, 0, &a, &b, &c, &d)) { b7 = b; c7 = c; }
    if (c1 & (1u << 27)) { unsigned hi; __asm__("xgetbv" : "=a"(xcr0), "=d"(hi) : "c"(0)); }
    const int ymm = (xcr0 & 0x06) == 0x06, zmm = (xcr0 & 0xE6) == 0xE6;
    if (!strcmp(feature, "ssse3")) return (c1 >> 9) & 1;
    if (!strcmp(feature, "avx2")) return ymm && ((b7 >> 5) & 1);
    if (!strcmp(feature, "avx512f")) return zmm && ((b7 >> 16) & 1);
    if (!strcmp(feature, "gfni")) return (c7 >> 8) & 1;
    return 0;
}
#define __builtin_cpu_init() ((void)0)
#define __builtin_cpu_supports(feature) prospero_cpu_supports(feature)
PRELUDE
for f in rs.c deps/obl/oblas_common.c deps/obl/oblas_lite.c; do
    "$sdk/bin/prospero-clang" -std=gnu11 -O2 -fPIC -include "$b/prelude.h" -I"$n" -I"$n/deps/obl" \
        -c "$n/$f" -o "$b/$(basename "$f" .c).o"
done
"$sdk/bin/prospero-ar" rcs "$prefix/lib/libnanors.a" "$b"/*.o
cp "$n/rs.h" "$n/deps/obl"/*.h "$prefix/include/nanors/"
cp "$n/LICENSE" "$prefix/licenses/nanors.txt"

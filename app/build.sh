#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Builds the ProsperoSunshine PS5 app (a title folder for ShadowMountPlus) inside app/Dockerfile's
# image. app/build.ps1 starts it; run by hand:
#
#   docker run --rm -v "$PWD":/repo -v prosperosunshine-app-cache:/cache prosperosunshine-app:2 \
#       bash app/build.sh
#
# The app is drawn with ps5-homebrew-ui (GPL-3.0-or-later), which also brings the PS5 build: its
# tools/build.sh compiles everything under src/, links it against ps5-opengl, signs it and puts the
# title folder together. This script fetches the kit at a pinned commit, stages a copy of it with the
# kit's own app, designs and demo replaced by app/src, and runs the kit's `make app` there.
#
# Inputs (all optional):
#   /cache/ps5-opengl-sdk   the ps5-opengl SDK (manifest.sha256 inside); app/build.ps1 copies it from
#                           PS5SD's build volume, PS5_OPENGL_SDK overrides the path
#   APP_FLAVOR              normal (default) or debug: the debug app says so (src/psapp/flavor.hpp gets
#                           PROSPERO_DEBUG) and bundles the debug host (README, "Debug build")
#   HOST_ELF                the host payload to bundle (default out/prospero_sunshine.elf, for debug
#                           out/debug/prospero_sunshine.elf)
#   APP_SCE_SYS             the title data folder (default app/sce_sys)
# Output: build/app/PPSA47989/ (the title folder) and build/app/PPSA47989.zip; for debug build/app-debug/PPSA47989/ and
# build/app-debug/PPSA47989-debug.zip, the same title, so it replaces the normal app's folder and back.
set -euo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
app_src="$repo/app"
cache=${APP_CACHE:-/cache}
kit_url=https://github.com/Phononenom/ps5-homebrew-ui
kit_rev=bf44d80384d5b8f8f4f213cf98d748887211328d
kit="$cache/ps5-homebrew-ui"
stage="$cache/stage"
glsdk=${PS5_OPENGL_SDK:-$cache/ps5-opengl-sdk}
flavor=${APP_FLAVOR:-normal}
case "$flavor" in
    normal) default_elf="$repo/out/prospero_sunshine.elf"; out="$repo/build/app"; suffix="" ;;
    debug) default_elf="$repo/out/debug/prospero_sunshine.elf"; out="$repo/build/app-debug"; suffix="-debug" ;;
    *) echo "app/build.sh: APP_FLAVOR is normal or debug, not $flavor" >&2; exit 2 ;;
esac
host_elf=${HOST_ELF:-$default_elf}
sce_sys=${APP_SCE_SYS:-$app_src/sce_sys}

[ -f "$glsdk/manifest.sha256" ] || {
    echo "app/build.sh: no ps5-opengl SDK at $glsdk (see app/README.md, Building)" >&2
    exit 2
}

# ---- the kit, pinned ---------------------------------------------------------------
if [ ! -d "$kit/.git" ]; then
    git clone -q "$kit_url" "$kit"
fi
if [ "$(git -C "$kit" rev-parse HEAD)" != "$kit_rev" ]; then
    git -C "$kit" fetch -q origin "$kit_rev" 2>/dev/null || git -C "$kit" fetch -q origin
    git -C "$kit" checkout -q --detach "$kit_rev"
fi

# ---- the staged tree ---------------------------------------------------------------
# The kit without its own app (src/app, src/concepts, src/demo, src/main.cpp), music and second
# sound set, plus app/src and app/sce_sys. .deps, build and the generated libc.prx survive between
# runs, so only the first build fetches the payload SDK and builds the host tools.
mkdir -p "$stage"
rsync -a --delete \
    --exclude=/.git --exclude=/.deps --exclude=/build --exclude=/dist --exclude=/runtime/libc.prx \
    --exclude=/src/app --exclude=/src/concepts --exclude=/src/demo --exclude=/src/main.cpp \
    --exclude=/assets/audio/music --exclude=/assets/audio/sfx/paper \
    --exclude=/sce_sys \
    "$kit/" "$stage/"
rsync -a --delete "$app_src/src/" "$stage/src/app/"
mv "$stage/src/app/main.cpp" "$stage/src/main.cpp"
if [ "$flavor" = debug ]; then
    sed -i '1i #define PROSPERO_DEBUG 1' "$stage/src/app/psapp/flavor.hpp"
    grep -q "^#define PROSPERO_DEBUG 1" "$stage/src/app/psapp/flavor.hpp"
fi
# rsync gives the staged files their source's time, so after a debug build the normal flavor.hpp looked older than the
# debug objects, and the normal build kept them (0.6.6's local app said "debug"). A change of flavor touches it, so
# everything that includes it is built again.
flavor_stamp="$stage/build/.flavor"
if [ "$(cat "$flavor_stamp" 2>/dev/null)" != "$flavor" ]; then
    touch "$stage/src/app/psapp/flavor.hpp"
fi
rm -rf "$stage/sce_sys"
mkdir -p "$stage/sce_sys"
cp "$sce_sys/param.json" "$sce_sys/icon0.png" "$stage/sce_sys/"
# The kit's checks know a game (category 0, badge 1, gameIntent) and a media app (65536, badge 2, no
# gameIntent). The title is a media app the way the console's homebrew media apps are (65536, badge
# 1, gameIntent; app/README.md), so the kit builds with the game variant and the real param.json
# goes into the title folder afterwards.
python3 -c 'import json,sys; p=json.load(open(sys.argv[1])); p["applicationCategoryType"]=0; p["contentBadgeType"]=1; json.dump(p, open(sys.argv[1], "w"), indent=2)' \
    "$stage/sce_sys/param.json"
# The home screen backgrounds come from pic0.png (1920x1080 or larger); without it the title has none.
if [ -f "$sce_sys/pic0.png" ]; then
    bash "$app_src/tools/make-backgrounds.sh" "$sce_sys/pic0.png" "$stage/sce_sys" "$cache"
fi

# ---- build -------------------------------------------------------------------------
make -C "$stage" app PS5_OPENGL_PREFIX="$glsdk" USE_CCACHE=0 PS5_CLANG=clang-18
echo "$flavor" > "$flavor_stamp"

# ---- the title folder --------------------------------------------------------------
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$sce_sys/param.json")
title="$stage/dist/$title_id"
cp "$sce_sys/param.json" "$title/sce_sys/param.json"
if [ -f "$host_elf" ]; then
    mkdir -p "$title/payloads"
    cp "$host_elf" "$title/payloads/prospero_sunshine.elf"
    echo "app/build.sh: bundled $(basename "$host_elf") ($(stat -c %s "$host_elf") bytes)"
else
    echo "app/build.sh: WARNING: no host ELF at $host_elf; the app will use the one on the console" >&2
fi
mkdir -p "$title/licenses"
cp "$repo/LICENSE" "$title/licenses/ProsperoSunshine-LICENSE.txt"
# The bundled host links these statically.
[ ! -d "$repo/out/deps/licenses" ] || cp "$repo"/out/deps/licenses/* "$title/licenses/"
cp -r "$repo/licenses/ps5-payload-sdk-runtime" "$repo/licenses/THIRD_PARTY.md" "$title/licenses/"
cp "$kit/LICENSE" "$title/licenses/ps5-homebrew-ui-LICENSE.txt"
cp "$kit/THIRD_PARTY_NOTICES.md" "$title/licenses/ps5-homebrew-ui-THIRD_PARTY_NOTICES.md"
printf '%s\n' "ps5-homebrew-ui (GPL-3.0-or-later): $kit_url at $kit_rev" > "$title/licenses/SOURCES.txt"
rm -rf "${out:?}/$title_id" "${out:?}/$title_id$suffix.zip"
mkdir -p "$out"
cp -a "$title" "$out/"
(cd "$out" && python3 -m zipfile -c "$title_id$suffix.zip" "$title_id")
python3 "$stage/tools/zip-open-modes.py" "$out/$title_id$suffix.zip"
echo "app/build.sh: $out/$title_id$suffix.zip, $flavor ($(stat -c %s "$out/$title_id/eboot.bin") bytes eboot.bin)"

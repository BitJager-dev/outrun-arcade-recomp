#!/usr/bin/env bash
# Native Linux (Vulkan) build: ReXGlue SDK + darkuni SDK patches + the game.
#
#   tools/build_linux.sh [work_dir]
#
# work_dir (default: ./out/linux-deps) holds the SDK checkout, the patch repo
# and the SDK install; it is reused on later runs (the SDK is only rebuilt when
# its install folder is missing). The game ends up in ./dist-linux/.
#
# Needs: clang (20+), cmake (3.25+), ninja, git, plus the SDK's system
# libraries (see README "Linux").
set -euo pipefail

SDK_REF="${REXGLUE_SDK_REF:-v0.9.0}"
PATCHES_REF="${REXGLUE_PATCHES_REF:-main}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mkdir -p "${1:-$ROOT/out/linux-deps}" && cd "${1:-$ROOT/out/linux-deps}" && pwd)"
SDK_SRC="$WORK/rexglue-sdk"
PATCHES="$WORK/rexglue-sdk-builds"
SDK_INSTALL="$WORK/rexglue-sdk-install"
JOBS="${JOBS:-$(nproc)}"
export CC="${CC:-clang}" CXX="${CXX:-clang++}"

# ---------- ReXGlue SDK ----------
# Rebuilt when patches/sdk/ changes (the install marker holds its hash).
SDK_STAMP="$SDK_REF $(cat "$ROOT"/patches/sdk/*.patch 2>/dev/null | sha1sum | cut -c1-12)"
if [[ "$(cat "$SDK_INSTALL/.installed" 2>/dev/null)" != "$SDK_STAMP" ]]; then
  if [[ ! -d "$SDK_SRC" ]]; then
    git clone --branch "$SDK_REF" --recurse-submodules --shallow-submodules \
      https://github.com/rexglue/rexglue-sdk.git "$SDK_SRC"
  fi
  if [[ ! -d "$PATCHES" ]]; then
    git clone https://github.com/darkuni/rexglue-sdk-builds.git "$PATCHES"
    git -C "$PATCHES" checkout "$PATCHES_REF"
  fi
  # Same patch set as the Windows CI package (patches/ in name order;
  # *.ffmpeg.patch apply inside thirdparty/FFmpeg). Skipped if already applied.
  if [[ ! -f "$SDK_SRC/.outrun_patched" ]]; then
    for p in "$PATCHES"/patches/*.patch; do
      [[ -e "$p" ]] || continue
      if [[ "$p" == *.ffmpeg.patch ]]; then
        git -C "$SDK_SRC/thirdparty/FFmpeg" apply --whitespace=nowarn "$p"
      else
        git -C "$SDK_SRC" apply --whitespace=nowarn "$p"
      fi
      echo "SDK patch: $(basename "$p")"
    done
    touch "$SDK_SRC/.outrun_patched"
  fi
  # This repo's Linux fixes (patches/sdk/, upstream SDK fixes backported to
  # the pinned tag). Each one is skipped if already applied.
  for p in "$ROOT"/patches/sdk/*.patch; do
    [[ -e "$p" ]] || continue
    if git -C "$SDK_SRC" apply --reverse --check "$p" 2>/dev/null; then
      continue
    fi
    git -C "$SDK_SRC" apply --whitespace=nowarn "$p"
    echo "SDK patch: $(basename "$p")"
  done
  cmake -S "$SDK_SRC" -B "$SDK_SRC/out/build/linux-amd64" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_C_FLAGS=-march=x86-64-v3 -DCMAKE_CXX_FLAGS=-march=x86-64-v3 \
    -DREXGLUE_ENABLE_TRACY=OFF \
    -DCMAKE_INSTALL_PREFIX="$SDK_INSTALL"
  cmake --build "$SDK_SRC/out/build/linux-amd64" --target install --parallel "$JOBS"
  echo "$SDK_STAMP" > "$SDK_INSTALL/.installed"
fi

# ---------- Game ----------
cmake -S "$ROOT" -B "$ROOT/out/linux" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_C_FLAGS=-march=x86-64-v3 -DCMAKE_CXX_FLAGS=-march=x86-64-v3 \
  -DCMAKE_PREFIX_PATH="$SDK_INSTALL"
cmake --build "$ROOT/out/linux" --parallel "$JOBS"

# ---------- Stage ----------
DIST="$ROOT/dist-linux"
mkdir -p "$DIST/licenses"
cp "$ROOT/out/linux/outrun_arcade" "$DIST/"
cp "$ROOT/tools/xma_clips.txt" "$DIST/"
cp "$ROOT/out/linux/outrun_arcade.map" "$DIST/" 2>/dev/null || true
find "$ROOT/out/linux" -maxdepth 1 -name '*.so*' -exec cp -P {} "$DIST/" \;
find "$SDK_INSTALL" -path '*/lib*' -name '*.so*' -exec cp -Pn {} "$DIST/" \; 2>/dev/null || true
cp "$ROOT/LICENSE" "$DIST/licenses/outrun-arcade-recomp-LICENSE.txt"
[[ -d "$SDK_INSTALL/share/licenses" ]] && cp -r "$SDK_INSTALL/share/licenses/." "$DIST/licenses/"
echo
echo "Built: $DIST/outrun_arcade"
echo "Put your XBLA package (or an extracted 'assets' folder) next to it and run it."

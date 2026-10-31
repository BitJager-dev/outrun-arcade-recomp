#!/usr/bin/env bash
# Pack the current commit as a release into an archive folder (NAS):
#
#   tools/ship_release.sh [dest] [--tested "what was tested"] [--no-windows]
#
# dest (default $SHIP_DEST) is the game's archive folder. It gets:
#   releases/<date>-<commit>/  linux tar.gz, windows zip (no game data) + NOTES.md
#   build/                     source-<commit>.bundle, sdk-patches/, prebuilt SDK
#   LATEST                     name of the newest release
# README.md and build/BUILD.md in dest are kept by hand; the line between
# <!-- latest --> markers in README.md is updated.
#
# Linux is built here (tools/build_linux.sh, SDK work folder $SHIP_SDK_WORK,
# default out/linux-deps). Windows is the GitHub Actions artifact built from the
# same commit: push the branch first and wait for "Build Windows" to succeed.
# Needs: gh (logged in), bsdtar.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME=outrun-online-arcade
DEST="${SHIP_DEST:-}" TESTED="" WINDOWS=1
while [[ $# -gt 0 ]]; do
  case "$1" in
    --tested) TESTED="$2"; shift 2 ;;
    --no-windows) WINDOWS=0; shift ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) DEST="$1"; shift ;;
  esac
done
[[ -n "$DEST" ]] || { echo "usage: $0 <dest> (or set SHIP_DEST)" >&2; exit 2; }
die() { echo "ship_release: $*" >&2; exit 1; }

cd "$ROOT"
git diff --quiet HEAD || die "uncommitted changes: commit first, a release must match a commit"
SHA="$(git rev-parse HEAD)"
BRANCH="$(git branch --show-current)"
NAME="$(date +%F)-${SHA:0:7}"
REL="$DEST/releases/$NAME"
[[ -e "$REL" ]] && die "$REL already exists"
WORK="${SHIP_SDK_WORK:-$ROOT/out/linux-deps}"
STAGE="$ROOT/out/ship/$NAME"
rm -rf "$STAGE" && mkdir -p "$STAGE"

# ---------- Windows artifact (checked first: fails fast) ----------
if (( WINDOWS )); then
  REPO="${SHIP_GH_REPO:-$(git remote get-url fork 2>/dev/null || git remote get-url origin)}"
  REPO="$(sed -E 's#(https://github.com/|git@github.com:)##; s#\.git$##' <<<"$REPO")"
  RUN="$(gh run list --repo "$REPO" --workflow build-windows.yml --commit "$SHA" --status success \
           --limit 1 --json databaseId -q '.[0].databaseId')"
  [[ -n "$RUN" ]] || die "no successful Build Windows run for ${SHA:0:7} on $REPO (push, wait, or --no-windows)"
  gh run download "$RUN" --repo "$REPO" -n outrun_arcade-win-amd64 -D "$STAGE/win/$GAME"
  WIN_URL="https://github.com/$REPO/actions/runs/$RUN"
fi

# ---------- Linux build ----------
tools/build_linux.sh "$WORK"
mkdir -p "$STAGE/linux/$GAME"
cp -r dist-linux/. "$STAGE/linux/$GAME/"
cat > "$STAGE/linux/$GAME/run.sh" <<'EOF'
#!/usr/bin/env bash
# Start OutRun Online Arcade (native Linux, Vulkan). Settings (outrun_arcade.toml),
# saves (userdata/) and logs/ are kept next to this script.
cd "$(dirname "$(readlink -f "$0")")" && exec ./outrun_arcade "$@"
EOF
chmod +x "$STAGE/linux/$GAME/run.sh"

# ---------- Release ----------
mkdir -p "$REL"
tar -C "$STAGE/linux" -czf "$REL/$GAME-linux-x64.tar.gz" "$GAME"
(( WINDOWS )) && bsdtar --format zip -C "$STAGE/win" -cf "$REL/$GAME-windows-x64.zip" "$GAME"
{
  echo "# $GAME $NAME"
  echo
  echo "- Commit: \`$SHA\` (branch \`$BRANCH\`)"
  echo "- SDK: \`$(cat "$WORK/rexglue-sdk-install/.installed")\` (tag + hash of patches/sdk)"
  echo "- Linux: built here with $("${CXX:-clang++}" --version | head -1)"
  if (( WINDOWS )); then echo "- Windows: GitHub Actions $WIN_URL"; else echo "- Windows: not included"; fi
  echo "- Tested: ${TESTED:-not yet}"
  echo
  echo "## Changes on top of upstream main"
  echo
  git log --no-merges --format='- %h %s' origin/main..HEAD
} > "$REL/NOTES.md"

# ---------- Build info ----------
mkdir -p "$DEST/build/sdk-patches/darkuni" "$DEST/build/sdk-patches/this-repo"
git bundle create "$DEST/build/source-${SHA:0:7}.bundle" "$BRANCH" main
cp "$WORK"/rexglue-sdk-builds/patches/*.patch "$DEST/build/sdk-patches/darkuni/"
cp patches/sdk/*.patch "$DEST/build/sdk-patches/this-repo/"
SDK_TGZ="$DEST/build/sdk-prebuilt-$(tr ' ' '-' < "$WORK/rexglue-sdk-install/.installed")-linux-x64.tar.gz"
[[ -e "$SDK_TGZ" ]] || tar -C "$WORK" -czf "$SDK_TGZ" rexglue-sdk-install

echo "$NAME" > "$DEST/LATEST"
if [[ -f "$DEST/README.md" ]]; then
  sed -i "s#<!-- latest -->.*<!-- /latest -->#<!-- latest -->\`releases/$NAME/\`<!-- /latest -->#" "$DEST/README.md"
fi
rm -rf "$STAGE"
echo "Shipped $REL"

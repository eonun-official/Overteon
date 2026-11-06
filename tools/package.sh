#!/bin/sh
# ===========================================================================
#  package.sh — build and install the LV2 bundle.
#
#  JUCE writes a fresh bundle to
#      build/HarmonicInjector_artefacts/Release/LV2/OnyxTEON.lv2
#  on every build.  This copies it to ~/.lv2 (or $LV2_DEST) so hosts can load
#  it, and verifies the copy afterwards — a stale bundle is easy to miss and
#  then every "the plug-in still has the old bug" report is really about the
#  copy in ~/.lv2, not about the build.
#
#    ./package.sh              build if needed, then install to ~/.lv2
#    ./package.sh --no-build   install whatever is already built
#    ./package.sh --dest DIR   install somewhere else
#    ./package.sh --check      only compare installed vs built
#
#  Exit status is 0 when the installed bundle matches the built one.
# ===========================================================================
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
BUILT="$ROOT/build/HarmonicInjector_artefacts/Release/LV2/OnyxTEON.lv2"
DEST_ROOT=${LV2_DEST:-$HOME/.lv2}
DEST="$DEST_ROOT/OnyxTEON.lv2"
URI="https://eonun.com/plugins/OnyxTEON"

build=1
check_only=0
while [ $# -gt 0 ]; do
    case "$1" in
        --no-build) build=0 ;;
        --check)    check_only=1 ;;
        --dest)     shift; DEST_ROOT=$1; DEST="$DEST_ROOT/OnyxTEON.lv2" ;;
        *) echo "usage: $0 [--no-build] [--check] [--dest DIR]" >&2; exit 2 ;;
    esac
    shift
done

# ---- 1. build -------------------------------------------------------------
if [ "$build" = 1 ] && [ "$check_only" = 0 ]; then
    echo "building the LV2 bundle"
    if ! ninja -C "$ROOT/build" HarmonicInjector_LV2 2>&1 | tail -3; then
        # some trees name the target differently; fall back to a plain build
        ninja -C "$ROOT/build" >/dev/null 2>&1 || {
            echo "build failed" >&2; exit 1; }
    fi
fi

[ -f "$BUILT/libOnyxTEON.so" ] || {
    echo "no bundle at $BUILT — build first" >&2; exit 1; }

# ---- 2. compare ------------------------------------------------------------
same=0
if [ -f "$DEST/libOnyxTEON.so" ] && cmp -s "$BUILT/libOnyxTEON.so" "$DEST/libOnyxTEON.so"; then
    same=1
fi

if [ "$check_only" = 1 ]; then
    if [ "$same" = 1 ]; then
        echo "installed bundle matches the build: $DEST"
        exit 0
    fi
    echo "installed bundle DIFFERS from the build: $DEST" >&2
    [ -f "$DEST/libOnyxTEON.so" ] && ls -la "$DEST/libOnyxTEON.so" "$BUILT/libOnyxTEON.so" >&2
    exit 1
fi

if [ "$same" = 1 ]; then
    echo "already up to date: $DEST"
    exit 0
fi

# ---- 3. install ------------------------------------------------------------
# Replaced as a whole directory rather than file by file: a bundle missing one
# of its four files loads as nothing at all, and a half-copied one is worse
# than none because the host caches the failure.
mkdir -p "$DEST_ROOT"
TMP="$DEST.new.$$"
rm -rf "$TMP"
cp -a "$BUILT" "$TMP" || { echo "copy failed" >&2; rm -rf "$TMP"; exit 1; }
rm -rf "$DEST.old.$$"
[ -d "$DEST" ] && mv "$DEST" "$DEST.old.$$"
mv "$TMP" "$DEST"
rm -rf "$DEST.old.$$"

# ---- 4. verify -------------------------------------------------------------
if ! cmp -s "$BUILT/libOnyxTEON.so" "$DEST/libOnyxTEON.so"; then
    echo "installed copy does not match the build — something is wrong" >&2
    exit 1
fi
for f in manifest.ttl dsp.ttl ui.ttl libOnyxTEON.so; do
    [ -f "$DEST/$f" ] || { echo "bundle is missing $f" >&2; exit 1; }
done
grep -q "$URI" "$DEST/manifest.ttl" || {
    echo "manifest does not declare $URI" >&2; exit 1; }

echo "installed $(du -sh "$DEST" | cut -f1) to $DEST"
echo "  URI $URI"

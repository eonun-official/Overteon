#!/bin/sh
# ===========================================================================
#  make-package.sh — assemble a shippable archive from a product directory.
#
#  This is not package.sh: that one installs the LV2 bundle into ~/.lv2 for this
#  machine.  This one produces the file you hand to somebody else -- every
#  format, the factory presets, the changelog, and a README that says what the
#  build is -- and leaves it next to the other build output:
#
#      Build/packages/Overteon_20261116-191811_unknown_linux_x86_64.zip
#                     Overteon_20261116-191811_unknown_linux_x86_64.zip.sha256
#
#  The archive name is the product directory's own name, so an archive can
#  always be traced back to the build it came from, and the two never drift.
#  Inside is one folder named for the version and the platform:
#
#      Overteon-0.9.20-linux-x86_64/
#          VST3/Overteon.vst3
#          LV2/Overteon.lv2/            (including presets/Factory)
#          VST/Overteon.so
#          Standalone/Overteon
#          README.txt                   what this is, and how to install it
#          docs/CHANGELOG_*.md
#
#    ./make-package.sh                    newest Linux product directory
#    ./make-package.sh --os windows       newest Windows one
#    ./make-package.sh --product DIR      one specific directory
#    ./make-package.sh --out DIR          somewhere other than Build/packages
#    ./make-package.sh --force            re-pack even if the archive is current
#    ./make-package.sh --os windows --with-qt
#                                         also carry the Qt6 runtime (a Windows
#                                         archive is not runnable without it)
#
#  Products that are developer tools rather than product -- ovctl, the headless
#  tests, overteon-qml-preview, libOverteon_SharedCode.a -- are deliberately not
#  included: they are not something a user installs.
# ===========================================================================
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
. "$HERE/product.sh"

OS_NAME=linux
PRODUCT=
OUT=
FORCE=0
WITH_QT=0

while [ $# -gt 0 ]; do
    case "$1" in
        --os)      shift; OS_NAME=${1:-linux} ;;
        --product) shift; PRODUCT=${1:-} ;;
        --out)     shift; OUT=${1:-} ;;
        --force)   FORCE=1 ;;
        --with-qt) WITH_QT=1 ;;
        -h|--help) sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

if [ -z "$PRODUCT" ]; then
    PRODUCT=$(product_dir "$OS_NAME") || {
        echo "nothing built for $OS_NAME yet — configure and run ninja first" >&2
        exit 1
    }
fi
[ -d "$PRODUCT" ] || { echo "no such product directory: $PRODUCT" >&2; exit 1; }

OUT=${OUT:-$ROOT/Build/packages}
mkdir -p "$OUT"

# ---- what is being packed ---------------------------------------------------
# The product directory name carries every field of the name, so the archive
# name is taken from it rather than recomputed: recomputing could produce a
# timestamp that no longer matches the build it describes.
STAMP_NAME=$(basename "$PRODUCT")

# Overteon_<BuildName?>_<Time>_<Host>_<OS>_<Platform>
case "$STAMP_NAME" in
    *_"${OS_NAME}"_*) : ;;
    *) echo "warning: $STAMP_NAME does not look like a $OS_NAME product directory" >&2 ;;
esac
# The OS field is matched rather than counted.  Counting from the end breaks on
# a platform name that itself has an underscore (x86_64), which turned
# Overteon-0.9.20-linux-x86_64 into Overteon-0.9.20-x86-64 -- a name that no
# longer says which of the two builds it is.
BUILD_OS=$OS_NAME
PLATFORM=${STAMP_NAME##*_"${BUILD_OS}"_}
[ -n "$PLATFORM" ] && [ "$PLATFORM" != "$STAMP_NAME" ] || PLATFORM="unknown"

# One source of truth for the version: the same project(VERSION) the binary
# reports in its About panel and in the VST3 moduleinfo.
VERSION=$(sed -n 's/^project(Overteon VERSION \([0-9][0-9.]*\)).*/\1/p' \
          "$ROOT/Config/CMakeLists.txt" | head -1)
[ -n "$VERSION" ] || { echo "could not read the version from Config/CMakeLists.txt" >&2; exit 1; }

STAGE_NAME="Overteon-${VERSION}-${BUILD_OS}-${PLATFORM}"
ARCHIVE="$OUT/${STAMP_NAME}.zip"

# ---- nothing to do? ---------------------------------------------------------
# With OVERTEON_AUTO_PACKAGE=ON this runs after every build, so it has to be
# cheap when the products have not changed since the last archive.
if [ "$FORCE" = 0 ] && [ -f "$ARCHIVE" ]; then
    stale=$(find "$PRODUCT" -newer "$ARCHIVE" -type f 2>/dev/null | head -1)
    if [ -z "$stale" ]; then
        echo "package is up to date: $ARCHIVE"
        exit 0
    fi
fi

# ---- stage ------------------------------------------------------------------
WORK="$OUT/.stage.$$"
rm -rf "$WORK"
STAGE="$WORK/$STAGE_NAME"
mkdir -p "$STAGE"
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

copied=

copy_format() {
    _src=$1
    _dst=$2
    [ -d "$PRODUCT/$_src" ] || return 1
    # An empty format directory means the format was not built; a bundle with
    # nothing in it is worse than a missing one, because it looks installable.
    [ -n "$(find "$PRODUCT/$_src" -type f 2>/dev/null | head -1)" ] || return 1
    # The *contents* are copied, not the directory: JUCE already names the
    # bundle inside the format folder (VST3/Overteon.vst3), so copying the
    # folder itself produced VST3/Overteon.vst3/Overteon.vst3/.
    mkdir -p "$STAGE/$_dst"
    cp -a "$PRODUCT/$_src/." "$STAGE/$_dst/" || {
        echo "copy failed: $_src" >&2; exit 1; }
    copied="$copied $_dst"
    return 0
}

copy_format VST3       VST3
copy_format LV2        LV2
copy_format Standalone Standalone
# VST2 has no bundle layout: on Linux the .so is loaded by name, and JUCE builds
# it as libOverteon.so because it is also the shared-code library.  Renaming it
# is what makes the folder installable as-is (~/.vst/Overteon.so).
if [ -f "$PRODUCT/VST/libOverteon.so" ]; then
    mkdir -p "$STAGE/VST"
    cp "$PRODUCT/VST/libOverteon.so" "$STAGE/VST/Overteon.so"
    copied="$copied VST/Overteon.so"
elif [ -f "$PRODUCT/VST/Overteon.dll" ]; then
    mkdir -p "$STAGE/VST"
    cp "$PRODUCT/VST/Overteon.dll" "$STAGE/VST/Overteon.dll"
    copied="$copied VST/Overteon.dll"
fi

[ -n "$copied" ] || { echo "no plug-in formats found in $PRODUCT" >&2; exit 1; }

# The factory presets go at the archive root as well as beside the LV2 bundle.
# OvJuceSource::presetsDirectory() looks for <parent of the executable's
# directory>/presets, which for the standalone is the archive root -- so without
# this a fresh install opens the preset browser on an empty bank.
if [ -d "$ROOT/Assets/presets/Factory" ]; then
    mkdir -p "$STAGE/presets/Factory" "$STAGE/presets/User"
    cp "$ROOT"/Assets/presets/Factory/*.hinj "$STAGE/presets/Factory/" 2>/dev/null
    with_presets=1
fi

# Build-time only files that live beside the real product on Windows: the import
# libraries and debug databases link against the DLL, they do not load it.
# Harmless on Linux (nothing matches) but the pattern is kept cross-platform
# because a Windows product directory is packaged on a Linux host.
find "$STAGE" -type f \( -name '*.lib' -o -name '*.exp' -o -name '*.pdb' \
                          -o -name '*.ilk' -o -name '*.obj' \) -delete 2>/dev/null

# A Windows archive is not runnable on its own: the editor is a Qt front end and
# the plug-in resolves Qt6Core.dll and friends at load time.  Off by default so
# the archive stays a download rather than a 100 MB one; --with-qt copies the
# runtime in and the README then says so.
#
# Only the reachable part of Qt goes in.  Copying the whole install put 110 DLLs
# and 274 MB beside a 12 MB plug-in, when the import graph reaches nine modules
# and one platform plug-in -- 35 MB.  Scripts/qt-closure.py computes that set
# from the PE import tables, which works on this Linux host because
# llvm-objdump reads PE.
if [ "$WITH_QT" = 1 ]; then
    QT_DIR=${QT_MSVC:-$HOME/Qt-msvc/6.9.3/msvc2022_64}
    [ -d "$QT_DIR/bin" ] || { echo "--with-qt: no Qt at $QT_DIR (set QT_MSVC)" >&2; exit 1; }
    # The platform plug-in is loaded by name at run time, so no import table
    # mentions it: it has to be named here or the editor cannot open a window.
    _qpa=
    [ -f "$QT_DIR/plugins/platforms/qwindows.dll" ] && _qpa=plugins/platforms/qwindows.dll
    _any=$(find "$STAGE" \( -name '*.dll' -o -name '*.exe' \) | head -1)
    [ -n "$_any" ] || { echo "--with-qt: no PE file staged" >&2; exit 1; }
    if [ -n "$_qpa" ]; then
        python3 "$ROOT/Scripts/qt-closure.py" "$_any" --qt "$QT_DIR" \
                --copy-to "$STAGE/qt" --extra "$_qpa" || exit 1
    else
        python3 "$ROOT/Scripts/qt-closure.py" "$_any" --qt "$QT_DIR" \
                --copy-to "$STAGE/qt" || exit 1
    fi
    with_qt=1
fi


# ---- the paperwork ----------------------------------------------------------
for _doc in "$ROOT"/Assets/docs/CHANGELOG_*.md; do
    [ -f "$_doc" ] || continue
    mkdir -p "$STAGE/docs"
    cp "$_doc" "$STAGE/docs/"
done

{
    printf 'Overteon %s\n' "$VERSION"
    printf '=================\n\n'
    printf 'Build      %s\n' "$STAMP_NAME"
    printf 'Platform   %s %s\n' "$BUILD_OS" "$PLATFORM"
    printf 'Formats    %s\n' "$(printf '%s\n' $copied | sed 's|/.*||' | sort -u | tr '\n' ' ')"
    [ "${with_presets:-0}" = 1 ] && printf 'Presets    presets/Factory (also beside the LV2 bundle)\n'
    printf '\n'

    cat <<'TXT'
The editor is Qt6/QML; the DSP and the parameter set are the ones this project
has always had, so .hinj presets and host sessions are interchangeable with
earlier builds.

Install
-------
Linux
  VST3    cp -r VST3/Overteon.vst3          ~/.vst3/
  LV2     cp -r LV2/Overteon.lv2            ~/.lv2/
  VST2    cp    VST/Overteon.so             ~/.vst/
          (VST2 is deprecated by Steinberg; prefer VST3)
  The factory presets are in presets/Factory at the archive root, and again
  beside the LV2 bundle; they are ordinary .hinj files that the editor's preset
  browser reads.  Nothing is written into the bundle itself.
  Qt6 (Core, Gui, Qml, Quick, Widgets, OpenGL) must be installed — the editor
  links it dynamically, as the licence requires.

Windows
  VST3    copy VST3\Overteon.vst3     to  C:\Program Files\Common Files\VST3\
  LV2     copy LV2\Overteon.lv2       to  a folder on your LV2 path
  VST2    copy VST\Overteon.dll       to  C:\Program Files\VstPlugins\
  The plug-in needs the Qt6 runtime DLLs beside it, or Qt6 on PATH.

Uninstall
---------
Delete the bundle you copied. Presets are the user's own files and are left
alone.

Licence
-------
Qt6 is used as a dynamically linked LGPL library; the plug-in itself is BSD.
See the project repository for the full text.
TXT
} > "$STAGE/README.txt"

if [ "${with_qt:-0}" = 1 ]; then
    cat >> "$STAGE/README.txt" <<'TXT'

Qt runtime
----------
This archive carries the Qt6 runtime it was built against in qt/.  Copy that
folder's DLLs and plugin folders next to the plug-in, or add qt/ to PATH before
starting the host; without them the editor cannot be created and the plug-in
loads without a window.
TXT
fi


# ---- archive ----------------------------------------------------------------
rm -f "$ARCHIVE"
( cd "$WORK" && zip -r -q -9 "$ARCHIVE" "$STAGE_NAME" ) || {
    echo "zip failed" >&2; exit 1; }

# ---- verify -----------------------------------------------------------------
# A silently truncated archive is the classic packaging bug, so the check is
# done on the file that was actually written, not on the staging tree.
unzip -t "$ARCHIVE" >/dev/null 2>&1 || { echo "archive is corrupt: $ARCHIVE" >&2; exit 1; }
for _must in VST3/Overteon.vst3 LV2/Overteon.lv2 README.txt; do
    unzip -l "$ARCHIVE" "$STAGE_NAME/$_must*" >/dev/null 2>&1 || {
        echo "archive is missing $_must" >&2; exit 1; }
done

( cd "$OUT" && sha256sum "$(basename "$ARCHIVE")" > "$(basename "$ARCHIVE").sha256" )

echo "packaged $(du -h "$ARCHIVE" | cut -f1) -> $ARCHIVE"
echo "  contains:$copied"
( cd "$OUT" && cat "$(basename "$ARCHIVE").sha256" | sed 's/^/  sha256 /' )

#!/bin/sh
# ===========================================================================
#  deploy-win.sh — assemble a self-contained Windows folder for wine/Windows.
#
#  Qt needs more than the .exe: the platform plug-in (qwindows.dll), the QML
#  import tree, and the Qt modules the binary actually imports.  windeployqt.exe
#  can do this but it is a Windows binary; Scripts/qt-closure.py does it here by
#  reading the PE import tables, and copies only what is reachable from the
#  binary rather than every Qt6*.dll the install happens to contain (110 of them,
#  274 MB, against 9 and 35 MB).
#
#    ./deploy-win.sh [product-dir] [out-dir]
#
#  Defaults: the newest Build/<BuildName>_..._windows_<arch>/ product directory,
#  and Build/dist-win for the assembled folder.
# ===========================================================================
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
. "$HERE/product.sh"
BUILD=${1:-$(product_dir windows)}
OUT=${2:-$ROOT/Build/dist-win}
QT=${QT_MSVC:-$HOME/Qt-msvc/6.9.3/msvc2022_64}
EXE=overteon-qml-preview.exe

[ -f "$BUILD/$EXE" ] || { echo "no $EXE in $BUILD — build first" >&2; exit 1; }
[ -d "$QT" ] || { echo "Qt for Windows not found at $QT" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT"
cp "$BUILD/$EXE" "$OUT/"

echo "copying the reachable Qt runtime..."
python3 "$ROOT/Scripts/qt-closure.py" "$OUT/$EXE" --qt "$QT" --copy-to "$OUT" \
        --extra plugins/platforms/qwindows.dll --report

echo "copying QML imports..."
cp -r "$QT/qml" "$OUT/qml"


# Qt looks for qt.conf next to the .exe; without it the plugin path is the
# compiled-in one and nothing loads.
cat > "$OUT/qt.conf" <<'EOF'
[Paths]
Prefix = .
Plugins = .
Qml2Imports = qml
EOF

# The MSVC runtime is normally supplied by the VC++ redistributable.  Copy it
# from the Qt install when present so the folder runs on a bare Windows/wine.
for f in vcruntime140.dll vcruntime140_1.dll msvcp140.dll msvcp140_1.dll \
         msvcp140_2.dll concrt140.dll; do
    for src in "$QT/bin/$f" /usr/x86_64-w64-mingw32/bin/$f; do
        [ -f "$src" ] && { cp "$src" "$OUT/"; break; }
    done
done

echo "done: $OUT ($(du -sh "$OUT" | cut -f1), $(ls "$OUT"/*.dll | wc -l) DLLs)"
echo
echo "run it:"
echo "  WINEPREFIX=\$HOME/.wine-onyx DISPLAY=:3 wine $OUT/$EXE --size 1040x772"
echo
echo "(no wine-specific flags needed: the app detects wine, selects the"
echo " OpenGL RHI and renders through QQuickWidget.  --no-compat forces the"
echo " normal Qt Quick window, e.g. on real Windows.)"

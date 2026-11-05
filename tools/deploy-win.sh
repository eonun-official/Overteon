#!/bin/sh
# ===========================================================================
#  deploy-win.sh — assemble a self-contained Windows folder for wine/Windows.
#
#  Qt needs more than the .exe: the platform plugin (qwindows.dll), the QML
#  import tree, and every Qt6*.dll they reference.  windeployqt.exe can do it
#  but it is a Windows binary; this script copies the same set explicitly, so
#  it also works on Linux.
#
#    ./deploy-win.sh [build-dir] [out-dir]
#
#  Defaults: build-win  dist-win
# ===========================================================================
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
BUILD=${1:-$ROOT/build-win}
OUT=${2:-$ROOT/dist-win}
QT=${QT_MSVC:-$HOME/Qt-msvc/6.9.3/msvc2022_64}
EXE=overteon-qml-preview.exe

[ -f "$BUILD/$EXE" ] || { echo "no $EXE in $BUILD — build first" >&2; exit 1; }
[ -d "$QT" ] || { echo "Qt for Windows not found at $QT" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT"
cp "$BUILD/$EXE" "$OUT/"

echo "copying Qt runtime..."
for f in "$QT"/bin/Qt6*.dll; do
    cp "$f" "$OUT/"
done

echo "copying Qt plugins..."
for d in platforms imageformats styles iconengines generic networkinformation; do
    [ -d "$QT/plugins/$d" ] || continue
    mkdir -p "$OUT/$d"
    cp "$QT/plugins/$d"/*.dll "$OUT/$d/" 2>/dev/null || true
done

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
echo "  WINEPREFIX=\$HOME/.wine-onyx DISPLAY=:3 wine $OUT/$EXE"

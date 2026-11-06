#!/bin/sh
# ===========================================================================
#  Blur demo: a 2x2 grid in one image file.
#
#      ./run.sh [sigma] -g     render, then show it
#      ./run.sh [sigma] -s     render only, print the numbers
#
#  Layout, all four cells the same crop at the same size:
#
#      +---------------+---------------+     left column : the photo, untouched
#      |   original    |  + BLUR (GPU) |     top-right   : GPU two-pass over its
#      +---------------+---------------+                   RIGHT HALF only
#      |   original    |  + BLUR (CPU) |     bottom-right: CPU OvFrost::
#      +---------------+---------------+                   dualGaussian, same
#
#  Only the right half of each right-hand cell carries the blur layer, so the
#  seam down the middle of that cell is what makes it readable.
# ===========================================================================
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
PREVIEW="$ROOT/build/overteon-qml-preview"

SIGMA=6
SHOW=1
for a in "$@"; do
    case "$a" in
        -g) SHOW=1 ;;
        -s) SHOW=0 ;;
        *)  SIGMA=$a ;;
    esac
done

# A fixed 1:1 display: $DISPLAY on a HiDPI screen gives 1.302x captures, and the
# cell boundaries then do not land on the pixel grid the QML uses.
DISP=${DISPLAY_TARGET:-:3}
PHOTO=${PHOTO:-$(ls /home/x_x/BingWallpaper/*.jpg 2>/dev/null | head -1)}
OUT=${OUT:-/tmp/blurdemo}
W=1280
H=960

[ -x "$PREVIEW" ] || { echo "build first: ninja -C $ROOT/build" >&2; exit 1; }
[ -n "$PHOTO" ]   || { echo "no photo in /home/x_x/BingWallpaper" >&2; exit 1; }

mkdir -p "$OUT"
GRID="$OUT/grid.png"

tmp=$(mktemp /tmp/paneXXXX.qml)
sed -e "s|property string photo: \"\"|property string photo: \"$PHOTO\"|" \
    -e "s|property real   sigma: 6.0|property real   sigma: $SIGMA|" \
    "$HERE/pane.qml" > "$tmp"
DISPLAY=$DISP "$PREVIEW" --size "${W}x${H}" --freeze \
    --qml "$tmp" --shot "$GRID" --shot-delay 3000 >/dev/null 2>&1
rm -f "$tmp"
[ -s "$GRID" ] || { echo "render failed" >&2; exit 1; }

STATS=$(python3 - "$GRID" "$SIGMA" <<'PY'
import sys
import numpy as np
from PIL import Image

a = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(np.float32)
sigma = sys.argv[2]
ch, cw = a.shape[0] // 2, a.shape[1] // 2

def detail(x):
    g = x.mean(axis=2)
    return float((4 * g[1:-1, 1:-1] - g[:-2, 1:-1] - g[2:, 1:-1]
                  - g[1:-1, :-2] - g[1:-1, 2:]).var())

cells = {"TL": (0, 0), "TR": (0, cw), "BL": (ch, 0), "BR": (ch, cw)}
vals = {}
for n, (y, x) in cells.items():
    c = a[y:y + ch, x:x + cw]
    vals[n] = (detail(c[:, :cw // 2]), detail(c[:, cw // 2:]))

# The left column must stay as sharp as the original; each right column cell
# must be sharp on its left half and heavily reduced on its right half.
# The bound is relative to the original, because a small sigma cannot flatten a
# high-contrast photo completely: at sigma 1 the right half still reads ~18
# against 822, which is 46x less detail and plainly blurred.
orig = vals["TL"][1]
ok = (vals["BL"][1] > orig * 0.8                     # left column untouched
      and vals["TR"][0] > orig * 0.8                 # TR sharp on its left
      and vals["TR"][1] < orig * 0.2                 # TR blurred on its right
      and vals["BR"][0] > orig * 0.8                 # BR sharp on its left
      and vals["BR"][1] < orig * 0.2)                # BR blurred on its right
print(f"sigma {sigma}   original {vals['TL'][1]:.0f}   "
      f"GPU blur {vals['TR'][1]:.1f}   CPU blur {vals['BR'][1]:.1f}   "
      f"{'PASS' if ok else 'FAIL'}")
PY
)

echo "$STATS"
echo "$GRID"

[ "$SHOW" = 1 ] || exit 0
python3 "$HERE/compare.py" "$GRID" "$STATS"

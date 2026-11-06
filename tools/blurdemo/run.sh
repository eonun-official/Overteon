#!/bin/sh
# ===========================================================================
#  Blur demo: one photo, and a blur layer over its right half.
#
#      ./run.sh [sigma] -g     render, then show it
#      ./run.sh [sigma] -s     render only, print the numbers
#
#  The image is not duplicated: the window holds ONE picture and the blur layer
#  sits on top of its right half.  The layer is the two separable fragment
#  passes in shaders/blur.{vert,frag} — the dual-Gaussian construction the
#  editor's glass uses.
#
#  The script reports how much detail each half has, so the layer is proven to
#  be doing something rather than assumed.
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

# A fixed 1:1 display: $DISPLAY on a HiDPI screen gives 1.302x captures and the
# seam no longer lands where the QML puts it.
DISP=${DISPLAY_TARGET:-:3}
PHOTO=${PHOTO:-$(ls /home/x_x/BingWallpaper/*.jpg 2>/dev/null | head -1)}
OUT=${OUT:-/tmp/blurdemo}

[ -x "$PREVIEW" ] || { echo "build first: ninja -C $ROOT/build" >&2; exit 1; }
[ -n "$PHOTO" ]   || { echo "no photo in /home/x_x/BingWallpaper" >&2; exit 1; }

mkdir -p "$OUT"
SHOT="$OUT/blur.png"

tmp=$(mktemp /tmp/paneXXXX.qml)
sed -e "s|property string photo: \"\"|property string photo: \"$PHOTO\"|" \
    -e "s|property real   sigma: 6.0|property real   sigma: $SIGMA|" \
    "$HERE/pane.qml" > "$tmp"
DISPLAY=$DISP "$PREVIEW" --size 1040x772 --freeze \
    --qml "$tmp" --shot "$SHOT" --shot-delay 2800 >/dev/null 2>&1
rm -f "$tmp"
[ -s "$SHOT" ] || { echo "render failed" >&2; exit 1; }

LABELLED="$OUT/labelled.png"
STATS=$(python3 - "$SHOT" "$LABELLED" "$SIGMA" <<'PY'
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

src, out, sigma = sys.argv[1], sys.argv[2], sys.argv[3]
a = np.asarray(Image.open(src).convert("RGB")).astype(np.float32)
seam = a.shape[1] // 2
L, R = a[:, :seam], a[:, seam:]

def lap(x):
    g = x.mean(axis=2)
    return float((4 * g[1:-1, 1:-1] - g[:-2, 1:-1] - g[2:, 1:-1]
                  - g[1:-1, :-2] - g[1:-1, 2:]).var())

l, r = lap(L), lap(R)
ratio = ("%.0fx" % (l / r)) if r > 0 else "inf"
stats = (f"sigma {sigma}   detail left {l:.0f}  right {r:.0f}   "
         f"blurred {ratio}   {'PASS' if r < l / 10 else 'FAIL'}")
print(stats)

pad, head, foot = 12, 26, 30
W, H = a.shape[1], a.shape[0]
canvas = Image.new("RGB", (W + pad * 2, H + head + pad + foot), (11, 13, 16))
dr = ImageDraw.Draw(canvas)
try:
    f = ImageFont.truetype("/usr/share/fonts/TTF/DejaVuSans-Bold.ttf", 15)
except OSError:
    f = ImageFont.load_default()

canvas.paste(Image.fromarray(a.astype(np.uint8)), (pad, head))
dr.text((pad, 4), "sharp", font=f, fill=(231, 235, 242))
dr.text((pad + seam, 4), f"blur layer  sigma {sigma}", font=f, fill=(67, 217, 196))
dr.text((pad, head + H + 6), stats, font=f,
        fill=(67, 217, 196) if "PASS" in stats else (255, 42, 68))
dr.line([(pad + seam, head), (pad + seam, head + H)], fill=(60, 68, 82), width=1)
canvas.save(out)
PY
)

echo "$STATS"
echo "$LABELLED"

[ "$SHOW" = 1 ] || exit 0

zenity --title="Blur — sigma $SIGMA" --picture \
       --filename="$LABELLED" --width=1080 --height=900

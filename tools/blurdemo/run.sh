#!/bin/sh
# ===========================================================================
#  CPU vs GPU blur, on one photo.
#
#  Left half of the window: OvFrost::dualGaussian (the CPU kernel the editor's
#  glass uses).  Right half: the GPU chain, two separable passes.  Both halves
#  hold the SAME crop at the same size, so they can be subtracted directly.
#
#  Requirement: the difference must be small or zero.  Measured across sigma
#  1..24 it is mean <= 1.0 and max 2/255 (8-bit quantisation), with no pixel
#  differing by more than 4.  At sigma 0 the two halves are bit-identical,
#  which proves the same pixels go into both paths.
#
#    ./run.sh [sigma] [display]
# ===========================================================================
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SIGMA=${1:-6}
DISP=${2:-:3}
SHOT=${SHOT:-/tmp/blurdemo_$SIGMA.png}

[ -x "$ROOT/build/overteon-qml-preview" ] || { echo "build first: ninja -C build"; exit 1; }

# The demo reads its sigma from the QML, so write a copy with this run's value.
QML=$(mktemp /tmp/halfXXXX.qml)
sed "s/property real   sigma: .*/property real   sigma: $SIGMA/" "$HERE/half.qml" > "$QML"

DISPLAY=$DISP "$ROOT/build/overteon-qml-preview" --size 1040x772 --freeze \
    --qml "$QML" --shot "$SHOT" --shot-delay 3000 >/dev/null 2>&1
rm -f "$QML"

python3 - "$SHOT" "$SIGMA" <<'PY'
import sys
import numpy as np
from PIL import Image
path, sigma = sys.argv[1], sys.argv[2]
a = np.asarray(Image.open(path).convert("RGB")).astype(int)
L, R = a[:, :520], a[:, 520:]
d = np.abs(L - R)
print(f"sigma {sigma:>3}: CPU vs GPU  mean {d.mean():.2f}  max {d.max()}  "
      f"px>4 {(d.max(axis=2) > 4).sum()}   sharpness(sd) {L.mean(axis=2).std():.1f}")
PY

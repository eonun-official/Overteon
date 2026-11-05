#!/usr/bin/env python3
"""Verify the canvas FrostEngine against a reference Gaussian.

The design's frosted glass is  backdrop-filter: blur(3px) saturate(.85)  over
the two control-pit trapezoids and the seam ellipse.  Both front ends compute
that blur themselves, so it has to be checked against what CSS would produce.

Method
  1. capture /current?nofrost=1  -> the exact content underneath the glass
  2. capture /current            -> the page with the canvas frost
  3. reference = GaussianBlur(sigma = 3) of (1)
  4. compare inside the trapezoids, skipping columns that carry HTML controls
     (text is painted *above* the glass, so it is not part of the backdrop)

Usage:  ./check-frost.py [url] [--tolerance 4.0]
Exit code 0 when every band is within tolerance.
"""
import argparse
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageFilter
import numpy as np


def shoot(url, out):
    profile = tempfile.mkdtemp(prefix="frostcheck-")
    env = dict(os.environ, MOZ_HEADLESS="1")
    subprocess.run(
        ["firefox", "--headless", "--no-remote", "--profile", profile,
         "--window-size=1040,772", "--screenshot", out, url],
        env=env, check=False, timeout=180,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(out) or os.path.getsize(out) == 0:
        raise SystemExit(f"firefox produced no screenshot for {url}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("url", nargs="?", default="http://127.0.0.1:8888/current")
    ap.add_argument("--tolerance", type=float, default=4.0,
                    help="max RMS error in 8-bit units (default 4)")
    args = ap.parse_args()

    base = args.url.split("?")[0]
    tmp = tempfile.mkdtemp(prefix="frost-")
    under_png = os.path.join(tmp, "under.png")
    frost_png = os.path.join(tmp, "frost.png")

    shoot(base + "?nofrost=1", under_png)
    shoot(base, frost_png)

    under = Image.open(under_png).convert("RGB")
    frost = np.asarray(Image.open(frost_png).convert("RGB")).astype(float)
    ref = np.asarray(under.filter(ImageFilter.GaussianBlur(3))).astype(float)

    # Geometry of the trapezoids in the 1040-wide design: the control pit is
    # 640 px wide starting at x = 200, its narrow edge is 60 % of that, and the
    # narrow side faces up for the sidechain bar and down for the post eq bar.
    PIT_X, PIT_W, NARROW = 200, 640, 0.6
    # HTML controls inside the pit (dropdowns, presets) are painted *above* the
    # glass, so they are not part of the backdrop
    CONTROLS = (264, 776)

    def interior_cols(y_in_bar, bar_height, narrow_up):
        # fraction along the bar, 0 = narrow edge
        t = (y_in_bar / max(1, bar_height - 1)) if narrow_up \
            else 1.0 - (y_in_bar / max(1, bar_height - 1))
        half = (NARROW + (1.0 - NARROW) * t) * PIT_W / 2.0
        cx = PIT_X + PIT_W / 2.0
        x0 = int(cx - half) + 5          # stay clear of the clip edge
        x1 = int(cx + half) - 5
        return [x for x in range(x0, x1) if not (CONTROLS[0] <= x <= CONTROLS[1])]

    worst = 0.0
    rows = []

    def band(y0, y1, label, bar_top, bar_h, narrow_up):
        nonlocal worst
        cols = interior_cols((y0 + y1) / 2.0 - bar_top, bar_h, narrow_up)
        if not cols:
            return
        a = frost[y0:y1][:, cols]
        b = ref[y0:y1][:, cols]
        d = a - b
        # per-column RMS, then the median: isolates the band from any single
        # column that carries text, a control or the clip edge
        per_col = np.sqrt((d ** 2).mean(axis=0))
        rms = float(np.median(per_col))
        bias = float(np.median(d.mean(axis=0)))
        corr = float(np.corrcoef(a.ravel(), b.ravel())[0, 1]) \
            if (a.std() > 0 or b.std() > 0) else 1.0
        rows.append((label, rms, bias, corr, len(cols)))
        worst = max(worst, rms)

    # sidechain bar: rows 0..31 of the page (narrow edge up)
    for y in range(2, 30, 4):
        band(y, y + 4, f"SC pit y={y:2d}", 0, 32, True)
    # post eq bar: rows 639..671 (narrow edge down)
    for y in range(641, 669, 4):
        band(y, y + 4, f"PQ pit y={y:3d}", 639, 32, False)

    print(f"reference: GaussianBlur(sigma=3) of the unfrosted page")
    print(f"{'band':14s} {'rms':>6s} {'bias':>7s} {'corr':>7s} {'cols':>5s}")
    for label, rms, bias, corr, n in rows:
        flag = "" if rms <= args.tolerance else "   <-- over tolerance"
        print(f"{label:14s} {rms:6.2f} {bias:7.2f} {corr:7.4f} {n:5d}{flag}")

    ok = worst <= args.tolerance
    print(f"\nworst RMS {worst:.2f} (tolerance {args.tolerance}) -> "
          f"{'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

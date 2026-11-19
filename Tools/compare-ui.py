#!/usr/bin/env python3
"""Compare the QML editor with the web page, landmark by landmark.

Both render the same 1040x772 design, so every feature should sit at the same
pixel.  Rather than a raw diff (dominated by antialiasing and by the animated
meters), this pulls out the landmarks that define the layout and reports the
offset of each one.

Usage:  ./compare-ui.py web.png qml.png
"""
import sys

import numpy as np
from PIL import Image

BG2 = (28, 31, 38)
INK = (231, 235, 242)
SC = (255, 92, 138)
PQ = (67, 217, 196)
AMBER = (240, 162, 59)


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(int)


def near(img, colour, tol):
    return np.abs(img - np.array(colour)).max(axis=2) <= tol


def runs(mask_1d, gap=3, min_len=1):
    idx = np.where(mask_1d)[0]
    out, start, prev = [], None, -99
    for x in idx:
        if start is None:
            start = x
        elif x > prev + gap:
            if prev - start + 1 >= min_len:
                out.append((int(start), int(prev)))
            start = x
        prev = x
    if start is not None and prev - start + 1 >= min_len:
        out.append((int(start), int(prev)))
    return out


def landmark_knobs(img):
    """Deck knobs: accent arcs, reported as x-centres plus the row range."""
    out = {}
    for name, colour in (("sc", SC), ("pq", PQ), ("amber", AMBER)):
        m = near(img[671:772], colour, 26)
        centres = [int((a + b) / 2) for a, b in runs(m.any(axis=0), gap=14, min_len=4)]
        rows = np.where(m.any(axis=1))[0]
        out[name] = (centres, (int(rows.min()) + 671, int(rows.max()) + 671) if len(rows) else None)
    return out


def landmark_trapezoid(img):
    out = {}
    for label, rows in (("SC", [2, 16, 30]), ("PQ", [641, 655, 669])):
        edges = []
        for y in rows:
            row = img[y]
            inside = np.abs(row - np.array(BG2)).max(axis=1) > 4
            idx = np.where(inside)[0]
            idx = idx[(idx > 120) & (idx < 920)]
            edges.append((y, int(idx.min()), int(idx.max())) if len(idx) else (y, None, None))
        out[label] = edges
    return out


def landmark_grid(img):
    """Vertical grid lines of the SC plot."""
    band = img[60:300].mean(axis=2)
    cols = np.where(band.mean(axis=0) > band.mean() + 1.2)[0]
    mask = np.zeros(1040, dtype=bool)
    mask[cols] = True
    return runs(mask, gap=2)


def landmark_text(img, y0, y1, colour, tol, gap=6):
    m = near(img[y0:y1], colour, tol)
    return runs(m.any(axis=0), gap=gap, min_len=1)


def report(name, a, b):
    if a == b:
        print(f"  {name:22s} OK")
        return 0
    print(f"  {name:22s} WEB {a}")
    print(f"  {'':22s} QML {b}")
    return 1


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    w, q = load(sys.argv[1]), load(sys.argv[2])
    problems = 0

    print("== knob centres (deck) ==")
    kw, kq = landmark_knobs(w), landmark_knobs(q)
    for k in sorted(kw):
        problems += report(k, kw[k], kq.get(k))

    print("== trapezoid edges ==")
    tw, tq = landmark_trapezoid(w), landmark_trapezoid(q)
    for k in sorted(tw):
        problems += report(k, tw[k], tq.get(k))

    print("== SC plot grid lines ==")
    problems += report("grid", landmark_grid(w), landmark_grid(q))

    print("== SC bar text ==")
    problems += report("ink runs", landmark_text(w, 0, 32, INK, 45),
                       landmark_text(q, 0, 32, INK, 45))
    problems += report("accent runs", landmark_text(w, 0, 32, SC, 55),
                       landmark_text(q, 0, 32, SC, 55))

    print("== PQ bar text ==")
    problems += report("ink runs", landmark_text(w, 639, 671, INK, 45),
                       landmark_text(q, 639, 671, INK, 45))

    print("== wordmark (seam) ==")
    problems += report("ink", landmark_text(w, 300, 372, INK, 60, gap=8),
                       landmark_text(q, 300, 372, INK, 60, gap=8))
    problems += report("pink", landmark_text(w, 300, 372, SC, 90, gap=8),
                       landmark_text(q, 300, 372, SC, 90, gap=8))

    print()
    print(f"{problems} landmark mismatches")
    return 0 if problems == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

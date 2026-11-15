#!/usr/bin/env python3
"""Render the preview to PNG and compare two builds pixel by pixel.

Used to prove that an optimisation does not change the style: the reference
is a screenshot from a known-good build, the candidate is the same view from
the new one, and this reports exactly how many pixels differ and by how much.

    ./render-diff.py --ref good.png        # capture the current build as ref
    ./render-diff.py --diff good.png new.png

By default it renders with --freeze (mock meters and spectrum pinned) and a
fixed --shot-delay, so two runs are comparable.
"""
import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "overteon-qml-preview")


def render(out, size="1040x772", extra=()):
    env = dict(os.environ, DISPLAY=":3")
    cmd = [EXE, "--size", size, "--freeze", "--shot", out,
           "--shot-delay", "2500", *extra]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=120)
    if not os.path.exists(out):
        print(r.stdout, r.stderr, file=sys.stderr)
        raise SystemExit(f"render failed: {out}")
    return out


def compare(a_path, b_path, tol=2, verbose=True):
    a = np.asarray(Image.open(a_path).convert("RGB")).astype(int)
    b = np.asarray(Image.open(b_path).convert("RGB")).astype(int)
    if a.shape != b.shape:
        print(f"size differs: {a.shape} vs {b.shape}")
        return 1
    d = np.abs(a - b).max(axis=2)
    changed = int((d > tol).sum())
    if verbose:
        print(f"size        {a.shape[1]}x{a.shape[0]}")
        print(f"tol         {tol}/255")
        print(f"changed     {changed} px ({100.0 * changed / d.size:.3f}%)")
        print(f"max delta   {int(d.max())}")
        if changed:
            ys, xs = np.where(d > tol)
            print(f"bbox        x {xs.min()}..{xs.max()}  y {ys.min()}..{ys.max()}")
    return 0 if changed == 0 else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", help="capture the current build into this path")
    ap.add_argument("--diff", nargs=2, metavar=("A", "B"), help="compare two PNGs")
    ap.add_argument("--tol", type=int, default=2)
    ap.add_argument("--size", default="1040x772")
    args = ap.parse_args()

    if args.ref:
        render(args.ref, args.size)
        print(f"wrote {args.ref}")
        return 0

    if args.diff:
        return compare(args.diff[0], args.diff[1], args.tol)

    # default: capture twice and confirm the renderer is deterministic
    tmp = tempfile.mkdtemp(prefix="renderdiff-")
    a = render(os.path.join(tmp, "a.png"), args.size)
    b = render(os.path.join(tmp, "b.png"), args.size)
    print("determinism check (same build rendered twice):")
    return compare(a, b, args.tol)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""bench.py — measure the OnyxTEON preview: CPU, FPS and pixel drift.

Three numbers, because "it feels slow" and "it uses CPU" are different things:

  * cpu       process CPU % over a window, at idle (frozen data) and live
  * fps       frames the window actually presents
  * diff      pixels changed against a reference render (style regression)

Usage
    ./bench.py                       # full run on the default display
    ./bench.py --display :0          # a specific display
    ./bench.py --ref ref.png         # also compare against a reference
    ./bench.py --ref-save ref.png    # capture a reference and exit
    ./bench.py --quick               # skip the idle phase

All processes are started and stopped by PID (pidfile + process group), never
by pattern matching.
"""
import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

import numpy as np

# px_diff noise floor: identical builds differ by a pixel or two of text
# antialiasing, so "0 px exactly" is not a usable bar.
NOISE_PX = 8
NOISE_D = 6
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "overteon-qml-preview")
RUN = os.path.join(tempfile.gettempdir(), "onyx-bench")


# --------------------------------------------------------------------------
def ensure_dir():
    os.makedirs(RUN, exist_ok=True)


def proc_cpu(pid, seconds=3.0, samples=3):
    """Average CPU % of a process over `seconds`."""
    vals = []
    for _ in range(samples):
        try:
            out = subprocess.run(
                ["ps", "-o", "%cpu=", "-p", str(pid)],
                capture_output=True, text=True, timeout=10).stdout.strip()
            if out:
                vals.append(float(out))
        except Exception:
            pass
        time.sleep(seconds / samples)
    return max(vals) if vals else 0.0


def thread_cpu(pid, seconds=2.0):
    """CPU % of the busiest thread — shows whether one thread is spinning."""
    best = 0.0
    try:
        tasks = os.listdir(f"/proc/{pid}/task")
    except OSError:
        return 0.0
    before = {}
    for t in tasks:
        try:
            f = open(f"/proc/{pid}/task/{t}/stat").read().split()
            before[t] = int(f[13]) + int(f[14])
        except Exception:
            pass
    time.sleep(seconds)
    hz = os.sysconf("SC_CLK_TCK")
    for t, b in before.items():
        try:
            f = open(f"/proc/{pid}/task/{t}/stat").read().split()
            after = int(f[13]) + int(f[14])
            best = max(best, (after - b) / hz / seconds * 100.0)
        except Exception:
            pass
    return best


# --------------------------------------------------------------------------
class Instance:
    """A preview process, started in its own session and stopped by PID."""

    def __init__(self, display, args=(), env_extra=None):
        ensure_dir()
        self.display = display
        self.exited = False
        self.explicit_exit = False
        self.log_path = os.path.join(RUN, "preview.log")
        env = dict(os.environ, DISPLAY=display, **(env_extra or {}))
        self.log = open(self.log_path, "w")
        self.proc = subprocess.Popen(
            [EXE, *args], env=env, stdout=self.log, stderr=subprocess.STDOUT,
            start_new_session=True)

    @property
    def pid(self):
        return self.proc.pid

    def wait_ready(self, timeout=40.0):
        """Wait until the process has a mapped window (or has died)."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            time.sleep(1.0)
            if self.proc.poll() is not None:
                return False
            found = find_windows_by_pid(self.display, self.pid) or find_windows(self.display)
            if found:
                return True
            if os.environ.get("BENCH_DEBUG"):
                print("   waiting: no window yet, log so far:")
                print("   " + self.log_text()[-300:].replace("\n", "\n   "))
        return False

    def stop(self):
        if self.proc.poll() is not None:
            return
        try:
            os.killpg(os.getpgid(self.pid), signal.SIGTERM)
        except ProcessLookupError:
            pass
        for _ in range(30):
            if self.proc.poll() is not None:
                break
            time.sleep(0.1)
        if self.proc.poll() is None:
            try:
                os.killpg(os.getpgid(self.pid), signal.SIGKILL)
            except ProcessLookupError:
                pass
        self.log.close()

    def log_text(self):
        try:
            return open(self.log_path).read()
        except OSError:
            return ""

    def alive(self):
        """A frozen UI shows up as a live process with a dead event loop."""
        return self.proc.poll() is None


# --------------------------------------------------------------------------
def find_windows_by_pid(display, pid):
    """Windows belonging to a specific process, via _NET_WM_PID."""
    env = dict(os.environ, DISPLAY=display)
    out = []
    for line in subprocess.run(["wmctrl", "-lp"], env=env,
                               capture_output=True, text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == str(pid):
            out.append(parts[0])
    return out


def find_windows(display):
    """Visible top-level windows of the preview.

    `xdotool search` also matches Qt's hidden helper windows (the selection
    owner, the 10x10 stub), so the mapped, reasonably sized ones are picked by
    asking xwininfo.
    """
    env = dict(os.environ, DISPLAY=display)
    ids = subprocess.run(["xdotool", "search", "--name", "overteon-qml-preview"],
                         env=env, capture_output=True, text=True).stdout.split()
    if not ids:
        ids = subprocess.run(["xdotool", "search", "--class", "overteon-qml-preview"],
                             env=env, capture_output=True, text=True).stdout.split()
    good = []
    for wid in ids:
        info = subprocess.run(["xwininfo", "-id", wid], env=env,
                              capture_output=True, text=True).stdout
        w = re.search(r"Width:\s+(\d+)", info)
        h = re.search(r"Height:\s+(\d+)", info)
        mapped = "IsViewable" in info
        if w and h and mapped and int(w.group(1)) > 200 and int(h.group(1)) > 200:
            good.append(wid)
    return good


def capture_window(display, out, pid=None):
    ids = (find_windows_by_pid(display, pid) if pid else []) or find_windows(display)
    if not ids:
        return None
    env = dict(os.environ, DISPLAY=display)
    wid = ids[-1]
    # Delete first: `import` leaves the previous file in place when it fails,
    # and then os.path.exists() is true for a stale image.  Two identical
    # stale captures read as "0 px changed" — the UI was reported as frozen
    # while it was animating at 62 fps.
    for f in (out, out + ".tmp.png"):
        try: os.unlink(f)
        except OSError: pass
    r = subprocess.run(["import", "-window", wid, out], env=env,
                       capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(out):
        return None
    return out


def raise_window(display, pid):
    """Bring the preview to the top before measuring.

    An obscured window is not exposed, and Qt stops rendering while it is not
    exposed — so a capture of a window that is merely *covered* reads as zero
    changes and the UI looks frozen while it is running normally.  Measured:
    0 px changed while covered, 83523 px once raised.
    """
    ids = find_windows_by_pid(display, pid) if pid else find_windows(display)
    if not ids:
        return False
    env = dict(os.environ, DISPLAY=display)
    return subprocess.run(["wmctrl", "-ia", ids[-1]], env=env,
                          capture_output=True).returncode == 0


def frame_activity(display, seconds=3.0, samples=4, pid=None):
    """Pixels that change between captures — 0 means the UI is frozen."""
    raise_window(display, pid)
    time.sleep(0.3)
    a_path = os.path.join(RUN, "fa.png")
    changed = []
    prev = None
    for _ in range(samples):
        p = capture_window(display, a_path, pid)
        if p is None:
            return None          # could not capture: unknown, not "frozen"
        cur = np.asarray(Image.open(p).convert("RGB")).astype(np.int16)
        if prev is not None and prev.shape == cur.shape:
            changed.append(int((np.abs(cur - prev).max(axis=2) > 3).sum()))
        prev = cur
        time.sleep(seconds / samples)
    return changed


def px_diff(a_path, b_path, tol=2):
    a = np.asarray(Image.open(a_path).convert("RGB")).astype(int)
    b = np.asarray(Image.open(b_path).convert("RGB")).astype(int)
    if a.shape != b.shape:
        return dict(error=f"size {a.shape} vs {b.shape}")
    d = np.abs(a - b).max(axis=2)
    return dict(changed=int((d > tol).sum()), total=int(d.size), max_delta=int(d.max()))


def shots_from_exe(size="1040x772", display=":3"):
    """Two deterministic renders with --shot, for the style diff.

    --shot runs the app headless-ish (it quits after grabbing), so it can use
    any display; :3 is used because it always exists for the test rig.
    """
    out = []
    for i in ("a", "b"):
        p = os.path.join(RUN, f"shot_{i}.png")
        for f in (p,):
            if os.path.exists(f):
                os.unlink(f)
        r = subprocess.run([EXE, "--size", size, "--freeze", "--shot", p,
                            "--shot-delay", "2500"],
                           env=dict(os.environ, DISPLAY=display),
                           capture_output=True, text=True, timeout=180)
        if not os.path.exists(p):
            print(f"  render failed (rc={r.returncode})")
            print("   ", (r.stdout + r.stderr).strip()[:400])
            raise SystemExit("cannot capture a render for the style check")
        out.append(p)
    return out


# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--display", default=":0")
    ap.add_argument("--size", default="1040x772")
    ap.add_argument("--ref", help="reference PNG for the style diff")
    ap.add_argument("--ref-save", help="capture a reference render here and exit")
    ap.add_argument("--json", help="write the results as JSON here")
    ap.add_argument("--quick", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(EXE):
        raise SystemExit(f"build first: {EXE} missing")
    ensure_dir()

    results = {"display": args.display, "size": args.size}

    if args.ref_save:
        a, _ = shots_from_exe(args.size)
        shutil.copy(a, args.ref_save)
        print(f"reference written to {args.ref_save}")
        return 0

    print(f"OnyxTEON benchmark — display {args.display}, {args.size}\n")

    # ---- style: two renders of the same build must be identical -----------
    print("style (determinism)")
    a, b = shots_from_exe(args.size)
    det = px_diff(a, b)
    results["deterministic"] = det
    # A handful of pixels at a tiny delta is antialiasing noise on text edges
    # (the same build twice differs by ~1 px at max 3), not a style change.
    ok = det.get("changed", 1) <= NOISE_PX and det.get("max_delta", 99) <= NOISE_D
    print(f"  {'PASS' if ok else 'FAIL'}  same build rendered twice: "
          f"{det.get('changed')} px differ (max {det.get('max_delta')})")

    if args.ref:
        ref = px_diff(args.ref, a)
        results["vs_reference"] = ref
        ok = ref.get("changed", 1) <= NOISE_PX and ref.get("max_delta", 99) <= NOISE_D
        print(f"  {'PASS' if ok else 'FAIL'}  vs {os.path.basename(args.ref)}: "
              f"{ref.get('changed')} px differ (max {ref.get('max_delta')})")

    # ---- live: cpu, fps, animation ---------------------------------------
    print("\nlive  (mock meters running)")
    inst = Instance(args.display, ["--size", args.size],
                    {"ONYXTEON_FPS": "1"})
    try:
        if not inst.wait_ready():
            print("  FAIL  the window never appeared")
            print(inst.log_text()[-2000:])
            results["live"] = {"error": "no window"}
            inst.stop()
            return finish(results, args)

        time.sleep(3)
        cpu = proc_cpu(inst.pid, 3.0)
        busiest = thread_cpu(inst.pid, 2.0)
        results["live"] = {"cpu": cpu, "busiest_thread": busiest}

        cores = os.cpu_count() or 1
        print(f"  cpu            {cpu:6.1f} % of one core "
              f"({cpu / cores:.1f} % of the machine, {cores} cores)")
        print(f"  busiest thread {busiest:6.1f} % of one core")

        act = frame_activity(args.display, pid=inst.pid)
        results["live"]["frame_changes"] = act
        if act is None:
            print("  frame changes  could not capture the window")
        else:
            frozen = all(v == 0 for v in act)
            if frozen:
                results["live"]["frozen"] = True
            print(f"  frame changes  {act}  {'<-- FROZEN' if frozen else ''}")

        fps = [float(m) for m in re.findall(r"\[fps\] window ([\d.]+)", inst.log_text())]
        if fps:
            results["live"]["fps"] = fps[-5:]
            print(f"  fps            {fps[-1]:6.1f}  (last of {len(fps)} samples)")
        else:
            print("  fps            no samples (the window never presented)")
    finally:
        inst.stop()

    # ---- idle: nothing changing ------------------------------------------
    if not args.quick:
        print("\nidle  (--freeze: mock data pinned, nothing to redraw)")
        inst = Instance(args.display, ["--size", args.size, "--freeze"],
                        {"ONYXTEON_FPS": "1"})
        try:
            if not inst.wait_ready():
                print("  FAIL  the window never appeared")
            else:
                time.sleep(3)
                cpu = proc_cpu(inst.pid, 3.0)
                act = frame_activity(args.display, pid=inst.pid)
                results["idle"] = {"cpu": cpu, "frame_changes": act}
                warn = "" if cpu < 5.0 else "   <-- should be near zero"
                cores = os.cpu_count() or 1
                print(f"  cpu            {cpu:6.1f} % of one core "
                      f"({cpu / cores:.1f} % of the machine){warn}")
                print(f"  frame changes  {act}")
        finally:
            inst.stop()

    return finish(results, args)


def finish(results, args):
    if args.json:
        with open(args.json, "w") as f:
            json.dump(results, f, indent=1)
        print(f"\nwrote {args.json}")

    bad = []
    if results.get("deterministic", {}).get("changed"):
        bad.append("non-deterministic render")
    if results.get("vs_reference", {}).get("changed"):
        bad.append("differs from the reference")
    if results.get("live", {}).get("frozen"):
        bad.append("UI frozen while live")
    # 40 % of ONE core (top's per-core scale) is the threshold: a 1040x772
    # animated QML scene with two painted plots and a blur is legitimately a
    # fraction of a core, but it should not take half of one.
    if results.get("live", {}).get("cpu", 0) > 40:
        cores = os.cpu_count() or 1
        bad.append(f"live cpu {results['live']['cpu']:.0f}% of a core")
    print("\n" + ("FAIL: " + ", ".join(bad) if bad else "PASS"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

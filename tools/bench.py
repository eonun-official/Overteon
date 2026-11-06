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


def capture_specific(display, wid, out):
    """Grab exactly this window.  Returns the path, or None if it failed.

    ImageMagick's `import -window <id>` works on Carla's canvas but fails on
    jalv's editor with "missing an image filename", so `xwd | convert` is tried
    as well.  Both go through the same X server; only the tool differs.
    """
    if not wid:
        return None
    env = dict(os.environ, DISPLAY=display)
    if os.path.exists(out):
        os.unlink(out)

    subprocess.run(["import", "-window", wid, out], env=env,
                   capture_output=True, timeout=30)
    if os.path.exists(out) and os.path.getsize(out) > 0:
        return out

    # fallback: xwd to a temp file, then convert
    xwd = out + ".xwd"
    try:
        with open(xwd, "wb") as f:
            subprocess.run(["xwd", "-id", wid, "-silent"], env=env,
                           stdout=f, stderr=subprocess.DEVNULL, timeout=30)
        subprocess.run(["convert", xwd, out], env=env,
                       capture_output=True, timeout=30)
    except Exception:
        pass
    finally:
        if os.path.exists(xwd):
            os.unlink(xwd)
    if os.path.exists(out) and os.path.getsize(out) > 0:
        return out
    return None


def editor_window(display, pid=None):
    """The window that actually holds the plug-in editor.

    jalv.gtk3 nests it several levels down:

        0x800000b "OnyxTEON"  846x694   the gtk toplevel
          ...
            0xd000011 "OnyxTEON" 1094x813  <-- the Qt editor itself

    Two things had to be learned the hard way here:

    * Grabbing the toplevel returns a blank white rectangle, because its own
      drawable is covered by that child chain.  That is what made the first
      version of the animation check report FROZEN on a perfectly healthy
      editor.
    * `wmctrl -lp` does not list this window on a real desktop (it is not
      managed by the WM there), so looking it up by PID finds nothing on :0
      while working on Xephyr.  The X class is the reliable handle: the editor
      is the window whose WM_CLASS is OnyxTEON.

    Note the editor is LARGER than its parent on a HiDPI display (1094x813 for
    a 840x624 socket at 1.302) — the Qt window is sized in device pixels.
    """
    env = dict(os.environ, DISPLAY=display)
    tree = subprocess.run(["xwininfo", "-root", "-tree"], env=env,
                          capture_output=True, text=True).stdout
    best = None
    for line in tree.splitlines():
        # The editor's X class.  jalv.gtk3 names its wrapper window "OnyxTEON"
        # too but with class Jalv.gtk3, so the class is what separates the Qt
        # window from the host's frame.
        if '"OnyxTEON" "OnyxTEON"' not in line and '("OnyxTEON" "OnyxTEON")' not in line:
            continue
        m = re.match(r"^(\s*)(0x[0-9a-f]+)\s", line)
        if not m:
            continue
        indent, wid = len(m.group(1)), m.group(2)
        sz = re.search(r"(\d+)x(\d+)\+", line)
        if not sz or int(sz.group(1)) < 200 or int(sz.group(2)) < 200:
            continue
        if best is None or indent > best[0]:
            best = (indent, wid)
    if best is None:
        return None
    info = subprocess.run(["xwininfo", "-id", best[1]], env=env,
                          capture_output=True, text=True).stdout
    return best[1] if "IsViewable" in info else None


def editor_window_state(display):
    """(window id or None, viewable?) for the editor, without filtering.

    Needed to tell "the editor never existed" apart from "it exists but the
    attach path leaves it unmapped" — two very different bugs, and only the
    second one shows up intermittently in jalv.gtk3.
    """
    env = dict(os.environ, DISPLAY=display)
    tree = subprocess.run(["xwininfo", "-root", "-tree"], env=env,
                          capture_output=True, text=True).stdout
    best = None
    for line in tree.splitlines():
        # The editor's X class.  jalv.gtk3 names its wrapper window "OnyxTEON"
        # too but with class Jalv.gtk3, so the class is what separates the Qt
        # window from the host's frame.
        if '"OnyxTEON" "OnyxTEON"' not in line and '("OnyxTEON" "OnyxTEON")' not in line:
            continue
        m = re.match(r"^(\s*)(0x[0-9a-f]+)\s", line)
        sz = re.search(r"(\d+)x(\d+)\+", line)
        if not m or not sz or int(sz.group(1)) < 200 or int(sz.group(2)) < 200:
            continue
        if best is None or len(m.group(1)) > best[0]:
            best = (len(m.group(1)), m.group(2))
    if best is None:
        return None, False
    info = subprocess.run(["xwininfo", "-id", best[1]], env=env,
                          capture_output=True, text=True).stdout
    return best[1], ("IsViewable" in info)


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
#  jalv mode: the plug-in in a real host, fed with real audio.
#
#  The preview app and the LV2 plug-in share the same QML front end but not the
#  same host: jalv drives the processor through JACK at a fixed block size, and
#  the CPU it costs there depends on the DSP, the audio block, and the UI thread
#  competing with the audio thread — none of which the preview can show.
#
#  The chain built here is:
#
#      mpv --ao=jack ──► OnyxTEON (jalv) ──► system playback
#         FLAC file        the plug-in          JACK
#
#  A PRIVATE jackd is started for this (`-n ovtest -d dummy`), so the user's own
#  audio server and hardware are never touched.  Everything is started by PID
#  and stopped by PID, the same rule as the rest of this file.
# --------------------------------------------------------------------------
JACK_SERVER = "ovtest"
JACK_ARGS = ["-n", JACK_SERVER, "-d", "dummy", "-r", "48000", "-p", "1024"]
LV2_URI = "https://eonun.com/plugins/OnyxTEON"
LV2_BUNDLE = os.path.join(ROOT, "build", "HarmonicInjector_artefacts",
                          "Release", "LV2", "OnyxTEON.lv2")


class Proc:
    """A child process in its own session, stoppable by PID."""

    def __init__(self, argv, name, env_extra=None, log_name=None):
        ensure_dir()
        self.name = name
        self.log_path = os.path.join(RUN, f"{log_name or name}.log")
        env = dict(os.environ, **(env_extra or {}))
        self.log = open(self.log_path, "w")
        self.proc = subprocess.Popen(
            argv, env=env, stdout=self.log, stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL, start_new_session=True)

    @property
    def pid(self):
        return self.proc.pid

    def alive(self):
        return self.proc.poll() is None

    def wait_port(self, pred, timeout=20.0, what=""):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if not self.alive():
                return False
            try:
                if pred():
                    return True
            except Exception:
                pass
            time.sleep(0.35)
        return False

    def stop(self):
        if self.proc.poll() is None:
            try:
                os.killpg(os.getpgid(self.pid), signal.SIGTERM)
            except OSError:
                pass
            for _ in range(50):
                if self.proc.poll() is not None:
                    break
                time.sleep(0.1)
            if self.proc.poll() is None:
                try:
                    os.killpg(os.getpgid(self.pid), signal.SIGKILL)
                except OSError:
                    pass
            try:
                self.proc.wait(timeout=5)
            except Exception:
                pass
        self.log.close()

    def log_text(self):
        try:
            self.log.flush()
        except Exception:
            pass
        try:
            with open(self.log_path) as f:
                return f.read()
        except OSError:
            return ""


def jc(args, timeout=10):
    """Run a jack tool against OUR server only."""
    return subprocess.run(["env", f"JACK_DEFAULT_SERVER={JACK_SERVER}", *args],
                          capture_output=True, text=True, timeout=timeout)


def server_up():
    return jc(["jack_lsp"], timeout=4).returncode == 0


def jack_ports(prefix=""):
    r = jc(["jack_lsp"], timeout=5)
    if r.returncode != 0:
        return []
    return [l.strip() for l in r.stdout.splitlines()
            if l.strip() and l.strip().startswith(prefix)]


def cpu_ticks(pid):
    """utime + stime in clock ticks, straight from /proc — `ps %cpu` is an
    average over the process lifetime and cannot answer "how much now"."""
    try:
        with open(f"/proc/{pid}/stat") as f:
            parts = f.read().rsplit(") ", 1)[1].split()
        return int(parts[11]) + int(parts[12])
    except (OSError, IndexError, ValueError):
        return None


def measure_cpu(pid, seconds):
    a = cpu_ticks(pid)
    if a is None:
        return None
    time.sleep(seconds)
    b = cpu_ticks(pid)
    if b is None:
        return None
    hz = os.sysconf("SC_CLK_TCK")
    return (b - a) / hz / seconds * 100.0


def capture_rms(ports, seconds=1.5):
    """Record the plug-in's outputs and measure them.

    jack_capture writes 32-bit float, which Python's `wave` module refuses, so
    the data chunk is located and read directly.  Returns None if nothing could
    be captured; the caller treats a tiny peak as silence.
    """
    if not ports:
        return None
    wav = os.path.join(RUN, "plugout.wav")
    argv = ["env", f"JACK_DEFAULT_SERVER={JACK_SERVER}", "jack_capture",
            "-d", f"{seconds:.2f}", "-c", "1"]
    for p in ports[:2]:
        argv += ["-p", p]
    argv.append(wav)
    try:
        subprocess.run(argv, capture_output=True, timeout=seconds + 20)
    except Exception:
        return None
    try:
        raw = open(wav, "rb").read()
    except OSError:
        return None
    i = raw.find(b"data")
    if i < 0:
        return None
    n = int.from_bytes(raw[i + 4:i + 8], "little")
    body = raw[i + 8:i + 8 + n]
    if not body:
        return None
    a = np.frombuffer(body[:len(body) // 4 * 4], dtype="<f4")
    if a.size == 0:
        return None
    peak = float(np.abs(a).max())
    rms = float(np.sqrt((a.astype(np.float64) ** 2).mean()))
    return {"peak": peak, "rms": rms,
            "dbfs": 20.0 * np.log10(max(rms, 1e-9)) if rms > 0 else -999.0}


def run_carla(args):
    """`-Gc`: the plug-in in carla-single, with its UI and real audio.

    Worth having next to jalv because Carla and jalv are completely different
    hosts and they disagree on the two things that matter here:

      * Carla lays out a host canvas for the editor and drives the idle callback
        hard, so the UI animates properly; jalv idles at ~11.8 Hz and that rate
        becomes the frame rate straight away.
      * Carla reports each editor window as "<name> (GUI)" with our Qt window as
        its child, so the geometry is visible from the outside — that is how the
        device/logical pixel mistake was finally found (a 1763x1309 canvas with a
        1354x1005 editor inside it).

    So `-Gj` answers "does it work in a slow host" and `-Gc` answers "does it
    work at all, at the right size".  Both are worth running.
    """
    results = {"mode": "carla"}

    pkg = os.path.join(ROOT, "tools", "package.sh")
    r = subprocess.run([pkg, "--check"], capture_output=True, text=True)
    if r.returncode != 0:
        print("installing the LV2 bundle (it differed from the build)")
        subprocess.run([pkg, "--no-build"], capture_output=True, text=True)
    print(f"bundle        {LV2_BUNDLE}")

    started_jack = not server_up()
    jack = None
    if started_jack:
        print(f"jack          starting a private server '{JACK_SERVER}'")
        jack = Proc(["jackd", *JACK_ARGS], "jack")
        if not jack.wait_port(server_up, timeout=15):
            print("FAIL: jackd did not come up")
            print(jack.log_text()[-1200:])
            jack.stop()
            return 1
    else:
        print(f"jack          reusing the server already on '{JACK_SERVER}'")
    results["jack"] = {"server": JACK_SERVER, "started_by_bench": started_jack}

    carla = play = None
    try:
        print("carla         carla-single lv2 (its own window manager for the UI)")
        carla = Proc(["carla-single", "lv2", LV2_URI], "carla",
                     {"JACK_DEFAULT_SERVER": JACK_SERVER,
                      "LV2_PATH": os.path.expanduser("~/.lv2"),
                      "DISPLAY": args.display})

        # Carla registers the ports under the plug-in name, not a short client
        # name, so they are looked up by the plug-in's own name.
        def carla_ports():
            return [x for x in jack_ports() if x.startswith("OnyxTEON:")]
        if not carla.wait_port(lambda: len(carla_ports()) >= 4, timeout=40):
            print("FAIL: carla never registered the plug-in's ports")
            print((carla.log_text() or "")[-1500:])
            return finish(results, args)
        ports_in  = sorted([x for x in carla_ports() if "In"  in x])
        ports_out = sorted([x for x in carla_ports() if "Out" in x])
        print(f"              {len(ports_in)} in / {len(ports_out)} out")

        flac = args.flac
        if not os.path.exists(flac):
            print(f"FAIL: no audio file at {flac}")
            return finish(results, args)
        print(f"mpv           {os.path.basename(flac)}")
        play = Proc(["mpv", "--no-video", "--really-quiet", "--no-terminal",
                     "--ao=jack", "--jack-name=mpvplay", "--loop-file=inf", flac],
                    "mpv", {"JACK_DEFAULT_SERVER": JACK_SERVER})
        if not play.wait_port(lambda: len(jack_ports("mpvplay:")) >= 2, timeout=25):
            print("FAIL: mpv did not open its JACK port")
            print((play.log_text() or "")[-1200:])
            return finish(results, args)

        src = sorted([x for x in jack_ports("mpvplay:") if "out_" in x])
        made = []
        for x, d in zip(src, ports_in[:2]):
            if jc(["jack_connect", x, d]).returncode == 0:
                made.append((x, d))
        results["audio"] = {"file": flac, "connections": [f"{a} -> {b}" for a, b in made]}
        print(f"connections   {len(made)}: "
              + ", ".join(f"{a.split(':')[1]}->{b.split(':')[1]}" for a, b in made))
        if len(made) < 2:
            print("FAIL: could not connect mpv to the plug-in")
            return finish(results, args)

        time.sleep(3)
        sig = capture_rms(ports_out, seconds=1.5)
        results["audio"]["signal"] = sig
        if sig:
            print(f"  signal         peak {sig['peak']:.3f}, {sig['dbfs']:+.1f} dBFS rms")

        # ---- the editor window, and does it move? ----------------------
        def carla_editor():
            tree = subprocess.run(["xwininfo", "-root", "-tree"],
                                  env=dict(os.environ, DISPLAY=args.display),
                                  capture_output=True, text=True).stdout
            best = None
            for line in tree.splitlines():
                if '"OnyxTEON" "OnyxTEON"' not in line:
                    continue
                m = re.match(r"^(\s*)(0x[0-9a-f]+)\s", line)
                sz = re.search(r"(\d+)x(\d+)\+", line)
                if not m or not sz or int(sz.group(1)) < 200:
                    continue
                if best is None or len(m.group(1)) > best[0]:
                    best = (len(m.group(1)), m.group(2), int(sz.group(1)), int(sz.group(2)))
            return best

        found = carla.wait_port(lambda: carla_editor(), timeout=25)
        if not found:
            print("  ui window      DID NOT OPEN")
            results["jalv"] = {"ui_missing": True}
            return finish(results, args)
        _, wid, ew, eh = carla_editor()
        print(f"  ui window      {wid}  {ew}x{eh}")

        # The host's canvas is the parent of that window; the editor must fill
        # it.  A mismatch is the device-vs-logical pixel bug.
        parent = subprocess.run(["xwininfo", "-id", wid, "-tree"],
                                env=dict(os.environ, DISPLAY=args.display),
                                capture_output=True, text=True).stdout
        pm = re.search(r"Parent window id: (0x[0-9a-f]+)", parent)
        canvas = None
        if pm:
            pinfo = subprocess.run(["xwininfo", "-id", pm.group(1)],
                                   env=dict(os.environ, DISPLAY=args.display),
                                   capture_output=True, text=True).stdout
            pw = re.search(r"Width:\s+(\d+)", pinfo)
            ph = re.search(r"Height:\s+(\d+)", pinfo)
            if pw and ph:
                canvas = (int(pw.group(1)), int(ph.group(1)))
        if canvas:
            fills = (abs(canvas[0] - ew) <= 2 and abs(canvas[1] - eh) <= 2)
            results["jalv"] = {"ui_window": wid, "editor": [ew, eh],
                               "canvas": list(canvas), "fills_canvas": fills}
            print(f"  canvas         {canvas[0]}x{canvas[1]}  "
                  f"{'filled' if fills else 'NOT FILLED — the editor is the wrong size'}")

        shots = []
        for i in range(6):
            f = os.path.join(RUN, f"carla_{i}.png")
            if os.path.exists(f):
                os.unlink(f)
            ed = carla_editor()          # the id moves; re-resolve every frame
            shots.append(capture_specific(args.display, ed[1] if ed else None, f))
            time.sleep(1.0)
        best = None
        for i in range(len(shots) - 1):
            if shots[i] and shots[i + 1]:
                d = px_diff(shots[i], shots[i + 1])
                if best is None or d["changed"] > best["changed"]:
                    best = d
        if best is None:
            print("  ui frames      could not capture the window")
        else:
            results["jalv"] = results.get("jalv", {})
            results["jalv"]["ui_moving"] = best
            if best["changed"] <= NOISE_PX:
                results["jalv"]["ui_frozen"] = True
                print(f"  ui frames      FROZEN ({best['changed']} px changed)")
            else:
                print(f"  ui frames      {best['changed']} px changed  <-- animating")

        results["live"] = {"alive": carla.alive() and play.alive()}
        print(f"  processes      carla {'alive' if carla.alive() else 'DIED'}, "
              f"mpv {'alive' if play.alive() else 'DIED'}")
        return finish(results, args)

    finally:
        for x in (play, carla):
            if x is not None:
                x.stop()
        if jack is not None:
            jack.stop()


def run_jalv(args):
    """The `--jalv` mode: build the whole chain, measure it, tear it down."""
    results = {"mode": "jalv"}

    # ---- 1. the bundle in ~/.lv2 must be the one we just built ----------
    pkg = os.path.join(ROOT, "tools", "package.sh")
    r = subprocess.run([pkg, "--check"], capture_output=True, text=True)
    installed = (r.returncode == 0)
    if not installed:
        print("installing the LV2 bundle (it differed from the build)")
        r = subprocess.run([pkg, "--no-build"], capture_output=True, text=True)
        print("  " + (r.stdout + r.stderr).strip().replace("\n", "\n  "))
        installed = (r.returncode == 0)
    results["bundle"] = {"installed": installed, "path": LV2_BUNDLE}
    print(f"bundle        {LV2_BUNDLE if installed else 'NOT INSTALLED'}")
    if not installed:
        print("FAIL: could not install the LV2 bundle")
        return 1

    # ---- 2. private JACK server -----------------------------------------
    started_jack = False
    jack = None
    if server_up():
        print(f"jack          reusing the server already on '{JACK_SERVER}'")
    else:
        print(f"jack          starting a private server '{JACK_SERVER}' "
              f"(dummy, 48 kHz, 1024)")
        jack = Proc(["jackd", *JACK_ARGS], "jack")
        started_jack = True
        if not jack.wait_port(server_up, timeout=15):
            print("FAIL: jackd did not come up")
            print(jack.log_text()[-1500:])
            jack.stop()
            return 1
    results["jack"] = {"server": JACK_SERVER, "started_by_bench": started_jack,
                       "blocksize": 1024, "rate": 48000}

    jalv = play = None
    try:
        # ---- 3. the plug-in --------------------------------------------
        jalv_env = {"JACK_DEFAULT_SERVER": JACK_SERVER,
                    "LV2_PATH": os.path.expanduser("~/.lv2"),
                    "DISPLAY": args.display}
        # -G is jalv.gtk3 with the plug-in's own UI: that is the front end under
        # test, and it is what the UI work is for.  `/usr/bin/jalv` is the
        # console host — it can show the same editor, but it has no window to
        # measure and its control window is a different code path in suil.
        # `-i` (non-interactive) is dropped for -G: it swallows keyboard input
        # that the gtk host needs for the embedded editor.
        binary = "jalv.gtk3" if args.ui else "jalv"
        jalv_argv = [binary, "-l", "1024", "-n", "onyx"]
        jalv_argv += ["-s"] if args.ui else ["-i"]
        jalv_argv.append(LV2_URI)
        print(f"jalv          {binary}{' with the plug-in UI' if args.ui else ' headless'}"
              + (f" on {args.display}" if args.ui else ""))
        jalv = Proc(jalv_argv, "jalv", jalv_env)
        if not jalv.wait_port(lambda: len(jack_ports("onyx:")) > 2, timeout=25):
            print("FAIL: the plug-in did not register its JACK ports")
            print((jalv.log_text() or "")[-2000:])
            return finish(results, args)
        # Port names are 1-based (JUCE writes audio_in_1 / audio_in_2), so they
        # are sorted by the trailing number rather than trusted to arrive in
        # order — connecting L to R is silent and looks like a broken plug-in.
        def numbered(prefix):
            got = [p for p in jack_ports("onyx:") if p.startswith("onyx:" + prefix)]
            return sorted(got, key=lambda q: int(re.sub(r"\D", "", q) or 0))
        ports_in, ports_out = numbered("audio_in_"), numbered("audio_out_")
        results["jalv"] = {"pid": jalv.pid, "ports": jack_ports("onyx:"),
                           "inputs": ports_in, "outputs": ports_out}
        print(f"              {len(jack_ports('onyx:'))} ports, "
              f"{len(ports_in)} in / {len(ports_out)} out")

        # ---- 4. mpv, playing the FLAC into the plug-in ------------------
        if args.no_audio:
            print("mpv           skipped (--no-audio)")
            sig = capture_rms(ports_out, seconds=1.0)
            results["audio"] = {"skipped": True,
                                "signal": sig}
            print(f"  signal         peak {sig['peak']:.6f}  "
                  f"{'silent as expected' if sig and sig['peak'] < 1e-4 else 'NOT SILENT'}")
            print("\n(negative control: with no source the outputs must be silent)")
            return finish(results, args)

        flac = args.flac
        if not os.path.exists(flac):
            print(f"FAIL: no audio file at {flac}")
            results["audio"] = {"missing": flac}
            return finish(results, args)
        print(f"mpv           {os.path.basename(flac)}  "
              f"({os.path.getsize(flac) / 1e6:.0f} MB)")
        play = Proc(["mpv", "--no-video", "--really-quiet", "--no-terminal",
                     "--ao=jack",
                     # --jack-name sets the CLIENT name (--jack-port sets the
                     # port name, and mpv exits silently on that); the ports
                     # then appear as mpvplay:out_0 / out_1
                     "--jack-name=mpvplay",
                     # loop: the benchmark window is short and the track is
                     # long, so this only matters if it ends early
                     "--loop-file=inf",
                     flac],
                    "mpv", {"JACK_DEFAULT_SERVER": JACK_SERVER})
        if not play.wait_port(lambda: len(jack_ports("mpvplay:")) >= 2, timeout=25):
            print("FAIL: mpv did not open its JACK port")
            print((play.log_text() or "")[-1500:])
            return finish(results, args)

        src = sorted([p for p in jack_ports("mpvplay:") if "out_" in p])
        # The plug-in's inputs, in port order, so L goes to L and R to R.
        dst = ports_in[:2]
        made = []
        for s, d in zip(src, dst):
            if jc(["jack_connect", s, d]).returncode == 0:
                made.append((s, d))
        results["audio"] = {"file": flac, "mpv": play.pid,
                            "connections": [f"{s} -> {d}" for s, d in made]}
        print(f"connections   {len(made)}: "
              + ", ".join(f"{s.split(':')[1]}->{d.split(':')[1]}" for s, d in made))
        if len(made) < 2:
            print("FAIL: could not connect mpv to the plug-in")
            return finish(results, args)

        # ---- 5. is audio really flowing? --------------------------------
        # CPU alone cannot tell "processing audio" from "idle with ports
        # connected", and a silent plug-in looks perfectly healthy from the
        # outside.  So record the plug-in's own outputs and measure them: a
        # silent chain produces a silent file and this is the only cheap way to
        # prove otherwise.
        time.sleep(2.0)
        results["jalv"]["cpu_settle"] = measure_cpu(jalv.pid, 1.0)

        sig = capture_rms(ports_out, seconds=1.5)
        results["audio"]["signal"] = sig
        if sig is None:
            print("  signal         could not capture the plug-in outputs")
        elif sig["peak"] < 1e-4:
            print(f"  signal         SILENT (peak {sig['peak']:.6f}) "
                  f"<-- the chain is not passing audio")
        else:
            print(f"  signal         peak {sig['peak']:.3f}, "
                  f"{sig['dbfs']:+.1f} dBFS rms  <-- audio is flowing")

        # ---- 5b. the editor, if asked for ---------------------------------
        if args.ui:
            # `-s` is only a request.  jalv silently falls back to a control-only
            # window when suil cannot host the UI type, so "the UI opened" has to
            # be checked rather than assumed — otherwise a run where nothing was
            # drawn still reports PASS.
            # The editor re-parents itself: it attaches to one host container,
            # then to another when the host replaces the widget, so the window
            # id is NOT stable and must be resolved again for every capture.
            # Measured attach sequence in jalv.gtk3: container 0x800004, then
            # 0x600003.  Waiting 20 s because the first attach attempt happens
            # before the host has a peer and is retried from the timer.
            on_screen = bool(jalv.wait_port(
                lambda: editor_window(args.display), timeout=20))
            if not on_screen:
                wid, viewable = editor_window_state(args.display)
                if wid and not viewable:
                    # The editor window exists but was never mapped.  That is
                    # the jalv.gtk3 attach bug, not a missing window, and it is
                    # worth naming precisely.
                    results["jalv"]["ui_unmapped"] = wid
                    print(f"  ui window      exists ({wid}) but is NOT MAPPED "
                          f"<-- attach path never made it visible")
            results["jalv"]["ui_window"] = on_screen
            print(f"  ui window      {'opened' if on_screen else 'DID NOT OPEN'}"
                  f" on {args.display}")
            if not on_screen:
                results["jalv"]["ui_missing"] = True
            else:
                # A window that exists is not a window that runs: the scene
                # graph can map and then never present a frame (a stuck render
                # loop, a GL context that fell back).  Two captures a second
                # apart must differ, because the meters and the spectrum are
                # moving the whole time.
                # Several samples, keep the biggest difference.  A single pair
                # is not enough: measured consecutive captures of a healthy
                # editor at 745, 106465 and 52632 changed pixels, so one quiet
                # moment would have been read as "frozen" while the UI was fine.
                # Six captures over ~7 s, and every consecutive pair is
                # compared.  Two samples were not enough: a static moment right
                # after the plug-in starts (before mpv's first block reaches it)
                # reads as "frozen" while the editor is perfectly healthy, and
                # the same run repeated gave 0 and 172052 changed pixels.
                shots = []
                for i in range(6):
                    f = os.path.join(RUN, f"ui_{i}.png")
                    if os.path.exists(f):
                        os.unlink(f)          # never compare a stale capture
                    wid = editor_window(args.display)     # re-resolve: it moves
                    shots.append(capture_specific(args.display, wid, f))
                    time.sleep(1.2)
                best = None
                for i in range(len(shots) - 1):
                    if shots[i] and shots[i + 1]:
                        d = px_diff(shots[i], shots[i + 1])
                        if best is None or d["changed"] > best["changed"]:
                            best = d
                if best is None:
                    print("  ui frames      could not capture the window")
                else:
                    results["jalv"]["ui_moving"] = best
                    if best["changed"] <= NOISE_PX:
                        results["jalv"]["ui_frozen"] = True
                        print(f"  ui frames      FROZEN "
                              f"({best['changed']} px changed between any two captures)")
                    else:
                        print(f"  ui frames      {best['changed']} px changed "
                              f"(max {best['max_delta']})  <-- animating")

        # ---- 6. measure ------------------------------------------------
        print("\nlive  (real FLAC through the DSP, 48 kHz / 1024 frames)")
        cpu = measure_cpu(jalv.pid, args.seconds)
        cores = os.cpu_count() or 1
        results["live"] = {"cpu": cpu, "seconds": args.seconds}
        print(f"  jalv cpu       {cpu:6.1f} % of one core "
              f"({cpu / cores:.1f} % of the machine, {cores} cores)")

        # JACK's own DSP load: how much of the block period the graph uses.
        # This is the number that decides whether audio glitches, and it is not
        # derivable from process CPU — a plugin can burn 30 % of a core while
        # only using 5 % of its deadline.
        try:
            cl = subprocess.run(["env", f"JACK_DEFAULT_SERVER={JACK_SERVER}",
                                 "timeout", "3", "jack_cpu_load"],
                                capture_output=True, text=True, timeout=8).stdout
            nums = [float(m) for m in re.findall(r"([\d.]+)\s*$", cl.strip(), re.M)]
            peak = max(nums) if nums else None
            results["live"]["dsp_load"] = peak
            if peak is not None:
                print(f"  dsp load       {peak:6.1f} % of the block period")
        except Exception:
            pass

        # ---- 7. did either process fall over? --------------------------
        ok_alive = jalv.alive() and play.alive()
        results["live"]["alive"] = ok_alive
        print(f"  processes      jalv {'alive' if jalv.alive() else 'DIED'}, "
              f"mpv {'alive' if play.alive() else 'DIED'}")
        if not ok_alive:
            for p in (jalv, play):
                if not p.alive():
                    print(f"  {p.name} said:")
                    print("   " + (p.log_text() or "(no output)")[-900:].replace(
                        "\n", "\n   "))
        return finish(results, args)

    finally:
        for p in (play, jalv):
            if p is not None:
                p.stop()
        if jack is not None:
            jack.stop()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--display", default=":0")
    ap.add_argument("--size", default="1040x772")
    ap.add_argument("--ref", help="reference PNG for the style diff")
    ap.add_argument("--ref-save", help="capture a reference render here and exit")
    ap.add_argument("--json", help="write the results as JSON here")
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("-j", "--jalv", action="store_true",
                    help="bench the LV2 plug-in in jalv (console host), "
                         "fed with real audio")
    # -G picks the GUI host:  -Gj = jalv.gtk3,  -Gc = carla-single.
    # Short options take their value attached, so both `-Gj` and `-G j` work;
    # the attached form is the one to type.
    ap.add_argument("-G", dest="gui_host", choices=["j", "c"], metavar="{j,c}",
                    help="-Gj: jalv.gtk3 with the plug-in UI;  "
                         "-Gc: carla-single with it")
    ap.add_argument("--flac",
                    default=os.path.expanduser("~/wtf/export/K33C - Fixed.flac"),
                    help="audio file mpv plays into the plug-in (--jalv only)")
    ap.add_argument("--seconds", type=float, default=6.0,
                    help="measurement window in seconds (--jalv only)")
    ap.add_argument("--no-audio", action="store_true",
                    help="skip mpv (--jalv only): the negative control that "
                         "proves the signal check can fail")
    args = ap.parse_args()

    if args.gui_host is not None:
        ensure_dir()
        if args.gui_host == "c":
            return run_carla(args)
        args.jalv = True          # -Gj is the jalv path with its gtk3 UI
        args.ui = True
    elif not hasattr(args, "ui"):
        args.ui = False
    if args.jalv:
        ensure_dir()
        return run_jalv(args)

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
    # Same tolerance the style line itself reports with: a handful of pixels at
    # a tiny delta is antialiasing noise on text edges, not a style change.
    # Testing `changed` alone made this summary say FAIL on runs whose own
    # detail line had just printed PASS.
    det = results.get("deterministic", {})
    if det.get("changed", 0) > NOISE_PX or det.get("max_delta", 0) > NOISE_D:
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
    # jalv mode: the plug-in must stay up and keep up.  A DSP load at or over
    # 100 % means it is missing deadlines, i.e. clicking, whatever the UI shows.
    if results.get("mode") in ("jalv", "carla"):
        if results.get("live", {}).get("alive") is False:
            bad.append("a process died during the run")
        dsp = results.get("live", {}).get("dsp_load")
        if dsp is not None and dsp >= 100.0:
            bad.append(f"dsp load {dsp:.0f}% — missing deadlines")
        sig = results.get("audio", {}).get("signal")
        if sig is not None and sig.get("peak", 0) < 1e-4:
            bad.append("the plug-in output is silent")
        if results.get("jalv", {}).get("ui_frozen"):
            bad.append("the editor window froze")
        if results.get("jalv", {}).get("fills_canvas") is False:
            bad.append("the editor does not fill the host's canvas")
        if results.get("jalv", {}).get("ui_missing"):
            if results["jalv"].get("ui_unmapped"):
                bad.append("the editor window was created but never mapped")
            else:
                bad.append("the editor window never appeared")
    print("\n" + ("FAIL: " + ", ".join(bad) if bad else "PASS"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

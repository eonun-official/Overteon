#!/usr/bin/env python3
"""Collect the Qt files a Windows plug-in actually needs at load time.

A Qt install ships every module and every plug-in it was built with; a given
plug-in uses a handful.  Copying the whole install beside the binary is how a
12 MB plug-in becomes a 274 MB folder of 110 DLLs, most of which are never
opened -- the SQL driver for MySQL, the serial-port module, and the rest.

This walks the PE import graph from the binary into the Qt install and prints
what is reachable.  Only Qt's own bin/*.dll and plugins/**/*.dll are considered:
everything else is a system DLL that the user's Windows already has.

    qt-closure.py Overteon.dll --qt C:/Qt/6.9.3/msvc2022_64
    qt-closure.py Overteon.dll --qt ... --copy-to dist/    # actually copy
    qt-closure.py Overteon.dll --qt ... --report

Needs llvm-objdump, which reads a PE file on any host -- that is the point: the
Windows build is cross-built on Linux, so there is no Windows dumpbin around.
"""

import argparse
import os
import shutil
import subprocess
import sys


def pe_imports(path):
    """DLL names in the import table of a PE file (empty for a non-PE)."""
    try:
        out = subprocess.run(["llvm-objdump", "-p", path],
                             capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return []
    if out.returncode != 0:
        return []
    return [line.strip().split("DLL Name:", 1)[1].strip()
            for line in out.stdout.splitlines()
            if line.strip().startswith("DLL Name:")]


def index_qt(qt_dir):
    """lowercase name -> path, for Qt's bin/*.dll and plugins/**/*.dll."""
    pool = {}
    bindir = os.path.join(qt_dir, "bin")
    if os.path.isdir(bindir):
        for name in os.listdir(bindir):
            if name.lower().endswith(".dll"):
                pool.setdefault(name.lower(), os.path.join(bindir, name))

    plugroot = os.path.join(qt_dir, "plugins")
    for root, _dirs, files in os.walk(plugroot):
        # A debug build is never loaded by a release binary and would only
        # double what ships.
        debug_dir = os.path.basename(root).endswith("d")
        for name in files:
            low = name.lower()
            if not low.endswith(".dll"):
                continue
            if debug_dir and low.endswith("d.dll"):
                continue
            pool.setdefault(low, os.path.join(root, name))
    return pool


def closure(entry, pool):
    """Breadth-first walk of the import graph, restricted to the Qt pool."""
    needed, external = [], set()
    queue = list(entry) if isinstance(entry, (list, tuple)) else [entry]
    seen = set()
    while queue:
        current = queue.pop(0)
        for dep in pe_imports(current):
            key = dep.lower()
            if key in seen:
                continue
            seen.add(key)
            if key in pool:
                needed.append(pool[key])
                queue.append(pool[key])
            else:
                external.add(dep)
    return needed, external


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binary", help="the .exe or plug-in .dll")
    ap.add_argument("--qt", required=True, help="root of the Qt install used to build it")
    ap.add_argument("--copy-to", metavar="DIR",
                    help="copy the reachable files into DIR, keeping Qt's layout")
    ap.add_argument("--extra", action="append", default=[], metavar="REL",
                    help="additional root inside the Qt install, e.g. "
                         "plugins/platforms/qwindows.dll -- these are loaded by "
                         "name at run time, so they appear in no import table")
    ap.add_argument("--report", action="store_true", help="per-file detail")
    args = ap.parse_args()

    if not os.path.isfile(args.binary):
        sys.exit(f"no such binary: {args.binary}")
    if not os.path.isdir(args.qt):
        sys.exit(f"no such Qt install: {args.qt}")

    pool = index_qt(args.qt)
    if not pool:
        sys.exit(f"{args.qt} has no Qt DLLs under bin/ or plugins/")

    roots = [args.binary]
    for extra in args.extra:
        path = extra if os.path.isabs(extra) else os.path.join(args.qt, extra)
        if not os.path.isfile(path):
            sys.exit(f"--extra: no such file: {path}")
        roots.append(path)

    needed, external = closure(roots, pool)

    # The extra roots themselves are wanted in the output -- they are the files
    # loaded by name at run time, which is why they had to be named by hand.
    for path in roots[1:]:
        if path not in needed:
            needed.append(path)
    total = sum(os.path.getsize(p) for p in needed)
    n_plugins = sum(1 for p in needed if os.sep + "plugins" + os.sep in p)

    print(f"{len(needed) - n_plugins} module DLL(s), {n_plugins} plug-in(s), "
          f"{total / 1048576:.1f} MB of {len(pool)} available", file=sys.stderr)
    for path in sorted(needed):
        if args.report:
            print(f"  {os.path.relpath(path, args.qt):<46} "
                  f"{os.path.getsize(path) // 1024:>7} KB", file=sys.stderr)
        else:
            print(os.path.relpath(path, args.qt))
    if args.report:
        print(f"  system DLLs, already on every Windows: {len(external)}",
              file=sys.stderr)

    if args.copy_to:
        plugroot = os.path.join(args.qt, "plugins")
        for path in needed:
            rel = os.path.relpath(path, plugroot)
            if rel.startswith(".."):
                rel = os.path.basename(path)      # from bin/: goes at the root
            dest = os.path.join(args.copy_to, rel)
            os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
            shutil.copy2(path, dest)
        print(f"copied {len(needed)} file(s) to {args.copy_to}", file=sys.stderr)


if __name__ == "__main__":
    main()

# Overteon — technologies

How this thing is built, laid out and shipped. For what the plug-in *is*, read
[README.md](README.md).

The **Qt6/QML** editor for Overteon.  Same DSP, same parameter set, same
presets, same plug-in identity as the original — the editor is rewritten in
Qt Quick instead of the HTML/JS WebView.

The original WebView version is kept as the visual reference in
`Assets/reference/original/web_fx/`: every screen in `Source/ui/qt/qml/` is a
1:1 port of a construct in that page.

## Layout

```
CMakeLists.txt  forwards into Config/, so `cmake ..` works from a build dir
Config/     build system — CMakeLists.txt, presets, toolchain files
Source/     everything that is compiled
  src/        the JUCE plug-in (processor, parameters, DSP)
  ui/qt/      the Qt editor: C++ items, QML, shaders, resources
  web_fx/     the WebView editor's assets (legacy -DOVERTEON_QT_UI=OFF path)
  third_party/ vendored JUCE, VST2 SDK, WebView2 (not tracked)
Tools/      developer tools that are not built into the product
  units/      headless checks for the meters and the analyser
  blurdemo/   the CPU-vs-GPU blur comparison
  preview-server/  the debug server the WebView editor talks to
Scripts/    task scripts — demo, packaging, Windows and VM deployment
Assets/     data that ships or documents the product
  presets/    factory and user .hinj
  reference/  the original web UI, kept for comparison
  ov_shaders/ generated shader bundle manifest
  docs/       changelogs and build notes
Build/      all build output (git-ignored, except dist-win/): the product
            directories, the ninja trees, and the release archives
```

Nothing outside `Config/`, `Source/`, `Tools/`, `Scripts/` and `Assets/` is
tracked, apart from the top-level `CMakeLists.txt`, the README and the
`.gitignore`. Every path inside the build system is relative — the tree is
meant to be cloned anywhere, and the products still land in `<repo>/Build/`
whichever directory the build tree is in.

## Product identity

Identical to the original plug-in, so a session saved with the old build loads
in this one without remapping, and `.hinj` presets work in both directions:

| | value |
|---|---|
| product name | `Overteon` |
| LV2 URI | `https://eonun.com/plugins/Overteon` |
| bundle id | `com.eonun.overteon` |
| plug-in code | `Ovtn` |
| bundle | `Overteon.lv2` / `Overteon.vst3` |
| preset directory | `~/Eonun/Overteon Presets` |
| QML import namespace | `Overteon 1.0` |
| wordmark | `OVER\|TEON` |
| parameters | 58, unchanged |
| editor | Qt6 / QML + RHI |

Because the identity is the original one, this build and the original WebView
build cannot be installed side by side — install one or the other.

## Getting the tree

JUCE is a submodule pinned to upstream tag 8.0.13, so clone recursively:

```sh
git clone --recursive <this repository>
```

A clone made without `--recursive` has an empty `Source/third_party/JUCE` and
configure stops with a message saying so; `git submodule update --init
--recursive` fixes it. Nothing else has to be downloaded: the pinned JUCE is
what the build expects, and configure does no network I/O when the submodule is
present.

## Build

The build system is `Config/CMakeLists.txt`, and the CMakeLists at the top of
the repository just forwards into it, so either entry point works:

```sh
# conventional
mkdir -p build && cd build && cmake .. -G Ninja && ninja vst3

# explicit, from the repository root
cmake -S . -B Build/host -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C Build/host vst3
```

`-G Ninja` is worth typing: the default generator here is Unix Makefiles, and
`cmake ..` alone gives you `make vst3` instead.

A build tree remembers which entry point configured it — CMake refuses to
reuse `Build/host` once it was configured through the other one. Pick one per
tree, or start a fresh directory: `cmake -S . -B Build/host` after
`cmake -S Config -B Build/host` fails with "does not match the source used to
generate cache" until you delete the cache.

### Targets

```sh
ninja vst3   ninja vst2   ninja lv2   ninja standalone
ninja preview      # overteon-qml-preview, the UI without a host
ninja tools        # ovctl and the headless meter/analyser checks
ninja package      # Build/packages/<build name>.zip
ninja              # everything
```

Each format target builds the shared code plus that one format. JUCE's own
targets (`Overteon_VST3`, `Overteon_LV2`, `Overteon_VST`, `Overteon_Standalone`)
still exist; the short names are the ones to type. A format left out of
`OVERTEON_FORMATS` has no short name, so asking for it fails with ninja's own
"unknown target" rather than silently building nothing.

`vst2` means VST2 — that is the format, not a typo for a second VST3.

### Runtime log

Build with `-DOVERTEON_FILE_LOG=ON` to have every process run write a
timestamped plain-text log into the user's home directory:

```sh
cmake -S . -B Build/log -G Ninja -DCMAKE_BUILD_TYPE=Release -DOVERTEON_FILE_LOG=ON
ninja -C Build/log standalone
ls -t ~/Overteon/*.log | head -1
```

The file is `~/Overteon/<date>.<time>.log`, for example
`~/Overteon/2026-11-23.19-26-25.log`. One file is created per run: a later run
never appends to or overwrites an earlier log, so two sessions cannot
interleave. It records the version and process id, instance creation and
destruction, the audio format passed to `prepareToPlay`, host state restores,
the Qt version (or the reason Qt could not start), whether the QML scene loaded
and any QML error text, and the editor open/close with its size.

Qt's own diagnostics go into the same file: `qDebug`/`qInfo`/`qWarning`/
`qCritical`, the QML engine's warnings, and the platform and scene-graph
categories, with the category, source file and line. Debug categories are
switched on in this build; the per-item and per-frame internals (font database,
emoji segmenter, dirty/clip bookkeeping, software-context renderables) are
filtered because at 60 Hz they alone produce thousands of lines a second and
bury the events a report needs. `OVERTEON_QTLOG_FULL=1` turns even those back on,
and `QT_LOGGING_RULES` overrides both. The log also records the Qt and JUCE versions, the platform
plug-in, every screen (geometry, DPR, DPI, refresh rate), the selected RHI
backend and shader configuration, the `QSG_*`/`QT_*`/`OVERTEON_*` switches in
effect, editor embedding and resize events, display-setting changes and preset
loads.

The option is OFF by default. A build without it creates neither the directory
nor any file, and leaves the host's JUCE logger untouched.

#### Reporting a problem

A log build also carries an issue reporter, compiled in only when
`OVERTEON_FILE_LOG=ON`. The Settings panel grows a **DIAGNOSTICS → REPORT
ISSUE (LOG)** row; the editor offers the same action automatically when the UI
could not start.

* With `OVERTEON_GITHUB_TOKEN` set (or a token compiled in with
  `-DOVERTEON_REPORT_TOKEN=...`) the log tail is posted to
  `https://api.github.com/repos/eonun-official/Overteon/issues` and the new
  issue URL is written to the log.
* Without a token nothing is sent: the full log is copied to the clipboard and
  the prefilled new-issue page is opened, so pasting and submitting stays the
  user's decision.

The home directory and any other user's home in log lines are redacted to `~`,
and the token never appears in the uploaded text or the log. A token compiled
into a distributed binary can be extracted from it — use a fine-grained,
issues-only token and rotate it, or prefer the environment variable.
`OVERTEON_REPORT_DISABLE=1` turns reporting off, `OVERTEON_REPORT_AUTO=1`
allows the automatic path without a token, `OVERTEON_REPORT_NO_BROWSER=1`
suppresses opening a browser, and `OVERTEON_REPORT_API_BASE` /
`OVERTEON_REPORT_REPO` point the uploader at a test server or a fork. The
whole reporter is absent from an OFF build: no row, no code, no network.

##### A build with the token compiled in

For an automation-report build the token can be embedded, so that a user never
has to configure anything:

```sh
OVERTEON_REPORT_TOKEN=github_pat_xxx Scripts/log-report-build.sh
```

The script puts the token in the CMake environment only; CMake writes it to
`ov_report_token.h` inside the build tree instead of a compiler command line.
The result is a release build whose REPORT ISSUE button (and the automatic
report on a failed editor start) creates issues directly. `strings` on the
binary will reveal the token: keep such builds on a channel you control, use a
fine-grained token with only `Issues: write` on this repository, and rotate it
when an artifact leaks. A run-time `OVERTEON_GITHUB_TOKEN` overrides the
compiled-in value, which makes rotating a test build possible without
rebuilding.

### Windows x64 cross-compile

```sh
cmake -S . -B Build/win -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/Config/cmake/toolchain-clangcl-xwin.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=$HOME/Qt-msvc/6.9.3/msvc2022_64
WINEPATH=$HOME/Qt-msvc/6.9.3/msvc2022_64/bin ninja -C Build/win vst3
```

`CMAKE_TOOLCHAIN_FILE` must be absolute: CMake resolves it against the *build*
directory, not the source directory. The presets in `Config/CMakePresets.json`
have the same problem and are therefore only useful for the host builds.

Two patches under `Config/cmake/patches/` make the cross-compile work, and
configure applies them to the submodule itself, only when
`CMAKE_CROSSCOMPILING AND WIN32`:

* `juce-vst3-helper-forward-toolchain.patch` — JUCE configures `juce_vst3_helper`
  as a separate CMake project and forwards only the compiler paths, so that
  nested configure sees the host platform and dies probing clang-cl. Upstream
  8.0.13 does not forward the toolchain, the emulator or the build type.
* `juce-mingw-guard.patch` — `juce_TargetPlatform.h` refuses MinGW outright;
  the Qt editor can be built with Qt's MinGW toolchain, so the guard is relaxed.

Because they apply to the submodule, a cross-compiling tree shows
`Source/third_party/JUCE` as modified. A host build does not: it leaves the
submodule pristine, and configure checks for each patch's marker before applying
it, so re-running cmake is harmless.

Products land in one directory per build, named for what it is, where and
when it was made:

```
Build/Overteon_20261116-191811_unknown_linux_x86_64/
    VST3/Overteon.vst3
    LV2/Overteon.lv2
    VST/libOverteon.so
    Standalone/Overteon
    overteon-qml-preview
    ovctl
Build/Overteon_20261116-192422_unknown_windows_x86_64/
    VST3/Overteon.vst3   VST/Overteon.dll   LV2/Overteon.lv2   Standalone/Overteon.exe
```

`Build/host` and `Build/win` are the ninja build trees — scratch, not products.
The product directory name is `<BuildName>_<Time>_<Host>_<OS>_<Platform>`, where
the time is the time of *configuration* and is cached so that re-running cmake
keeps writing into the same directory; `-DOVERTEON_BUILD_STAMP=...` chooses a
different one. A non-Release build type is part of `<BuildName>`
(`Overteon-Debug_...`) because single-config generators have no per-configuration
subdirectory. Scripts never hard-code this path: they source
`Scripts/product.sh` and call `product_dir linux|windows`.

`-DOVERTEON_QT_UI=OFF` builds the WebView editor instead (useful for
side-by-side comparison with the original).

## What ends up in the binary

The development builds made here link Qt as a shared library, so the plug-in
contains no Qt code — the modules appear in its `DT_NEEDED` / import table and
are loaded by the host at run time. The released single-file VST3 is a different
proposition: it statically links Qt Core, Gui, Qml, Quick, Network and OpenGL, so
that one is a single file with no runtime beside it. Both are licence-compatible
with AGPL-3.0 + Qt's GPLv3 option, and NOTICE states which is which; see
[README.md](README.md) section 15. What the binary *does* carry for no good
reason is a symbol table: `.symtab` and its 868 KB string table exist for a
debugger and are dead weight in a plug-in that ships. Release builds now link
with `-s`:

| artefact | before | after |
|---|---|---|
| VST3 `Overteon.so` | 8 463 048 | 7 261 576 |
| LV2 `libOverteon.so` | 8 442 640 | 7 069 128 |
| Standalone `Overteon` | 9 010 976 | 7 725 696 |

`.dynsym` is untouched, so `lv2_descriptor`, `GetPluginFactory` and
`VSTPluginMain` are all still exported and the plug-in still loads — verified by
running the stripped LV2 in `jalv.gtk3` and by `dlopen`ing all three formats.
`-DOVERTEON_STRIP_SYMBOLS=OFF` keeps the symbols; Debug and RelWithDebInfo builds
keep them regardless, because those exist to be debugged.

Windows needs nothing here: a PE file has no symbol table, the symbols go in a
separate `.pdb`, and packaging already leaves that out.

## The Qt that ships beside it

A Windows plug-in cannot use the host's Qt, so it needs its own — and copying the
whole Qt install is how a 12 MB plug-in becomes 274 MB of 110 DLLs, most of them
never opened. The import graph reaches nine modules and one platform plug-in:

```sh
python3 Scripts/qt-closure.py Overteon.dll --qt $QT --report
#  9 module DLL(s), 1 plug-in(s), 35.2 MB of 174 available
```

`Scripts/qt-closure.py` walks the PE import tables with `llvm-objdump`, which
reads a Windows binary on this Linux host — that is why it exists rather than
`windeployqt`. `--with-qt` on the packager and `Scripts/deploy-win.sh` both use
it, taking the Windows archive from 78 MB to 26 MB.

## Package

```sh
ninja -C Build/host package        # -> Build/packages/Overteon_<stamp>.zip
ninja -C Build/win  package        # the Windows archive, cross-built on Linux
```

`Scripts/make-package.sh` stages the plug-in formats, the factory presets, the
changelogs and a generated `README.txt` into one folder named for the version
and the platform, and zips it. The archive keeps the product directory's name,
so it always says which build it came from, and a `.sha256` sits beside it. The
archive is verified after writing (`unzip -t` plus a check that the bundles are
present), because a truncated archive is the classic packaging bug.

Developer tools (`overteon-qml-preview`, `ovctl`, the headless tests,
`libOverteon_SharedCode.a`) and Windows import libraries are not included —
they are not something a user installs.

```sh
./Scripts/make-package.sh --os windows --with-qt   # carry the Qt6 runtime too
./Scripts/make-package.sh --product DIR --out DIR --force
```

A Windows archive is not runnable on its own, because the editor resolves
`Qt6Core.dll` and friends at load time; `--with-qt` copies the Qt runtime into
`qt/` inside the archive and says so in its README. It is off by default so the
download stays 12 MB instead of 78 MB.

`-DOVERTEON_AUTO_PACKAGE=ON` adds packaging to `ALL`, so a plain `ninja` also
produces the archive. It costs a `find` when nothing changed, since the script
exits immediately if the archive is newer than every product.

`Scripts/package.sh` is a different job: it installs the LV2 bundle into
`~/.lv2` on this machine and verifies the copy.

## Try the UI without a host

```sh
. Scripts/product.sh
P=$(product_dir linux)
"$P/overteon-qml-preview" --size 1040x772                # interactive
"$P/overteon-qml-preview" --size 1040x772 --freeze --shot x.png
```


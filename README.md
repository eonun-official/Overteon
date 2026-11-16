# Overteon

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


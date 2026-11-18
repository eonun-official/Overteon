# Overteon

The **Qt6/QML** editor for Overteon.  Same DSP, same parameter set, same
presets, same plug-in identity as the original — the editor is rewritten in
Qt Quick instead of the HTML/JS WebView.

The original WebView version is kept as the visual reference in
`Assets/reference/original/web_fx/`: every screen in `Source/ui/qt/qml/` is a
1:1 port of a construct in that page.

## Layout

```
Config/     build system — CMakeLists.txt, CMakePresets.json, toolchain files
Source/     everything that is compiled
  src/        the JUCE plug-in (processor, parameters, DSP)
  ui/qt/      the Qt editor: C++ items, QML, shaders, resources
  web_fx/     the WebView editor's assets (legacy -DOVERTEON_QT_UI=OFF path)
  third_party/ pinned SDKs (JUCE fetched from upstream, VST2 headers tracked,
              WebView2 only for the legacy WebView path)
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
Build/      all build output (git-ignored, except dist-win/)
```

Nothing outside `Config/`, `Source/`, `Tools/`, `Scripts/` and `Assets/` is
tracked, and every path inside the build system is relative — the tree is
meant to be cloned anywhere.

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

The source tree is `Config/`; the build tree goes under `Build/`.

```sh
# Linux host — vendored JUCE, no network needed
cmake -S Config -B Build/host -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C Build/host

# Windows x64 cross-compile (clang-cl + xwin + an MSVC Qt)
cmake -S Config -B Build/win -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/Config/cmake/toolchain-clangcl-xwin.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=$HOME/Qt-msvc/6.9.3/msvc2022_64
WINEPATH=$HOME/Qt-msvc/6.9.3/msvc2022_64/bin ninja -C Build/win
```

`CMAKE_TOOLCHAIN_FILE` must be absolute: CMake resolves it against the *build*
directory, not the source directory. The presets in `Config/CMakePresets.json`
have the same problem and are therefore only useful for the host builds.

Artefacts land in `Build/host/Overteon_artefacts/Release/` and
`Build/win/Overteon_artefacts/Release/`.

`-DOVERTEON_QT_UI=OFF` builds the WebView editor instead (useful for
side-by-side comparison with the original).

## Licence

Overteon is licensed under the **GNU Affero General Public License v3**
(AGPL-3.0) — the full text is in [LICENSE](LICENSE).

It builds on JUCE 8.0.13 (AGPLv3 option) and Qt 6.9.3 (GPLv3 open-source
option).  The released single-file VST3 statically links Qt; the development
runtime under `Build/dist-win` uses shared Qt DLLs and is not a release
artifact.  Every third-party component, its licence and the exact combination
terms are declared in [NOTICE](NOTICE); verbatim licence texts are in
[LICENSES/](LICENSES/).

This notice applies to the current release; it is on `main` and on the
`v0.9.20-licences` tag.  Older tags and development snapshots are retained as
history and are superseded by it; do not redistribute an older binary without
the current NOTICE and `LICENSES/`.

Because the complete corresponding source and build scripts are published here
and tagged per release, and JUCE is pinned to its exact upstream tag, the
static-linking obligations of the GPLv3/LGPLv3 are satisfied — no separate
object files are needed.

The "Overteon" and "OVER|TEON" names and the logo are project marks and are not
licensed by the AGPL-3.0; modified versions distributed to others must be
renamed.  See [NOTICE](NOTICE).

## Try the UI without a host

```sh
./Build/host/overteon-qml-preview --size 1040x772                     # interactive
./Build/host/overteon-qml-preview --size 1040x772 --freeze --shot x.png
```

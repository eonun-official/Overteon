# Overteon — Qt6/QML editor

The plug-in keeps JUCE for DSP, parameters and host integration; only the
editor is Qt.  `-DOVERTEON_QT_UI=OFF` builds the original WebView editor
instead (`src/OverteonWebEditor.h`).

```
ui/qt/
  OvSource.h        abstract data source (std only, no JUCE)
  OvJuceSource.*    live processor: parameters, meters, FFT, presets
  OvMockSource.h    same parameter table + animated meters (preview only)
  OvParam.*         one parameter as a QML object (norm / value / display)
  OvEngine.*        facade handed to QML as the `engine` context object
  OvEqView.*        the EQ plot: RBJ maths, grid, spectrum, nodes, dragging
  OvMeterView.*     header pod: hooked IN/OUT bars + harmonic ring
  OvQmlEditor.*     JUCE editor that reparents a QQuickView into the host
  OvQmlTypes.h      qmlRegisterType for the two painted items
  qml/              the UI itself (1:1 with web_fx/index.html)
  preview/          `overteon-qml-preview`, a host-free viewer
```

## Visual fidelity

`web_fx/index.html` is the reference.  The QML mirrors it directly:

| design (CSS)                 | QML                                   |
|------------------------------|---------------------------------------|
| `:root` colour tokens        | `OvTheme.qml` (same hex values)       |
| `#wrap` 1040x772             | `main.qml` design item, uniformly scaled |
| `.kring` / `.karc` / `.kndl` | `OvKnob.qml` (270° sweep from 12 o'clock) |
| `.ddbtn` / `.ddmenu`         | `OvDropdown.qml`                      |
| `.tbtn`                      | `OvToggle.qml`                        |
| `.rangebadge` / `.rangemenu` | `OvRangeBadge.qml`                    |
| `#presetMenu`                | `OvPresetButton.qml`                  |
| `.ctxmenu` (node types)      | `OvBandMenu.qml`                      |
| `#hpod` SVG meter pod        | `OvMeterView` (QPainter)              |
| `canvas` EQ engine           | `OvEqView` (QPainter, same RBJ code)  |

## The two blurs

`backdrop-filter: blur(3px) saturate(.85)` is the design's frosted glass on the
header ellipse and the two control-pit trapezoids.  Plug-in hosts frequently
ignore it, so both front ends compute the blur themselves:

* **HTML** — `web_fx/index.html` contains `FrostEngine`: the region under the
  glass is composited into a half-resolution buffer, run through a 3-level
  dual-Kawase pyramid (4-tap downsample, tent upsample blended with the finer
  level), saturated, glow-tinted and clipped to the trapezoid / ellipse shape.
  `body.frostjs` switches off the CSS `backdrop-filter` so nothing is blurred
  twice.  It re-renders whenever the EQ canvases or the spectrum change.

* **QML** — `OvBlur.qml` runs two Gaussian passes through Qt Quick
  `ShaderEffect`. The shader sources are Slang: `blur.slang`, `glass.slang`,
  the shared interface `effect.slang`, and `gaussian.slang`. The build compiles
  Slang directly to SPIR-V; a host-only Qt adapter packages reflection and
  SPIR-V/MSL/HLSL variants into QSB. No GLSL sources, intermediate files, or
  package variants are used.

## Graphics backend

The editor and preview select Vulkan on Linux and Wine. Native macOS uses
Qt's Metal default, and native Windows uses D3D. Explicit Qt environment
choices are preserved. OpenGL is not supported by these effects: there is no
GLSL variant. A host that already initialized its own Qt renderer must use
Vulkan, Metal or D3D for these effects as well.

Install the [Slang compiler](https://github.com/shader-slang/slang/releases)
and set `-DOVERTEON_SLANGC=/path/to/slangc` if it is not on PATH. For local
development, the build also searches `Build/tools/slang/bin`. The adapter
requires matching Qt Gui/ShaderTools private development headers on the build
host; this private API is isolated from the plug-in runtime. See
`Tools/slang-qsb/README.md` for its standalone build.

For cross-compilation, set `OVERTEON_SLANG_QSB` to a prebuilt native adapter,
or set `OVERTEON_SHADER_HOST_QT` to a native Qt kit so CMake builds the adapter
in a separate host project. The Slang compiler must also run on the build host.

Check the baked stages and backend variants with the Qt kit's `qsb` tool
(used only to inspect packages, never to compile GLSL):

```sh
ninja -C Build/host overteon_shaders
ruby Tools/units/check-shaders.rb /path/to/qsb Build/host/ov_shaders
```

## Menus

Qt `Popup` lives in the window overlay and ignores the design's scale, which
put menus at the wrong size and position.  Every menu is therefore a plain
`Item` reparented to the design root through the `OvMenu` singleton, with a
transparent click-catcher underneath (`main.qml`).

## Trying it without a host

Products are written to `Build/<BuildName>_<Time>_<Host>_<OS>_<Platform>/`, so
the copy to run is the one in the newest of those directories:

```sh
cmake --preset ninja && ninja -C Build/host overteon-qml-preview
. Scripts/product.sh
P=$(product_dir linux)
"$P/overteon-qml-preview" --size 1040x772            # interactive
"$P/overteon-qml-preview" --size 1040x772 --freeze --shot out.png
```

## In a host

```sh
ninja -C Build/host Overteon_LV2
. Scripts/product.sh
LV2_PATH="$(product_dir linux)/LV2" jalv.gtk3 -s https://eonun.com/plugins/Overteon
```

The editor creates one `QGuiApplication` per process, reparents its
`QQuickView` into the host's native handle (`QWindow::fromWinId`) and pumps Qt
from a JUCE timer, because the plug-in never owns the message loop.

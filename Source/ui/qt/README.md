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

* **QML** — `OvGlass.qml` uses Qt's built-in `MultiEffect` (pre-compiled
  multi-pass gaussian; Qt 6 does not accept runtime GLSL).  Opaque side fills
  painted on top cut it back to the trapezoid, exactly like `.eqhdr::before`
  and `::after` do in the CSS.

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

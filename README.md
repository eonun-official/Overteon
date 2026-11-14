# Overteon

The **Qt6/QML** editor for Overteon.  Same DSP, same parameter set, same
presets, same plug-in identity as the original — the editor is rewritten in
Qt Quick instead of the HTML/JS WebView.

The original WebView version lives in the sibling directory **`../OVERTEON`**
and stays the visual reference: every screen in `ui/qt/qml/` is a 1:1 port of a
construct in `../OVERTEON/web_fx/index.html`.

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

```sh
cmake -S . -B build -G Ninja              # vendored ./JUCE, no network
ninja -C build -j"$(nproc)"               # LV2 + VST3 + Standalone
ninja -C build overteon-qml-preview       # host-free UI viewer
```

Artefacts: `build/HarmonicInjector_artefacts/Release/{LV2/OVERTEON.lv2,VST3,Standalone}/`

`-DOVERTEON_QT_UI=OFF` builds the WebView editor instead (useful for
side-by-side comparison with OVERTEON).

## Try the UI without a host

```sh
./build/overteon-qml-preview --size 1040x772                     # interactive
./build/overteon-qml-preview --size 1040x772 --freeze --shot x.png
```

See `ui/qt/README.md` for the architecture and the CSS → QML mapping table.

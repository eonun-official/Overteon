# OnyxTEON

The **Qt6/QML fork** of Overteon.  Same DSP, same parameter set, same presets —
the editor is rewritten in Qt Quick instead of the HTML/JS WebView.

The original WebView version lives in the sibling directory **`../OVERTEON`**
and stays the visual reference: every screen in `ui/qt/qml/` is a 1:1 port of a
construct in `../OVERTEON/web_fx/index.html`.

## Product identity

Deliberately different from OVERTEON so both plug-ins can be installed and
loaded side by side in the same host:

| | OVERTEON | OnyxTEON |
|---|---|---|
| LV2 URI | `https://eonun.com/plugins/Overteon` | `https://eonun.com/plugins/OnyxTEON` |
| bundle id | `com.eonun.overteon` | `com.eonun.onyxteon` |
| plug-in code | `Ovtn` | `Onyx` |
| bundle | `Overteon.lv2` / `.vst3` | `OnyxTEON.lv2` / `.vst3` |
| editor | WebView (HTML) | Qt6 / QML |

Parameter IDs are **identical**, so `.hinj` presets are interchangeable
between the two.

## Build

```sh
cmake -S . -B build -G Ninja              # vendored ./JUCE, no network
ninja -C build -j"$(nproc)"               # LV2 + VST3 + Standalone
ninja -C build overteon-qml-preview       # host-free UI viewer
```

Artefacts: `build/HarmonicInjector_artefacts/Release/{LV2/ONYXTEON.lv2,VST3,Standalone}/`

`-DOVERTEON_QT_UI=OFF` builds the WebView editor instead (useful for
side-by-side comparison with OVERTEON).

## Try the UI without a host

```sh
./build/overteon-qml-preview --size 1040x772                     # interactive
./build/overteon-qml-preview --size 1040x772 --freeze --shot x.png
```

See `ui/qt/README.md` for the architecture and the CSS → QML mapping table.

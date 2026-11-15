# Preview server (vibe.d) — serving the editor UI over HTTP

The editor UI normally runs inside the plug-in, talking to JUCE's WebView
backend (`window.__JUCE__`).  This server makes the same UI reachable from a
normal browser, so it can be rendered and compared with any engine — Gecko,
Blink, WebKit — without starting a DAW.

```
cd tools/preview-server
dub build --compiler=ldc2 --build=release     # first build fetches vibe.d
./keep-alive.sh start                          # serve on 0.0.0.0:8888
./keep-alive.sh status
./keep-alive.sh stop
```

## Routes

| route | what |
|---|---|
| `/` | landing page |
| `/original` | the **original** HTML+CSS — `web_fx/index.html` extracted from `~/SupersawOmega.rar` into `reference/original/web_fx` |
| `/current` | the current UI (FrostEngine dual-Kawase blur, OVER\|TEON) |
| `/compare` | both side by side in iframes |
| `/raw/original`, `/raw/current` | the untouched `index.html` files |
| `/edix.otf`, `/eonun_logo.png` | assets (identical in both trees) |
| `/healthz` | `ok <original root> \| <current root>` |

Bound to `0.0.0.0:8888`, so `http://192.168.1.11:8888/` works from the LAN.

## The mock backend

Neither page runs in a browser on its own: the design expects JUCE to supply
`__JUCE__`, slider states and native functions.  `source/app.d` injects
`mock-backend.js` before the vendored frontend library.  It implements the
same protocol

* `__JUCE__.postMessage({eventId, payload})` — JS → host
* `__JUCE__.backend.emitByBackend(eventId, json)` — host → JS

with the **same parameter table as `src/FxProcessor.cpp`** (identical ids,
ranges, skew and defaults) and JUCE's own skew convention
(`norm = lin^skew`, `value = start + span·norm^(1/skew)`), so curves, node
positions, knob arcs and readouts match the plug-in exactly.  Meters and the
spectrum are animated so the pod and the analyser move.

## Screenshots with Gecko

```sh
./shot-gecko.sh out.png 1040x772 http://127.0.0.1:8888/original
```

Starts the server if needed and captures with headless Firefox (Gecko) using a
throw-away profile, so it works while another Firefox instance is running.

## Keeping it alive

`keep-alive.sh` runs the server in a restart loop (`setsid`, detached), so a
crash or a manual kill brings it back after a second.  Log:
`/tmp/overteon-preview.log`.

Roots can be overridden without a rebuild:

```sh
OVERTEON_ORIGINAL_WEBFX=/path/to/original OVERTEON_CURRENT_WEBFX=/path/to/current \
  ./overteon-preview-server
```

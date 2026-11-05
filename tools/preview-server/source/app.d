// ===========================================================================
//  Overteon / OnyxTEON preview server (vibe.d)
//
//  Serves the editor UIs over HTTP so they can be rendered by a real browser
//  engine (Gecko/Firefox, Blink/Chromium, WebKit) instead of a plug-in host.
//
//    /                landing page with links
//    /original        the ORIGINAL HTML+CSS, extracted from ~/SupersawOmega.rar
//                     (the pristine design, no modifications)
//    /current         the current UI (FrostEngine dual-Kawase blur, ONYX|TEON)
//    /compare         both side by side in iframes
//    /raw/original    original index.html, untouched
//    /raw/current     current index.html, untouched
//    /edix.otf, /eonun_logo.png, /healthz
//
//  Neither page can run in a browser on its own: the design talks to JUCE's
//  WebView backend (window.__JUCE__).  So `mock-backend.js` is injected in
//  front of the document.  It implements the same protocol (slider/toggle
//  events, native function invocation) with the SAME parameter table as
//  src/FxProcessor.cpp — ids, ranges, skew, defaults — and animates meters and
//  spectrum, so curves, knobs and readouts match the plug-in exactly.
//
//  Bound to 0.0.0.0:8888 so it is reachable from the LAN.
// ===========================================================================
module app;

import vibe.vibe;
import std.string : replace, indexOf, split;
import std.path : buildPath;
import std.file : read, readText, exists;
import std.process : environment;

// ---------------------------------------------------------------------------
//  Roots.  Overridable so the same server binary can serve any checkout.
// ---------------------------------------------------------------------------
string envOr (string name, string fallback)
{
    const v = environment.get (name, "");
    return v.length ? v : fallback;
}

string originalRoot ()
{
    return envOr ("OVERTEON_ORIGINAL_WEBFX",
                  "/home/x_x/build/Onyx-z/OnyxTEON/reference/original/web_fx");
}

string currentRoot ()
{
    return envOr ("OVERTEON_CURRENT_WEBFX",
                  "/home/x_x/build/Onyx-z/OnyxTEON/web_fx");
}

string rootFor (string which)
{
    return which == "original" ? originalRoot () : currentRoot ();
}

/// The mock JUCE backend, injected before the vendored frontend library.
string mockBackendScript ()
{
    return `
<script>
/* ===========================================================================
   Overteon preview backend — stands in for JUCE's WebView integration so the
   editor UI runs in a plain browser.  Speaks the same protocol:
     JS  -> __JUCE__.postMessage({eventId, payload})
     host-> __JUCE__.backend.emitByBackend(eventId, jsonString)
   Parameter table mirrors src/FxProcessor.cpp exactly (same ids, ranges,
   skew and defaults), so curves, knobs and readouts match the plug-in.
   ========================================================================= */
(function () {
  const skewForCentre = (lo, hi, centre) =>
    Math.log(0.5) / Math.log((centre - lo) / (hi - lo));

  const F = (lo, hi, skew, def) => ({ kind: "f", lo, hi, skew, def });
  const C = (choices, def) => ({ kind: "c", choices, def });
  const B = (def) => ({ kind: "b", lo: 0, hi: 1, skew: 1, def });

  const driveSkew = skewForCentre(0, 10, 2);
  const freqSkew  = skewForCentre(5, 20000, 800);
  const qSkew     = skewForCentre(0.1, 6, 0.71);
  const airSkew   = skewForCentre(2000, 20000, 9000);

  const P = {};
  const add = (id, spec) => { spec.id = id; P[id] = spec; };

  add("drive",      F(0, 10, driveSkew, 0));
  add("mix",        F(0, 1, 1, 1));
  add("driveType",  C(["Tanh", "Tube", "Tape"], 0));
  add("driveMode",  C(["Classic", "Harmonic", "Schranz"], 1));
  add("osFactor",   C(["2x", "4x", "8x", "16x", "32x"], 1));
  add("inGain",     F(-24, 24, 1, 0));
  add("outGain",    F(-24, 24, 1, 0));
  add("deEmph",     F(0, 1, 1, 1));
  add("harmGain",   F(-12, 24, 1, 0));
  add("airProtect", F(2000, 20000, airSkew, 20000));
  add("evenOdd",    F(-1, 1, 1, 0));
  add("msMode",     C(["Stereo", "Mid", "Side", "M/S"], 0));
  add("autoGain",   B(0));
  add("harmSolo",   B(0));

  const defF = [100, 250, 1200, 4000, 8500];
  const defT = [2, 0, 0, 0, 3];
  const eqTypes = ["Bell", "Notch", "Low Shelf", "High Shelf",
                   "Low Cut 12", "Low Cut 24", "High Cut 12", "High Cut 24"];

  for (const pre of ["sc", "pq"]) {
    for (let b = 0; b < 5; ++b) {
      const nm = pre + (b + 1);
      const bump = (pre === "pq" && b === 3);
      add(nm + "Freq", F(5, 20000, freqSkew, bump ? 10000 : defF[b]));
      add(nm + "Gain", F(-12, 12, 1, 0));
      add(nm + "Q",    F(0.1, 6, qSkew, bump ? 0.9 : 0.707));
      add(nm + "Type", C(eqTypes, defT[b]));
    }
  }
  add("scRange", C(["+/-12 dB", "+/-24 dB", "+/-48 dB", "+/-90 dB"], 0));
  add("pqRange", C(["+/-3 dB", "+/-6 dB", "+/-12 dB", "+/-30 dB"], 2));
  add("protectMode", C(["Off", "Clip", "Modern", "True Peak"], 2));
  add("limiterOs", C(["2x", "4x", "8x", "16x"], 1));

  // default -> normalised, using JUCE's convention: norm = lin^skew
  for (const id in P) {
    const p = P[id];
    if (p.kind === "c") p.norm = p.def / (p.choices.length - 1);
    else p.norm = Math.pow((p.def - p.lo) / (p.hi - p.lo), p.skew);
    p.defNorm = p.norm;
    p.target = p.norm;
  }

  const presets = { Factory: ["default", "Air Lift", "Drum Glue", "Vocal Sheen"],
                    User: [] };

  const emit = (eventId, payload) =>
    window.__JUCE__.backend.emitByBackend(eventId, JSON.stringify(payload));

  // JUCE: convertFrom0to1 uses pow(norm, 1/skew)
  const scaledOf = (p) =>
    Math.pow(p.norm, 1 / p.skew) * (p.hi - p.lo) + p.lo;

  const propsOf = (p) => ({
    eventType: "propertiesChanged",
    start: p.lo, end: p.hi, skew: p.skew,
    name: p.id, label: p.id,
    numSteps: p.kind === "c" ? p.choices.length - 1 : 100,
    interval: 0, parameterIndex: 0,
  });

  const functions = ["applyInit", "savePreset", "loadPreset", "listPresets",
                     "loadPresetAt", "openExternal"];

  function handleInvoke(payload) {
    const { name, params, resultId } = payload;
    let result = true;

    if (name === "applyInit") {
      for (const id in P) P[id].target = P[id].defNorm;
    } else if (name === "listPresets") {
      result = JSON.stringify(presets);
    } else if (name === "loadPresetAt") {
      console.log("[preview] load preset", params[0], params[1]);
    } else if (name === "openExternal") {
      console.log("[preview] open", params[0]);
    }

    setTimeout(() => emit("__juce__complete", { promiseId: resultId, result }), 0);
  }

  window.__JUCE__ = {
    postMessage: function (raw) {
      let msg;
      try { msg = JSON.parse(raw); } catch (e) { return; }
      const { eventId, payload } = msg;

      if (eventId === "__juce__invoke") { handleInvoke(payload); return; }
      if (!payload) return;
      if (eventId.indexOf("__juce__") !== 0) return;

      const id = eventId.replace(/^__juce__(slider|toggle|comboBox)/, "");
      const p = P[id];
      if (!p) return;

      if (payload.eventType === "requestInitialUpdate") {
        emit(eventId, propsOf(p));
        emit(eventId, { eventType: "valueChanged", value: scaledOf(p) });
      } else if (payload.eventType === "valueChanged") {
        // the page sends SCALED values (SliderState.setNormalisedValue)
        const lin = (payload.value - p.lo) / (p.hi - p.lo);
        p.norm = Math.pow(Math.min(1, Math.max(0, lin)), p.skew);
        emit(eventId, { eventType: "valueChanged", value: scaledOf(p) });
      }
    },
    initialisationData: {
      __juce__platform: [],
      __juce__functions: functions,
      __juce__registeredGlobalEventIds: [],
      __juce__sliders: Object.keys(P),
      __juce__toggles: ["autoGain", "harmSolo"],
      __juce__comboBoxes: Object.keys(P).filter(k => P[k].kind === "c"),
    },
  };

  // ---- metering: same shapes the editor expects -------------------------
  const t0 = performance.now();
  setInterval(() => {
    const t = (performance.now() - t0) / 1000;
    const lvl = (x) => Math.max(-60, Math.min(6, x));
    const inL  = lvl(-22 + 7 * Math.sin(t * 0.9) + 2 * Math.sin(t * 4.7));
    const inR  = lvl(inL - 1.7 + 0.8 * Math.sin(t * 1.3));
    emit("meter", { il: inL, ir: inR, ol: inL + 0.6, or: inR + 0.6,
                    hm: -14 + 3 * Math.sin(t * 0.6) });

    const spec = [];
    for (let i = 0; i < 128; i++) {
      const x = i / 127;
      let v = 0.55 * Math.exp(-Math.pow((x - 0.32) / 0.24, 2))
            + 0.30 * Math.exp(-Math.pow((x - 0.72) / 0.18, 2))
            + 0.10 * (0.5 + 0.5 * Math.sin(t * 3 + x * 9));
      v *= 0.92 + 0.08 * Math.sin(t * 5 + x * 40);
      spec.push(Math.max(0, Math.min(1, v)));
    }
    emit("spectrum", spec);

    // ease towards the target so UI changes never jump
    for (const id in P) {
      const p = P[id];
      if (Math.abs(p.norm - p.target) > 1e-4) {
        p.norm += (p.target - p.norm) * 0.2;
        emit("__juce__slider" + id,
             { eventType: "valueChanged", value: scaledOf(p) });
      }
    }
  }, 33);

  console.log("[preview] mock backend ready,", Object.keys(P).length, "parameters");
})();
</script>
`;
}

/// One of the two index.html files with the mock backend spliced in.
string previewDocument (string which)
{
    const src = readText (buildPath (rootFor (which), "index.html"));

    // the vendored JUCE frontend is the first <script> in the document
    const idx = src.indexOf ("<script>");
    if (idx < 0) return src;

    return src[0 .. idx] ~ mockBackendScript () ~ src[idx .. $];
}

string landingPage ()
{
    return `
<!doctype html><meta charset=utf-8><title>Overteon preview</title>
<style>
  body { background:#16181d; color:#e7ebf2; font:13px/1.7 system-ui,sans-serif;
         margin:0; padding:40px }
  h1   { font-weight:400; letter-spacing:2px }
  a    { color:#43d9c4; text-decoration:none }
  a:hover { color:#ff5c8a }
  li   { margin:6px 0 }
  code { color:#8b93a3; font-size:12px }
  .dim { color:#5b6473 }
</style>
<h1>OVERTEON <span style="color:#ff5c8a">PREVIEW</span></h1>
<ul>
  <li><a href="/original">/original</a> — the original HTML+CSS
      <code>web_fx/index.html</code> from SupersawOmega.rar</li>
  <li><a href="/current">/current</a> — the current UI
      <code>(FrostEngine dual-Kawase blur)</code></li>
  <li><a href="/compare">/compare</a> — both side by side</li>
  <li><a href="/raw/original">/raw/original</a>,
      <a href="/raw/current">/raw/current</a> — untouched sources</li>
</ul>
<p class="dim">A mock JUCE backend is injected so the design runs outside a
plug-in host. Meters and spectrum are animated.</p>
`;
}

string comparePage ()
{
    return `
<!doctype html><meta charset=utf-8><title>compare</title>
<style>
  html, body { margin:0; height:100%; background:#0b0d10;
               font:11px system-ui,sans-serif; color:#8b93a3 }
  body       { display:flex }
  figure     { margin:0; flex:1; min-width:0; display:flex; flex-direction:column }
  figcaption { padding:6px 10px; letter-spacing:1.5px }
  iframe     { border:0; width:100%; flex:1; background:#16181d }
  .a { color:#ff5c8a } .b { color:#43d9c4 }
</style>
<figure><figcaption class="a">ORIGINAL (from the rar)</figcaption>
  <iframe src="/original"></iframe></figure>
<figure><figcaption class="b">CURRENT (FrostEngine)</figcaption>
  <iframe src="/current"></iframe></figure>
`;
}

/// Routes for one UI tree; `which` is a parameter, so the lambdas below each
/// get their own copy.
void addEditorRoutes (URLRouter router, string which)
{
    // "/<which>/" with a trailing slash also works, so relative asset URLs in
    // the document resolve inside the same directory.
    router.get ("/" ~ which, (HTTPServerRequest req, HTTPServerResponse res) {
        res.writeBody (previewDocument (which), "text/html; charset=utf-8");
    });
    router.get ("/" ~ which ~ "/", (HTTPServerRequest req, HTTPServerResponse res) {
        res.writeBody (previewDocument (which), "text/html; charset=utf-8");
    });
    router.get ("/raw/" ~ which, (HTTPServerRequest req, HTTPServerResponse res) {
        res.writeBody (readText (buildPath (rootFor (which), "index.html")),
                       "text/html; charset=utf-8");
    });
}

// ---------------------------------------------------------------------------
void setupRoutes ()
{
    auto router = new URLRouter;

    router.get ("/", (HTTPServerRequest req, HTTPServerResponse res) {
        res.writeBody (landingPage (), "text/html; charset=utf-8");
    });

    router.get ("/compare", (HTTPServerRequest req, HTTPServerResponse res) {
        res.writeBody (comparePage (), "text/html; charset=utf-8");
    });

    // The two editor pages.  A helper function is used so each closure captures
    // its OWN copy of `which` — a foreach loop reuses one variable, which would
    // make every route serve the last tree.
    foreach (which; [ "original", "current" ])
        addEditorRoutes (router, which);

    // assets: identical in both trees (verified by md5), so one route each
    router.get ("/edix.otf", (HTTPServerRequest req, HTTPServerResponse res) {
        res.writeBody (cast(ubyte[]) read (buildPath (currentRoot (), "edix.otf")),
                       "font/otf");
    });
    router.get ("/eonun_logo.png", (HTTPServerRequest req, HTTPServerResponse res) {
        res.writeBody (cast(ubyte[]) read (buildPath (currentRoot (), "eonun_logo.png")),
                       "image/png");
    });

    router.get ("/healthz", (HTTPServerRequest req, HTTPServerResponse res) {
        res.writeBody ("ok " ~ originalRoot () ~ " | " ~ currentRoot (), "text/plain");
    });

    // 0.0.0.0: reachable from the LAN (e.g. http://192.168.1.11:8888/)
    listenHTTP ("0.0.0.0:8888", router);
    logInfo ("Overteon preview on http://0.0.0.0:8888");
    logInfo ("  original: %s", originalRoot ());
    logInfo ("  current : %s", currentRoot ());
}

shared static this ()
{
    setupRoutes ();
}

// ===========================================================================
//  Overteon / OnyxTEON preview server (vibe.d)
//
//  Serves the editor UIs over HTTP so they can be rendered by a real browser
//  engine (Gecko/Firefox, Blink/Chromium, WebKit) instead of a plug-in host.
//
//    /                landing page with links
//    /original        the ORIGINAL HTML+CSS, extracted from ~/SupersawOmega.rar
//                     (the pristine design, no modifications)
//    /current         the current UI (ONYX|TEON; same HTML, only the wordmark
//                     differs — the glass is plain CSS backdrop-filter, as in
//                     the original, because the blur that matters lives in the
//                     Qt front end and the page is the reference)
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

  // JUCE: convertFrom0to1 uses pow(norm, 1/skew).
  //
  // A CHOICE parameter carries no lo/hi/skew - C() only stores its choices
  // and default - so the float formula produced NaN for every combo box and the page
  // fell back to its own zero.  That is why driveMode arrived as Classic (so
  // the sidechain section drew itself inactive), and why every EQ type and
  // range was wrong while the mock's table said otherwise.  A combo box's
  // value is its INDEX, which is also what ComboBoxState in JUCE expects.
  const isChoice = (p) => p.kind === "c";
  const choiceCount = (p) => p.choices.length;

  const valueOfParam = (p) => isChoice (p)
      ? Math.max (0, Math.min (choiceCount (p) - 1, Math.round (p.norm * (choiceCount (p) - 1))))
      : Math.pow(p.norm, 1 / p.skew) * (p.hi - p.lo) + p.lo;

  const propsOf = (p) => isChoice (p)
      ? ({ eventType: "propertiesChanged",
           start: 0, end: choiceCount (p) - 1, skew: 1,
           name: p.id, label: p.id,
           numSteps: choiceCount (p) - 1,
           interval: 0, parameterIndex: 0 })
      : ({ eventType: "propertiesChanged",
           start: p.lo, end: p.hi, skew: p.skew,
           name: p.id, label: p.id, numSteps: 100,
           interval: 0, parameterIndex: 0 });

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
        emit(eventId, { eventType: "valueChanged", value: valueOfParam(p) });
      } else if (payload.eventType === "valueChanged") {
        if (isChoice (p)) {
          // a combo box sends its index
          p.norm = Math.max (0, Math.min (1, payload.value / (choiceCount (p) - 1)));
        } else {
          // a slider sends the SCALED value (SliderState.setNormalisedValue)
          const lin = (payload.value - p.lo) / (p.hi - p.lo);
          p.norm = Math.pow(Math.min(1, Math.max(0, lin)), p.skew);
        }
        emit(eventId, { eventType: "valueChanged", value: valueOfParam(p) });
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

  // The meters and spectrum come from the SAME signal the Qt preview
  // generates (ui/qt/OvMockSource.h): a 60 Hz square wave whose amplitude is
  // swept by a 0.25 Hz LFO, at 48 kHz, analysed the same way — peak of the
  // last block for the bars, Hann-windowed 2048-point FFT for the spectrum.
  //
  // This replaced an invented formula.  While the two front ends drew
  // different data, every comparison between them was meaningless.
  //
  // ?freeze=1 pins the clock to 1.000 s and ?t=SECONDS pins it anywhere, so a
  // Qt capture taken with ONYXTEON_T can be compared against the same instant.
  const FROZEN  = /[?&]freeze=1/.test(location.search);
  const mT      = /[?&]t=([0-9.]+)/.exec(location.search);

  // ?pq1Freq=595&pq1Gain=-7.2 sets parameters by name, the same way the Qt
  // preview's --set does.  Without this a comparison could only ever run the
  // defaults, and the defaults are all 0 dB — a flat curve tests nothing.
  (function applyUrlParams () {
    const qs = location.search.replace(/^[?]/, "").split("&");
    for (const kv of qs) {
      const i = kv.indexOf("=");
      if (i < 0) continue;
      const k = decodeURIComponent(kv.slice(0, i));
      const v = parseFloat(decodeURIComponent(kv.slice(i + 1)));
      const p = P[k];
      if (!p || !isFinite(v)) continue;
      p.target = p.norm = (p.kind === "c")
          ? Math.max(0, Math.min(1, Math.round(v) / (p.choices.length - 1)))
          : Math.pow(Math.max(0, Math.min(1, (v - p.lo) / (p.hi - p.lo))), p.skew);
    }
  })();
  const PINNED  = FROZEN ? 1.0 : (mT ? parseFloat(mT[1]) : null);

  const SR = 48000, BLOCK = 1024, FFTN = 2048;
  const ring = new Float32Array(FFTN);
  let ringPos = 0, phase = 0, lfoPhase = 0, generatedUntil = -1;

  function fft (re, im) {
    const n = re.length;
    for (let i = 1, j = 0; i < n; i++) {
      let bit = n >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t;
                   t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (let len = 2; len <= n; len <<= 1) {
      const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
      for (let i = 0; i < n; i += len) {
        let cr = 1, ci = 0;
        for (let k = 0; k < len / 2; k++) {
          const ur = re[i + k], ui = im[i + k];
          const vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
          const vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
          re[i + k] = ur + vr; im[i + k] = ui + vi;
          re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
          const ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
        }
      }
    }
  }

  function renderTo (t) {
    // First call, or the clock went backwards (?t= / ?freeze=1 changes):
    // fill the whole ring.  Returning early on the first call left every
    // sample at zero (-120 dB), and with the clock pinned it never advanced,
    // so the bars stayed empty for the entire comparison run.
    if (generatedUntil < 0 || t < generatedUntil) {
      ring.fill(0); ringPos = 0; phase = 0; lfoPhase = 0;
      generatedUntil = t - FFTN / SR;
    }
    if (t <= generatedUntil) return;
    const dt = Math.min(t - generatedUntil, 0.25);
    generatedUntil = t;
    const n = Math.min(Math.floor(dt * SR), FFTN);
    for (let i = 0; i < n; i++) {
      phase += 60 / SR; if (phase >= 1) phase -= 1;
      lfoPhase += 0.25 / SR; if (lfoPhase >= 1) lfoPhase -= 1;
      const lfo = 0.5 + 0.5 * Math.sin(2 * Math.PI * lfoPhase);
      const amp = Math.pow(10, (-42 + 36 * lfo) / 20);
      ring[ringPos] = amp * (phase < 0.5 ? 1 : -1);
      ringPos = (ringPos + 1) % FFTN;
    }
  }

  function peakDb () {
    let pk = 0;
    for (let i = 0; i < BLOCK; i++)
      pk = Math.max(pk, Math.abs(ring[(ringPos - BLOCK + i + FFTN * 2) % FFTN]));
    return 20 * Math.log10(Math.max(pk, 1e-6));
  }

  function spectrumBins () {
    const re = new Float32Array(FFTN), im = new Float32Array(FFTN);
    for (let i = 0; i < FFTN; i++) {
      const w = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (FFTN - 1));
      re[i] = ring[(ringPos + i) % FFTN] * w;
    }
    fft(re, im);
    const out = [];
    for (let i = 0; i < 128; i++) {
      const fr = 20 * Math.pow(1000, i / 127);
      const bin = Math.max(0, Math.min(FFTN / 2, Math.floor(fr / SR * FFTN)));
      const mag = 2 * Math.hypot(re[bin], im[bin]) / FFTN;
      const db = 20 * Math.log10(Math.max(mag, 1e-7));
      out.push(Math.max(0, Math.min(1, (db + 85) / 85)));
    }
    return out;
  }

  // ?debug=1: report the state the CSS actually sees.  Without this, "the
  // glow looks weaker than the Qt build" cannot be told apart from "the glow
  // is switched off" — the pit is then just blur plus the .018 veil.
  const DEBUG = /[?&]debug=1/.test(location.search);
  function reportDebug () {
    const el = document.querySelector("#secPQ .controlpit");
    if (!el) return;
    const cs = getComputedStyle(el, "::before");
    const out = {
      light: el.style.getPropertyValue("--light"),
      beforeOpacity: cs.opacity,
      beforeBackground: cs.backgroundImage.slice(0, 60),
      clip: cs.clipPath || cs.webkitClipPath,
    };
    el.setAttribute("data-probe", JSON.stringify(out));
  }

  const t0 = performance.now();
  setInterval(() => {
    const t = (PINNED !== null) ? PINNED : (performance.now() - t0) / 1000;
    renderTo(t);
    const pk  = peakDb();
    const lfo = 0.5 + 0.5 * Math.sin(2 * Math.PI * 0.25 * (t % 4));
    emit("meter", { il: pk, ir: pk, ol: pk, or: pk, hm: -40 + 40 * lfo });
    emit("spectrum", spectrumBins());
    if (DEBUG) reportDebug();

    // ease towards the target so UI changes never jump
    for (const id in P) {
      const p = P[id];
      if (Math.abs(p.norm - p.target) > 1e-4) {
        p.norm += (p.target - p.norm) * 0.2;
        emit("__juce__slider" + id,
             { eventType: "valueChanged", value: valueOfParam(p) });
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
  <li><a href="/current">/current</a> — the same page with the ONYX|TEON
      wordmark <code>(identical rendering)</code></li>
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
<figure><figcaption class="b">CURRENT (ONYX|TEON)</figcaption>
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

    // The page posts its own geometry here when ?diag=1, so the state can be
    // read from the server log of the same browser that took the screenshot.
    router.post ("/diag", (HTTPServerRequest req, HTTPServerResponse res) {
        import std.file : append;
        const body = req.bodyReader.readAllUTF8 ();
        append ("/tmp/overteon-diag.log", body ~ "\n");
        res.writeBody ("ok", "text/plain");
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

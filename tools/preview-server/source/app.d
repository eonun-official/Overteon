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
import std.string : replace, indexOf, split, endsWith;
import std.path : buildPath;
import std.file : read, readText, exists, write, mkdirRecurse, dirEntries, SpanMode;
import std.path : baseName;
import std.process : environment;
import std.uni : isAlphaNum, toLower;
import std.conv : to;
import std.datetime.systime : Clock;

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

  // ---------------------------------------------------------------------------
  //  Remembering what the user changed
  // ---------------------------------------------------------------------------
  // Without this, every change was lost on reload: the page always started from
  // the defaults, so a state worth looking at had to be re-typed — or, worse,
  // encoded into the URL by hand — before anything could be compared.  The
  // server now holds the state it was given and restores it on the next load.
  //
  // Three sources, most specific first:
  //
  //   * the URL (?sc1Freq=115&...), so a link reproduces an exact state and a
  //     comparison run is reproducible from the address bar;
  //   * localStorage, which is what "holds its parameters when changed" means —
  //     a reload, or a new tab on the same page, comes back to the same values;
  //   * the defaults.
  //
  // Keyed by PATHNAME because /original and /current are two different front
  // ends served from one origin, and they must not overwrite each other's state.
  const STORE_KEY = "overteon.params:" + location.pathname;

  // Apply id -> scaled value.  The ONE path used by ?id=value, ?preset=, the
  // preset menu and an upload, so a state cannot arrive by four routes and
  // behave in four ways.
  function applyValues (vals, markAsUrl) {
    const touched = [];
    for (const id in vals) {
      const p = P[id];
      if (!p) continue;
      const v = vals[id];
      if (!isFinite (v)) continue;
      p.target = p.norm = (p.kind === "c")
          ? Math.max (0, Math.min (1, Math.round (v) / (p.choices.length - 1)))
          : Math.pow (Math.max (0, Math.min (1, (v - p.lo) / (p.hi - p.lo))), p.skew);
      if (markAsUrl) urlIds.add (id);
      touched.push (id);
    }
    return touched;
  }

  // Tell the PAGE what the values are now.
  //
  // This is the part that made an uploaded preset look like it had done
  // nothing: P[] is this mock's own table, but the UI draws from the page's
  // SliderState objects, and those only ever change when a valueChanged event
  // arrives from the backend.  Writing to P[] alone moved a number nobody was
  // looking at.
  //
  // Every parameter is driven through SliderState on this page — it never calls
  // getToggleState/getComboBoxState, it wraps all of them with S(id) — so the
  // identifier is "__juce__slider" + id even for a combo box or a switch, and
  // the payload is the SCALED value, which is what SliderState expects.
  function pushToPage (ids) {
    // Before the page's backend bridge exists there is nothing to push to — and
    // nothing to fix: the values are in P[] already, and the page asks for them
    // with requestInitialUpdate when each control is created.
    if (!window.__JUCE__ || !window.__JUCE__.backend) return 0;
    let n = 0;
    for (const id of ids) {
      const p = P[id];
      if (!p) continue;
      try {
        emit ("__juce__slider" + id,
              { eventType: "valueChanged", value: valueOfParam (p) });
        ++n;
      } catch (e) {
        console.warn ("[preset] cannot push", id, e);
      }
    }
    return n;
  }

  // Ask the server for a .hinj.  Synchronous: a preset has to be in place
  // before the next frame, or the picture jumps after the screenshot.
  function fetchPreset (folder, name) {
    try {
      const q = "/preset?folder=" + encodeURIComponent (folder || "Factory")
                      + "&name=" + encodeURIComponent (name);
      const xhr = new XMLHttpRequest ();
      xhr.open ("GET", q, false);
      xhr.send (null);
      if (xhr.status !== 200) return null;
      const vals = JSON.parse (xhr.responseText);
      return vals.error ? null : vals;
    } catch (e) {
      return null;
    }
  }

  // A loaded preset is the state: apply it, remember it, and tell the page
  // which name it is now showing (the menu button listens for "presetName").
  function loadPresetValues (vals, label) {
    const touched = applyValues (vals, true);
    const n = touched.length;
    if (n) {
      pushToPage (touched);
      try { localStorage.setItem (STORE_KEY, JSON.stringify (
          Object.fromEntries (Object.keys (P).map (k => [k, P[k].target])))); } catch (e) {}
      syncUrl ();
      if (label) emit ("presetName", label);
      console.log ("[preset]", label || "?", n, "of", Object.keys (vals).length, "applied");
    }
    return n;
  }

  function normOfValue (p, v) {
    return (p.kind === "c")
        ? Math.max (0, Math.min (1, Math.round (v) / (choiceCount (p) - 1)))
        : Math.pow (Math.max (0, Math.min (1, (v - p.lo) / (p.hi - p.lo))), p.skew);
  }

  function ensureChoiceCount (p) { return p.choices.length; }

  function loadStored () {
    try {
      const raw = localStorage.getItem (STORE_KEY);
      if (!raw) return {};
      const o = JSON.parse (raw);
      return (o && typeof o === "object") ? o : {};
    } catch (e) {
      return {};                      // private mode, quota, corrupt JSON
    }
  }

  // Write as VALUES, not normalised numbers: the value is what the page and the
  // Qt preview's --set both speak, so a stored file can be read by a human and
  // pasted into a URL.
  let saveTimer = null;
  function saveStored () {
    if (saveTimer !== null) return;
    saveTimer = setTimeout (function () {
      saveTimer = null;
      try {
        const o = {};
        for (const id in P) if (P[id].norm !== P[id].defNorm) o[id] = valueOfParam (P[id]);
        if (Object.keys (o).length === 0) localStorage.removeItem (STORE_KEY);
        else localStorage.setItem (STORE_KEY, JSON.stringify (o));
      } catch (e) { /* quota or no storage: not fatal */ }
    }, 200);
  }

  // Keep the address bar in step, so a state can be shared by copying the URL.
  // replaceState rather than pushState: this is not navigation and must not fill
  // the back history with one entry per knob turn.
  function syncUrl () {
    try {
      const qs = [];
      for (const id in P) if (P[id].norm !== P[id].defNorm)
        qs.push (encodeURIComponent (id) + "=" + valueOfParam (P[id]));
      if (FROZEN) qs.push ("freeze=1");
      if (mT) qs.push ("t=" + mT[1]);
      const mp = /[?&]preset=([^&]+)/.exec (location.search);
      if (mp) qs.push ("preset=" + mp[1]);
      const url = location.pathname + (qs.length ? "?" + qs.join("&") : "");
      history.replaceState (null, "", url);
    } catch (e) { /* some embeddings refuse; the page still works */ }
  }

  // ---- preset file I/O in the browser -------------------------------------
  // The plugin loads and saves via JUCE FileChooser; the page cannot, so both
  // ends of that go through the server: an upload in, a download out.  The
  // resulting file is a real APVTS <HINJ> document, so it drops straight into
  // the plugin afterwards — that is what makes this usable as a reference rig.
  let hinjInput = null;

  function pickHinjFile() {
    if (!hinjInput) {
      hinjInput = document.createElement("input");
      hinjInput.type = "file";
      hinjInput.accept = ".hinj,application/xml,text/xml";
      hinjInput.style.display = "none";
      // Keep it in the document: a detached input is not reliably clickable in
      // every browser, and the change event bubbles nowhere without a parent.
      document.body.appendChild(hinjInput);
      hinjInput.addEventListener("change", () => {
        const file = hinjInput.files && hinjInput.files[0];
        if (!file) return;
        const reader = new FileReader();
        reader.onload = () => {
          uploadHinj(String(reader.result), file.name);
          // Clear it only after the bytes are in hand, so picking the SAME file
          // twice still fires a change event — a silent no-op the second time
          // is exactly the kind of "nothing happened" this rig must not have.
          hinjInput.value = "";
        };
        reader.onerror = () => {
          hinjInput.value = "";
          presetError("READ FAILED");
        };
        reader.readAsText(file);
      });
    }
    hinjInput.click();
  }

  // A failed load must say so.  The console is not visible to whoever is
  // staring at the page, and "nothing happened" is indistinguishable from a
  // broken button.
  function presetError (msg) {
    const btn = document.getElementById("bPresets");
    if (!btn) { console.warn("[preset]", msg); return; }
    const was = btn.textContent;
    btn.textContent = msg;
    btn.style.color = "#ff5a5a";
    setTimeout(() => { btn.textContent = was; btn.style.color = ""; }, 2500);
  }

  function uploadHinj(text, fileName) {
    let base = (fileName || "upload").replace(/\.[^.]*$/, "").replace(/[^\w .+-]/g, "_");
    try {
      const xhr = new XMLHttpRequest();
      xhr.open("POST", "/upload?name=" + encodeURIComponent(base), false);
      xhr.setRequestHeader("Content-Type", "text/plain; charset=utf-8");
      xhr.send(text);
      const info = JSON.parse(xhr.responseText);
      if (info.error) { presetError("NOT A .HINJ"); console.warn("[upload] rejected:", info.error); return; }
      const vals = fetchPreset(info.folder, info.name);
      if (!vals) { presetError("READ BACK FAILED"); return; }
      const n = loadPresetValues(vals, info.name);
      if (!n) presetError("NO MATCHING PARAMS");
    } catch (e) {
      presetError("UPLOAD FAILED");
      console.warn("[upload] failed:", e);
    }
  }

  function downloadHinj(name) {
    const esc = (t) => String(t).replace(/&/g, "&amp;").replace(/</g, "&lt;")
                                 .replace(/>/g, "&gt;").replace(/"/g, "&quot;");
    let xml = '<?xml version="1.0" encoding="UTF-8"?>\n<HINJ>\n';
    for (const id of Object.keys(P).sort())
      xml += '  <PARAM id="' + esc(id) + '" value="' + esc(valueOfParam(P[id])) + '"/>\n';
    xml += '</HINJ>\n';

    const url = URL.createObjectURL(new Blob([xml], { type: "application/xml" }));
    const a = document.createElement("a");
    a.href = url;
    a.download = name.replace(/[^\w .+-]/g, "_") + ".hinj";
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  function handleInvoke(payload) {
    const { name, params, resultId } = payload;
    let result = true;

    if (name === "applyInit") {
      for (const id in P) P[id].norm = P[id].target = P[id].defNorm;
      pushToPage (Object.keys (P));
      // INIT means "back to defaults", so the remembered state has to go too —
      // leaving it would restore the old values on the next reload and make
      // INIT look like it had done nothing.
      try { localStorage.removeItem (STORE_KEY); } catch (e) {}
      syncUrl();
    } else if (name === "listPresets") {
      // The real plugin reads its preset directories; so does this.  Whatever
      // is on disk is what the menu shows, uploads included.
      try {
        const xhr = new XMLHttpRequest();
        xhr.open("GET", "/presets", false);
        xhr.send(null);
        result = xhr.status === 200 ? xhr.responseText : JSON.stringify(presets);
      } catch (e) { result = JSON.stringify(presets); }

    } else if (name === "loadPreset") {
      // "Load Preset" in the menu.  In the plugin this opens a native chooser;
      // a browser has no such thing, so the picker is an <input type=file>.
      // The element is created once and reused — a fresh one per click leaks a
      // DOM node every time — and click() happens inside the same gesture that
      // reached us, which is what lets the browser open the chooser at all.
      pickHinjFile();

    } else if (name === "loadPresetAt") {
      // A click in the preset menu: folder + name, straight off the server.
      const vals = fetchPreset(params[0], params[1]);
      if (vals) { if (!loadPresetValues(vals, params[1])) presetError("NO MATCHING PARAMS"); }
      else { presetError("PRESET MISSING"); console.warn("[preview] preset not found:", params[0], params[1]); }

    } else if (name === "savePreset") {
      downloadHinj(params[0] || "preset");

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
        // The user changed something: hold it, and reflect it in the URL.
        // p.target follows too, otherwise the easing loop below would drag the
        // value straight back to where the target still pointed.
        p.target = p.norm;
        saveStored();
        syncUrl();
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
  const urlIds = new Set();
  (function applyUrlParams () {
    const qs = location.search.replace(/^[?]/, "").split("&");
    for (const kv of qs) {
      const i = kv.indexOf("=");
      if (i < 0) continue;
      const k = decodeURIComponent(kv.slice(0, i));
      const v = parseFloat(decodeURIComponent(kv.slice(i + 1)));
      const one = {}; one[k] = v;
      applyValues (one, true);
    }
  })();

  // Restore what was last changed, unless the URL already said otherwise for
  // that parameter — a link must always win over whatever this browser happens
  // to remember, or a shared comparison URL would silently show something else.
  (function applyStored () {
    const stored = loadStored();
    for (const id in stored) {
      const p = P[id];
      if (!p) continue;                       // a parameter that no longer exists
      const v = parseFloat(stored[id]);
      if (!isFinite(v)) continue;
      if (urlIds.has (id)) continue;          // the URL said it explicitly
      p.target = p.norm = normOfValue (p, v);
    }
  })();

  // ?preset=<name> — a .hinj on the server, applied like ?id=value.
  //
  // Synchronous on purpose: the file has to be in place before the first frame,
  // or the page would flash the defaults and then jump.  It is a few kilobytes
  // off localhost, so the block is imperceptible, and getting it wrong would
  // make every screenshot of a preset unreliable.
  (function applyPresetParam () {
    const m = /[?&]preset=([^&]+)/.exec (location.search);
    if (!m) return;
    let spec = decodeURIComponent (m[1]);
    let folder = "Factory", name = spec;
    const slash = spec.indexOf ("/");
    if (slash > 0) { folder = spec.slice (0, slash); name = spec.slice (slash + 1); }
    const vals = fetchPreset (folder, name);
    if (!vals) { console.warn ("[preset]", spec, "not found"); return; }
    loadPresetValues (vals, name);
  })();

  // A link is a setting too: seed the store from whatever the URL asked for, so
  // a reload without the query string still comes back to that state and the
  // address bar can be simplified to the bare page.
  (function seedFromUrl () {
    let any = false;
    for (const id in P) if (P[id].norm !== P[id].defNorm) any = true;
    if (!any) return;
    saveStored();
    syncUrl();
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
//  Reading a .hinj preset.
//
//  The format is JUCE's APVTS state dump: a <PARAM> child per parameter, with
//  the id and the scaled value as attributes.
//
//      <HINJ><PARAM id="pq1Freq" value="248.53"/> ... </HINJ>
//
//  Parsed here rather than in the browser so the same file can be opened as a
//  URL: ?preset=<name> is enough to reproduce a state, and that is what makes a
//  reference comparison repeatable.  Only the name/value pairs are extracted —
//  anything else in the document is ignored.
// ---------------------------------------------------------------------------
string[string] parseHinj (string text)
{
    string[string] result;
    size_t i = 0;
    while (true)
    {
        const p = text.indexOf ("<PARAM", i);
        if (p < 0) break;
        const q = text.indexOf (">", p);
        if (q < 0) break;
        const element = text[p .. q];
        i = q + 1;

        string attrOf (string name)
        {
            size_t a = 0;
            while (true)
            {
                const k = element.indexOf (name, a);
                if (k < 0) return null;
                // whole-attribute match only
                if (k > 0 && (isAlphaNum (element[k-1]) || element[k-1] == '_'))
                { a = k + name.length; continue; }
                size_t v = k + name.length;
                while (v < element.length && (element[v] == ' ' || element[v] == '\t')) ++v;
                if (v >= element.length || element[v] != '=') { a = k + name.length; continue; }
                ++v;
                while (v < element.length && (element[v] == ' ' || element[v] == '\t')) ++v;
                if (v >= element.length) return null;
                const quote = element[v];
                if (quote != '"' && quote != '\'') return null;
                ++v;
                const e = element.indexOf (quote, v);
                if (e < 0) return null;
                return element[v .. e];
            }
        }

        const id  = attrOf ("id");
        const val = attrOf ("value");
        if (id.length && val.length) result[id] = val;
    }
    return result;
}

/// Where presets live: <root>/Factory for the ones in the repo, <root>/User for
/// whatever was uploaded.  The page's own menu is organised exactly this way
/// (listPresets returns {Factory:[...], User:[...]} and loadPresetAt takes the
/// folder as its first argument), so an uploaded file lands where the menu can
/// already point at it.
string presetRoot ()
{
    return envOr ("OVERTEON_PRESET_DIR", "/home/x_x/build/Onyx-z/OnyxTEON/presets");
}

/// <root>/<folder>/<file>, or null if the name is not one we serve.  Exactly
/// one level: no traversal, so this route cannot read arbitrary files.
string presetPath (string folder, string file)
{
    if (folder != "Factory" && folder != "User") return null;
    if (file.length == 0) return null;
    if (file.indexOf ("/") >= 0 || file.indexOf ("..") >= 0) return null;
    string f = file;
    if (! f.endsWith (".hinj")) f ~= ".hinj";
    return buildPath (presetRoot (), folder, f);
}

string[] listPresets (string folder)
{
    string[] names;
    try
        foreach (e; dirEntries (buildPath (presetRoot (), folder), SpanMode.shallow))
        {
            const bn = baseName (e.name);
            if (bn.endsWith (".hinj")) names ~= bn [0 .. $ - 5];
        }
    catch (Exception) {}          // the directory may not exist yet
    return names;
}

string valuesJson (string[string] vals)
{
    string json = "{";
    bool first = true;
    foreach (id, v; vals)
    {
        if (! first) json ~= ",";
        first = false;
        json ~= `"` ~ id ~ `":` ~ v;
    }
    return json ~ "}";
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

    // ---- presets ---------------------------------------------------------
    // GET /preset?folder=Factory&name=X  -> {"id":value,...}
    //
    // The page applies these through the same function as ?id=value, so a
    // preset and a query string cannot disagree about how a value is scaled.
    // folder defaults to Factory, and a bare name is searched in both.
    router.get ("/preset", (HTTPServerRequest req, HTTPServerResponse res) {
        res.contentType = "application/json";
        res.headers["Cache-Control"] = "no-store";

        string folder = req.query.get ("folder", "");
        const name = req.query.get ("name", "");

        string path = presetPath (folder, name);
        if (path is null && folder.length == 0)
        {
            // No folder given: try Factory, then User.
            foreach (f; [ "Factory", "User" ])
            {
                path = presetPath (f, name);
                if (path !is null && exists (path)) break;
                path = null;
            }
        }

        if (path is null)
        {
            res.writeBody (`{"error":"bad name"}`, "application/json");
            return;
        }
        if (! exists (path))
        {
            res.writeBody (`{"error":"not found","path":"` ~ path ~ `"}`, "application/json");
            return;
        }

        res.writeBody (valuesJson (parseHinj (readText (path))), "application/json");
    });

    // GET /presets -> {"Factory":[...],"User":[...]}   (this is what the page's
    // preset menu asks listPresets() for)
    router.get ("/presets", (HTTPServerRequest req, HTTPServerResponse res) {
        import std.algorithm : sort;
        import std.array : array;
        string json = "{";
        foreach (i, folder; [ "Factory", "User" ])
        {
            if (i) json ~= ",";
            json ~= `"` ~ folder ~ `":[`;
            auto names = listPresets (folder).sort.array;
            foreach (j, n; names)
            {
                if (j) json ~= ",";
                json ~= `"` ~ n ~ `"`;
            }
            json ~= "]";
        }
        res.writeBody (json ~ "}", "application/json");
    });

    // POST /upload  (body = a .hinj)  -> {"folder":"User","name":"...","count":N}
    router.post ("/upload", (HTTPServerRequest req, HTTPServerResponse res) {
        const body = req.bodyReader.readAllUTF8 ();
        res.contentType = "application/json";

        if (body.length == 0 || body.indexOf ("<PARAM") < 0)
        {
            res.writeBody (`{"error":"no <PARAM> element: this is not an APVTS .hinj"}`, "application/json");
            return;
        }

        const vals = parseHinj (body);
        if (vals.length == 0)
        {
            res.writeBody (`{"error":"parsed 0 parameters"}`, "application/json");
            return;
        }

        string name = req.query.get ("name", "");
        if (name.length == 0)
            name = "upload-" ~ to!string (Clock.currTime ().toUnixTime ());
        const path = presetPath ("User", name);
        if (path is null)
        {
            res.writeBody (`{"error":"bad name"}`, "application/json");
            return;
        }

        mkdirRecurse (buildPath (presetRoot (), "User"));
        write (path, body);

        res.writeBody (`{"folder":"User","name":"` ~ baseName (path) [0 .. $ - 5]
                       ~ `","count":` ~ to!string (vals.length) ~ `}`, "application/json");
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

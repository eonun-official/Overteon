// Schranz-mode probe: does a deep EQ cut ever produce gain > 0 dB anywhere?
// Mirrors the exact coefficient math in src/DSP.h (setPeak / setNotch / shelves)
// and the Schranz signal path: dry -> SC EQ -> jlimit(-8,8) -> out.
const fs = 48000;

// --- exact copy of DSP.h setPeak ---
function peak(f0, q, dB, fs) {
  const A = Math.pow(10, dB / 40);
  const w = 2 * Math.PI * Math.min(Math.max(f0, 10), fs * 0.45) / fs;
  const al = Math.sin(w) / (2 * Math.max(0.1, q));
  const cw = Math.cos(w);
  const a0 = 1 + al / A;
  return {
    b0: (1 + al * A) / a0, b1: (-2 * cw) / a0, b2: (1 - al * A) / a0,
    a1: (-2 * cw) / a0, a2: (1 - al / A) / a0,
  };
}

// |H(e^jw)| in dB
function magDb(c, f) {
  const w = 2 * Math.PI * f / fs;
  const z1r = Math.cos(w), z1i = -Math.sin(w);   // z^-1
  const z2r = Math.cos(2 * w), z2i = -Math.sin(2 * w);
  const nr = c.b0 + c.b1 * z1r + c.b2 * z2r;
  const ni = c.b1 * z1i + c.b2 * z2i;
  const dr = 1 + c.a1 * z1r + c.a2 * z2r;
  const di = c.a1 * z1i + c.a2 * z2i;
  const g = Math.hypot(nr, ni) / Math.hypot(dr, di);
  return 20 * Math.log10(g);
}

function scan(c, lo = 10, hi = 20000, n = 4000) {
  let best = -Infinity, atF = 0;
  for (let i = 0; i <= n; ++i) {
    const f = lo * Math.pow(hi / lo, i / n);
    const d = magDb(c, f);
    if (d > best) { best = d; atF = f; }
  }
  return { maxDb: best, at: atF };
}

// --- time domain: dry -> EQ -> hard clip ---
function runNoise(c, nSample, seed) {
  let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  let s = seed;
  const rnd = () => { s = (s * 1103515245 + 12345) & 0x7fffffff; return (s / 0x3fffffff) - 1; };
  let inP = 0, outP = 0, clipHits = 0;
  for (let i = 0; i < nSample; ++i) {
    const x = 0.5 * rnd();
    const y = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
    x2 = x1; x1 = x; y2 = y1; y1 = y;
    const cl = Math.max(-8, Math.min(8, y));
    if (cl !== y) ++clipHits;
    inP += x * x; outP += cl * cl;
  }
  return { rmsDb: 10 * Math.log10(outP / inP), clipHits };
}

// --- impulse: post-ringing energy at f0 ---
function runImpulse(c, f0, nSample) {
  let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  let re = 0, im = 0;
  const w = 2 * Math.PI * f0 / fs;
  for (let i = 0; i < nSample; ++i) {
    const x = i === 0 ? 1 : 0;
    const y = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
    x2 = x1; x1 = x; y2 = y1; y1 = y;
    re += y * Math.cos(w * i); im -= y * Math.sin(w * i);
  }
  return 20 * Math.log10(Math.hypot(re, im) / (nSample / 2) + 1e-30);
}

// sweep: how deep can we cut before the filter itself boosts anything?
console.log("=== 1. 滤波器自身是否有 boost（全频段扫描，20Hz-20kHz） ===");
for (const f0 of [40, 60, 100, 200]) {
  for (const q of [0.7, 2, 6]) {
    for (const dB of [-12, -30, -60, -90]) {
      const c = peak(f0, q, dB, fs);
      const r = scan(c);
      const flag = r.maxDb > 0.01 ? "  <<< BOOST" : "";
      console.log(`f0=${String(f0).padStart(4)}Hz Q=${q} cut=${String(dB).padStart(3)}dB -> max ${r.maxDb.toFixed(2)} dB @ ${Math.round(r.at)} Hz${flag}`);
    }
  }
}

console.log("\n=== 2. 白噪 + 硬削顶：输出 RMS 相对输入（应为负值=衰减） ===");
for (const f0 of [40, 60]) {
  for (const dB of [-30, -60, -90]) {
    const c = peak(f0, 2, dB, fs);
    const r = runNoise(c, 48000 * 3, 7);
    console.log(`f0=${f0}Hz cut=${dB}dB -> RMS ${r.rmsDb.toFixed(2)} dB, clip hits ${r.clipHits}`);
  }
}

console.log("\n=== 3. 脉冲响应：f0 处的振铃电平（削 -60 dB 时） ===");
for (const f0 of [40, 60, 100]) {
  for (const dB of [-12, -60]) {
    const c = peak(f0, 2, dB, fs);
    const lvl = runImpulse(c, f0, 2048);
    console.log(`f0=${f0}Hz cut=${dB}dB -> ringing @f0 = ${lvl.toFixed(1)} dBFS`);
  }
}

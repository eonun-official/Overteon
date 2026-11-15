// fit_check.js — 数值验证：48k/44.1k 下能否用「钳制 + 二元拟合」
// 逼近理想（极高采样率）响应，并检查 f0 连续拖动时是否出现突跳。
// 运行：node tools/fit_check.js
//
// 约定：全部使用与 src/DSP.h 相同的 RBJ 公式；
//   reference = 用 fs=20MHz 计算同一 f0（等价于模拟原型，翘曲 < 0.001 dB）
//   candidate = 用真实 fs，角点钳在 0.45*fs，拟合 (fc, Qc)

const F_LO = 20, F_HI = 20000;

// ---- RBJ 系数（返回 [b0,b1,b2,a1,a2]，归一化 a0=1）----
function coefs(type, fs, f0, q, gainDb) {
  const A = Math.pow(10, gainDb / 40);
  const w = 2 * Math.PI * Math.min(f0, fs * 0.4999) / fs;
  const cw = Math.cos(w), sw = Math.sin(w);
  const al = sw / (2 * Math.max(0.05, q));
  const sA = 2 * Math.sqrt(A) * al;

  switch (type) {
    case 0: { // bell / peaking
      const b0 = 1 + al * A, b1 = -2 * cw, b2 = 1 - al * A;
      const a0 = 1 + al / A, a1 = -2 * cw, a2 = 1 - al / A;
      return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0];
    }
    case 1: { // notch
      const b0 = 1, b1 = -2 * cw, b2 = 1;
      const a0 = 1 + al, a1 = -2 * cw, a2 = 1 - al;
      return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0];
    }
    case 2: { // low shelf
      const b0 = A * ((A + 1) - (A - 1) * cw + sA);
      const b1 = 2 * A * ((A - 1) - (A + 1) * cw);
      const b2 = A * ((A + 1) - (A - 1) * cw - sA);
      const a0 = (A + 1) + (A - 1) * cw + sA;
      const a1 = -2 * ((A - 1) + (A + 1) * cw);
      const a2 = (A + 1) + (A - 1) * cw - sA;
      return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0];
    }
    case 3: { // high shelf
      const b0 = A * ((A + 1) + (A - 1) * cw + sA);
      const b1 = -2 * A * ((A - 1) + (A + 1) * cw);
      const b2 = A * ((A + 1) + (A - 1) * cw - sA);
      const a0 = (A + 1) - (A - 1) * cw + sA;
      const a1 = 2 * ((A - 1) - (A + 1) * cw);
      const a2 = (A + 1) - (A - 1) * cw - sA;
      return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0];
    }
    case 4: case 5: { // low cut (highpass)
      const b0 = (1 + cw) / 2, b1 = -(1 + cw), b2 = (1 + cw) / 2;
      const a0 = 1 + al, a1 = -2 * cw, a2 = 1 - al;
      return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0];
    }
    case 6: case 7: { // high cut (lowpass)
      const b0 = (1 - cw) / 2, b1 = 1 - cw, b2 = (1 - cw) / 2;
      const a0 = 1 + al, a1 = -2 * cw, a2 = 1 - al;
      return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0];
    }
  }
  throw new Error("type " + type);
}

function magDb(c, fs, f) {
  const w = 2 * Math.PI * f / fs;
  const cw = Math.cos(w), sw = Math.sin(w), c2 = Math.cos(2 * w), s2 = Math.sin(2 * w);
  const nr = c[0] + c[1] * cw + c[2] * c2, ni = -(c[1] * sw + c[2] * s2);
  const dr = 1 + c[3] * cw + c[4] * c2, di = -(c[3] * sw + c[4] * s2);
  const num = Math.hypot(nr, ni), den = Math.hypot(dr, di);
  return 20 * Math.log10(Math.max(1e-12, num / Math.max(1e-12, den)));
}

// 24 dB 档 = 两级串联；两级都按同一组 (fc,Qc) 拟合，第二级 Q 固定 0.707
function designedMagDb(type, fs, fc, qc, gainDb) {
  const two = (type === 5 || type === 7);
  let m = magDb(coefs(type, fs, fc, qc, gainDb), fs, FCUR);
  if (two) m += magDb(coefs(type, fs, fc, 0.707, 0), fs, FCUR);
  return m;
}

let FCUR = 0;
function candidateCurve(type, fs, fc, qc, g, gainFitted) {
  const out = [];
  for (let i = 0; i <= 64; ++i) {
    const f = F_LO * Math.pow(F_HI / F_LO, i / 64);
    FCUR = f;
    out.push(designedMagDb(type, fs, fc, qc, gainFitted ? gainFitted : g));
  }
  return out;
}

function referenceCurve(type, f0, q, g) {
  const FSREF = 20e6;
  const out = [];
  for (let i = 0; i <= 64; ++i) {
    const f = F_LO * Math.pow(F_HI / F_LO, i / 64);
    FCUR = f;
    out.push(designedMagDb(type, FSREF, f0, q, g));
  }
  return out;
}

function maxErr(type, fs, f0, q, g, fc, qc, gFit) {
  const ref = referenceCurve(type, f0, q, g);
  const cand = candidateCurve(type, fs, fc, qc, g, gFit);
  let e = 0;
  for (let i = 0; i < ref.length; ++i) e = Math.max(e, Math.abs(cand[i] - ref[i]));
  return e;
}

// 坐标下降：fc、qc（可选 gain 缩放）
function fit(type, fs, f0, q, g, allowGain) {
  const lim = 0.45 * fs;
  let fc = Math.min(f0, lim), qc = q, gF = g, best = Infinity;
  const evalAt = (a, b, c) => maxErr(type, fs, f0, q, g, Math.min(a, lim), b, c);

  for (let pass = 0; pass < 5; ++pass) {
    for (const step of [2000, 800, 300, 100, 40, 15]) {
      for (let k = 0; k < 40; ++k) {
        let improved = false;
        for (const [di, dj] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
          const a = fc + (di ? di * step : 0);
          const b = Math.max(0.2, Math.min(12, qc * Math.exp(dj * 0.06)));
          const c = allowGain ? gF + (allowGain === 2 ? (di ? 0 : 0) : 0) : gF;
          const v = evalAt(a, b, c);
          if (v < best - 1e-9) { best = v; fc = Math.min(a, lim); qc = b; gF = c; improved = true; }
        }
        if (!improved) break;
      }
    }
  }
  return { fc, qc, gF, err: best };
}

const TYPES = [
  { id: 0, name: "Bell" }, { id: 1, name: "Notch" },
  { id: 2, name: "LowShelf" }, { id: 3, name: "HighShelf" },
  { id: 4, name: "LowCut12" }, { id: 5, name: "LowCut24" },
  { id: 6, name: "HighCut12" }, { id: 7, name: "HighCut24" },
];

const lines = [];
lines.push("==== 最大误差（20 Hz–20 kHz，dB）====");
lines.push(["类型", "fs", "f0", "Q", "gain", "fit fc", "fit Q", "maxErr"].join("\t"));

const worst = {};
for (const fs of [44100, 48000]) {
  for (const t of TYPES) {
    const hasGain = (t.id === 0 || t.id === 2 || t.id === 3);
    const hasQ = !(t.id === 5 || t.id === 7);
    for (const f0 of [22000, 25000, 30000, 35000]) {
      if (f0 >= 0.45 * fs) { /* still test */ }
      for (const q of (hasQ ? [0.7, 2, 5] : [0.707])) {
        for (const g of (hasGain ? [-6, 6, 12] : [0])) {
          const r = fit(t.id, fs, f0, q, g, 0);
          const key = `${fs}/${t.name}`;
          worst[key] = Math.max(worst[key] || 0, r.err);
          lines.push([t.name, fs, f0, q, g, Math.round(r.fc), r.qc.toFixed(2), r.err.toFixed(3)].join("\t"));
        }
      }
    }
  }
}

lines.push("");
lines.push("==== 各类型 / 各采样率 最差误差 ====");
for (const k of Object.keys(worst).sort()) lines.push(`${k}\t${worst[k].toFixed(3)} dB`);

// ---- 连续性：f0 从 15k 连续拖到 35k，观察 20k 处响应是否突跳 ----
lines.push("");
lines.push("==== 连续性扫描（48k，HighCut12，Q=0.707，观察 20 kHz 处响应）====");
{
  const fs = 48000, type = 6, q = 0.707, lim = 0.45 * fs;
  let prev = null, maxJump = 0, jumpAt = 0;
  const series = [];
  for (let f0 = 15000; f0 <= 35000; f0 += 100) {
    const fc = Math.min(f0, lim);
    FCUR = 20000;
    const v = designedMagDb(type, fs, fc, q, 0);
    if (prev !== null) {
      const d = Math.abs(v - prev);
      if (d > maxJump) { maxJump = d; jumpAt = f0; }
    }
    prev = v;
    if (f0 % 1000 === 0) series.push(`${f0}:${v.toFixed(2)}`);
  }
  lines.push(`20k 处响应序列(1k 步进): ${series.join("  ")}`);
  lines.push(`相邻 100Hz 最大跳变: ${maxJump.toFixed(4)} dB @ f0=${jumpAt}`);
  lines.push(maxJump < 0.05 ? "结论：连续，无突跳" : "结论：存在突跳，需要平滑设计函数");
}

console.log(lines.join("\n"));

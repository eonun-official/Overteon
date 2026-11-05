// Harmonic-mode probe: why a deep SC-EQ cut turns into a bell at that band.
// Chain under test (same as FxProcessor, Harmonic):
//   y = E(x)                  forward sidechain EQ (deep cut at f0)
//   r = tanh(k*y) - k*y       harmonic residue (only the nonlinear part)
//   out = Einv(r)             de-emphasis = inverse EQ  (BOOST at f0)
const fs = 48000, f0 = 120, Q = 2, k = 2.0;

function peak(gainDb, sign) {           // sign = +1 forward, -1 inverse
  const dB = gainDb * sign;
  const A = Math.pow(10, dB / 40);
  const w = 2 * Math.PI * Math.min(Math.max(f0, 10), fs * 0.45) / fs;
  const al = Math.sin(w) / (2 * Math.max(0.1, Q));
  const cw = Math.cos(w);
  const a0 = 1 + al / A;
  return { b0: (1 + al * A) / a0, b1: (-2 * cw) / a0, b2: (1 - al * A) / a0,
           a1: (-2 * cw) / a0, a2: (1 - al / A) / a0 };
}
function mk(c) { let x1=0,x2=0,y1=0,y2=0;
  return (x) => { const y = c.b0*x + c.b1*x1 + c.b2*x2 - c.a1*y1 - c.a2*y2;
    x2=x1; x1=x; y2=y1; y1=y; return y; };
}
function goertzel(buf, f) {
  const w = 2*Math.PI*f/fs; let re=0, im=0;
  for (let i=0;i<buf.length;i++){ re += buf[i]*Math.cos(w*i); im -= buf[i]*Math.sin(w*i); }
  return 20*Math.log10(Math.hypot(re,im)/(buf.length/2)+1e-30);
}
function rmsDb(buf){ let p=0; for(const v of buf) p+=v*v; return 10*Math.log10(p/buf.length+1e-30); }

const N = fs * 3;
let s = 12345; const rnd = () => { s=(s*1103515245+12345)&0x7fffffff; return (s/0x3fffffff)-1; };

console.log("Harmonic residue, f0 = 120 Hz, Q = 2, drive k = 2");
console.log("cut(dB) |  residue@f0  |  output@f0  | out RMS");
for (const cut of [0, -12, -30, -45, -60, -75, -90]) {
  const fwd = mk(peak(cut, +1)), inv = mk(peak(cut, -1));
  const inBuf = [], resBuf = [], outBuf = [];
  for (let i = 0; i < N; ++i) {
    const x = 0.3 * rnd();
    const y = fwd(x);
    const r = Math.tanh(k * y) - k * y;     // harmonic residue only
    const o = inv(r);
    if (i > N/2) { inBuf.push(x); resBuf.push(r); outBuf.push(o); }
  }
  console.log(`${String(cut).padStart(7)} | ${goertzel(resBuf,f0).toFixed(1).padStart(11)} dB | ${goertzel(outBuf,f0).toFixed(1).padStart(9)} dB | ${rmsDb(outBuf).toFixed(2)} dB`);
}

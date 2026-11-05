// Verify the "splitter" claim + the "grain on boost" observation.
// Harmonic path:  y = E(x); r = tanh(k*y) - k*y; h = Einv(r); out = x + h
// E and Einv are exact inverses at f0, so only the nonlinear part survives.
const fs = 48000, f0 = 120, Q = 1.0, k = 2.5;

function peak(gainDb) {
  const A = Math.pow(10, gainDb / 40);
  const w = 2 * Math.PI * Math.min(Math.max(f0, 10), fs * 0.45) / fs;
  const al = Math.sin(w) / (2 * Math.max(0.1, Q));
  const cw = Math.cos(w), a0 = 1 + al / A;
  return { b0: (1 + al * A) / a0, b1: (-2 * cw) / a0, b2: (1 - al * A) / a0,
           a1: (-2 * cw) / a0, a2: (1 - al / A) / a0 };
}
const mk = (c) => { let x1=0,x2=0,y1=0,y2=0;
  return (x)=>{ const y=c.b0*x+c.b1*x1+c.b2*x2-c.a1*y1-c.a2*y2;
    x2=x1;x1=x;y2=y1;y1=y;return y; }; };

function goertzel(buf, f) {
  const w = 2*Math.PI*f/fs; let re=0,im=0;
  for(let i=0;i<buf.length;i++){ re+=buf[i]*Math.cos(w*i); im-=buf[i]*Math.sin(w*i); }
  return 20*Math.log10(Math.hypot(re,im)/(buf.length/2)+1e-30);
}
const rmsDb = (b)=>{ let p=0; for(const v of b) p+=v*v; const r=Math.sqrt(p/b.length); return 20*Math.log10(r+1e-30); };

const N = fs*3;
let s = 987654; const rnd=()=>{s=(s*1103515245+12345)&0x7fffffff; return (s/0x3fffffff)-1;};
// 120 Hz tone + a little noise: the tone makes f0 and its harmonics measurable,
// the noise keeps the broadband IMD floor realistic.
const src = (i)=>{ const t = Math.sin(2*Math.PI*f0*i/fs)*0.25 + 0.08*rnd(); return t; };

console.log(`f0=${f0}Hz Q=${Q} drive k=${k}   input = 120Hz tone 0.25 + noise 0.08`);
console.log("gain |  out@f0 |  out@2f0 | h-rms | out-rms | dry@2f0 baseline");
const baseDry = [];
{ for(let i=0;i<N;i++){ if(i>N/2) baseDry.push(src(i)); } }

for (const g0Db of [0, 6, 12, 24, -6, -24, -60]) {
  const E = mk(peak(g0Db)), Einv = mk(peak(-g0Db));
  const out=[], hb=[];
  for (let i=0;i<N;i++){
    const x = src(i);
    const y = E(x);
    const r = Math.tanh(k*y) - k*y;      // linear part removed => harmonics only
    const h = Einv(r);
    if (i>N/2){ out.push(x+h); hb.push(h); }
  }
  console.log(
    `${String(g0Db).padStart(4)} | ${goertzel(out,f0).toFixed(1).padStart(7)} | `+
    `${goertzel(out,2*f0).toFixed(1).padStart(8)} | ${rmsDb(hb).toFixed(1).padStart(5)} | `+
    `${rmsDb(out).toFixed(2).padStart(7)} | ${goertzel(baseDry,2*f0).toFixed(1)}`);
}

// What exactly IS the sound from an extreme SC-EQ cut in Harmonic mode?
// Separates the three ingredients:
//   (1) nonlinearity  (2) broadband excitation  (3) de-emphasis boost
const fs = 48000, f0 = 120, Q = 2, k = 2.5, CUT = -60;

function peak(gainDb) {
  const A = Math.pow(10, gainDb / 40);
  const w = 2 * Math.PI * Math.min(Math.max(f0, 10), fs * 0.45) / fs;
  const al = Math.sin(w) / (2 * Math.max(0.1, Q));
  const cw = Math.cos(w), a0 = 1 + al / A;
  return { b0:(1+al*A)/a0, b1:(-2*cw)/a0, b2:(1-al*A)/a0, a1:(-2*cw)/a0, a2:(1-al/A)/a0 };
}
const mk = (c)=>{ let x1=0,x2=0,y1=0,y2=0;
  return (x)=>{ const y=c.b0*x+c.b1*x1+c.b2*x2-c.a1*y1-c.a2*y2;
    x2=x1;x1=x;y2=y1;y1=y;return y; }; };

function goertzel(buf, f) {
  const w = 2*Math.PI*f/fs; let re=0,im=0;
  for(let i=0;i<buf.length;i++){ re+=buf[i]*Math.cos(w*i); im-=buf[i]*Math.sin(w*i); }
  return 20*Math.log10(Math.hypot(re,im)/(buf.length/2)+1e-30);
}

const N = fs*2;
let s = 424242; const rnd=()=>{s=(s*1103515245+12345)&0x7fffffff; return (s/0x3fffffff)-1;};

const cases = {
  "单一 1kHz 正弦（窄带激励）": (i) => Math.sin(2*Math.PI*1000*i/fs)*0.3,
  "白噪声（宽带激励）":        (i) => 0.15*rnd(),
};
const variants = {
  "完整链路 (饱和 + 逆EQ)": (y) => { const r=Math.tanh(k*y)-k*y; return r; },
  "去掉非线性 (线性直通)":   (y) => 0,
};

console.log(`f0=${f0}Hz Q=${Q} 侧链削 ${CUT}dB，测 120Hz 处的输出电平`);
console.log("激励 | 变体 | h@120 (谐波层) | 结论");
for (const [cn, gen] of Object.entries(cases)) {
  for (const [vn, shape] of Object.entries(variants)) {
    const E = mk(peak(CUT)), Einv = mk(peak(-CUT));
    const hb = [];
    for (let i = 0; i < N; ++i) {
      const x = gen(i), y = E(x), r = shape(y), h = Einv(r);
      if (i > N/2) hb.push(h);
    }
    const lv = goertzel(hb, f0);
    console.log(`${cn} | ${vn} | ${lv.toFixed(1).padStart(7)} dB | ${lv > -60 ? "有铃声" : "无声"}`);
  }
}

console.log("\n--- 干声掩蔽测试：同一铃声，叠加在有无 120Hz 原声之上 ---");
{
  const E = mk(peak(CUT)), Einv = mk(peak(-CUT));
  const withTone = [], withoutTone = [];
  for (let i = 0; i < N; ++i) {
    const base = 0.15*rnd();
    for (const has of [true, false]) {
      const x = base + (has ? Math.sin(2*Math.PI*f0*i/fs)*0.4 : 0);
      const h = Einv(Math.tanh(k*E(x)) - k*E(x));
      if (i > N/2) (has ? withTone : withoutTone).push(x + h);
    }
  }
  console.log(`原声该频段很响 -> 输出@120 = ${goertzel(withTone, f0).toFixed(1)} dB`);
  console.log(`原声该频段为空 -> 输出@120 = ${goertzel(withoutTone, f0).toFixed(1)} dB`);
}

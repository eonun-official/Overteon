# Overteon v2.0.0 — 改动日志

记录时间：2026-11-03（初稿）／2026-11-04（补充工程说明）
工作目录：`c:\Users\Eonun\Documents\SupersawOmega`
目标插件：**Overteon**（原名 Harmonic Injector，内部代号「主动被动均衡」）
厂商：`Eonun` · 制作人 Eur9ka · 设计者 Eonun
回滚基线：`_backup_v1.9.0\`（工程无 git，把对应文件拷回原位即可回滚）

---

## 一、C++ DSP 层

### 1. EQ 增益量程四档（HIGHLIGHT）

- 新增 choice 参数 `scRange`（侧链）：`±12 / ±24 / ±48 / ±90 dB`
- 新增 choice 参数 `pqRange`（Post EQ）：`±3 / ±6 / ±12 / ±30 dB`
- **关键设计**：Gain 参数本身仍保持 ±12 dB 不动，量程通过倍率实现：

```cpp
static constexpr double kScRangeMult[4] = { 1.0, 2.0, 4.0,  7.5 };
static constexpr double kPqRangeMult[4] = { 0.25, 0.5, 1.0, 2.5 };
// 有效增益 = 参数值 × 倍率
```

  好处：**旧预设 100% 兼容**（倍率默认档 = ×1）。
- `processBlock` 中实际用 `par.scG[b] * scMultS`，`scMultS / pqMultS` 每块做 0.05 一阶平滑（切档不咔哒）。
- 量程参数进入 `cacheParams`，进预设。

### 2. 过采样 2x–32x（真实五档）

- `osFactor` choices：`{"2x","4x","8x","16x","32x"}`，默认仍为 `4x`（index 1）。
- `osN[5] / osD[5] / osLat[5]` 五档表，latency 随档位切换自动上报。
- **过采样是全局偏好，不进预设**：
  - 构造时从 `%APPDATA%\TengyunAudio\HarmonicInjector.settings` 读（XmlElement 根标签 `"HINJGLOBAL"`）
  - `setStateInformation` 保住当前 osFactor
  - `loadPresetFile` 也保住当前 osFactor
  - 三处保护，避免加载预设把全局设置冲掉。

### 3. ±90 dB 电平安全性（审计结论，无需改代码）

链路：`dry → [+90dB bell] → tanh 饱和 → 减线性分量 → ±4 谐波钳 → 逆 EQ → +dry → ±8 输出钳`

- LS 归一化 + tanh 有界 → 饱和输出天然有界
- 谐波支路 ±4 钳位 → 极端增益下谐波增量被限制在极小安全值
- 输出 ±8 钳位兜底
- 全程 double 运算，约 300 dB 余量
- 切档 0.05 平滑 → 无瞬态尖峰

结论：**不会溢出，dry 信号在任何档位都逐样本干净保留。**

### 4. 失真拓扑相位安全性（审计结论）

拓扑：`out = dry(未触碰) + MIX · E⁻¹( NL(E(x)) − g₀·E(x) )`

- 谐波在**同一过采样域、同一批样本**内相减 → 线性分量样本级精确抵消
- dry 路径完全不经过任何滤波器 → **与干信号相位绝对一致，结构上不存在相位问题**
- 没有线性相位 FIR → **结构上不可能产生前振铃**
- 结论：仍是「只加不减」的 active-distortion EQ。

---

## 二、UI / WebView 层（`web_fx/index.html`）

版本号更新为 `v2.0.0 · EONUN`（初版曾是 `TENGYUN AUDIO`，已随改名一并清除）。

### a. 量程徽章 + 下拉

- 两个 EQ 面板右上角各一个徽章，显示当前档位 `±N dB ▾`，点击弹出下拉选档。
- 徽章、下拉按钮均**显示有效增益**。
- 网格 dB 刻度线随档位自动换步长（1/2/4/8/10/16/30 dB），曲线、节点、tooltip、拖拽换算全部统一 `g_eff = param × mult`。
- 徽章点击会互斥关闭其他下拉。

### b. 滚轮调 Q 不再「鼠标追点」

- 滚轮开始调 Q 时锁定该频段节点 1.2 秒（`_wLock / _wUntil`）。
- 锁定期间鼠标不用跟着节点跑，节点移动也不丢目标。
- Shift + 滚轮 = 0.25 倍微调。

### c. 双击频点数值编辑框

- 双击任一节点 → 弹出浮动编辑框，含 **Freq / Gain / Q** 三个输入框。
- 频率支持 `10k` 简写（正则 `/(-?[\d.]+)\s*(k)?/i`）。
- Gain 自动除倍率并夹在 `±R`；Q 夹在 `0.1..6`。
- 编辑框**绑定频段**，节点移动时编辑框跟着走（`drawCurve` 末尾 `updateEdit(false)`）。
- 打开状态下点其他节点 → 编辑框**换绑到新节点**，不会消失。
- 点空白 / 点 ✕ / Esc → 关闭。
- 输入框内 `pointerdown / dblclick / wheel` 全部 `stopPropagation`，不会误触画布。

### d. 刻度数字配色

- 侧链 EQ 左侧 dB 刻度数字：`rgba(255,92,138,.85)`（梅红）
- Post EQ 左侧 dB 刻度数字：`rgba(67,217,196,.85)`（青绿）
- 频率轴刻度保持灰色 `#4a525f`，避免过花。

### e. 电平条迁移（Pro-Q4 风）

- `#pqMeters` 贴 Post EQ **右缘**竖排，`z-index:6`，半透明底 `rgba(13,15,19,.30)`。
- 四个竖条：`mIL / mIR`（输入 L/R）、`mOL / mOR`（输出 L/R）。
- 左侧带 `.mscale` 响度刻度：`0 / -6 / -12 / -24 / -48` dB。
- 曲线可从电平条下方透出，频段变化不被遮挡。
- meter 事件 ID 未改，JS 监听无需变动。

### f. 顶部旋钮 Pro-Q4 化 + 双击复位

- `knob()` 完全重写为 SVG 结构：`.ktrk` 细圆环底 + `.karc` 强调色弧线 + `.kndl` 指针 + `.kdot` 中心点。
- 弧线从 −135° 起算，`P(a)` 极坐标转换。
- 颜色用 `arc.style.stroke = accent`（走 style 而非 presentation attribute，规避 Chromium 对 `var()` 的兼容问题）。
- 双击复原默认值，8 个旋钮默认值：

| 参数 | 默认 |
|---|---|
| INPUT `inGain` | 0 dB |
| DRIVE `drive` | 1.5 |
| EVEN/ODD `evenOdd` | 0 |
| HARMONICS `mix` | 100% |
| HARM GAIN `harmGain` | 0 dB |
| DE-EMPH `deEmph` | 100% |
| AIR PROTECT `airProtect` | 20000（显示 OFF） |
| OUTPUT `outGain` | 0 dB |

### g. 过采样控件改下拉

- `sgOs` 由分段按钮改成 `dropdown()` 下拉，五档 2X / 4X / 8X / 16X / 32X。
- 新增通用 `dropdown(host, id, opts)` 工厂。

### h. About 弹窗

- 点击标题 `HARMONIC INJECTOR` → 弹出。
- 关闭：✕ 按钮 / 点击遮罩 / Esc。
- 内容英文，三段：
  1. 拓扑卖点：失真后立即激活拓扑算法，同时解决相位和前振铃，只输出失真谐波；「只加不减、无相位问题」设计保证声音支撑性和通透感的同时尽可能不影响动态。
  2. 制作人 **Eur9ka**、设计者 **Eonun**（`Produced by Eur9ka · Designed by Eonun`）。
- logo 走 JUCE 资源分发：`getBackendResourceAddress("eonun_logo.png")`。

### i. 通用 dismiss

- 点外部关闭：右键类型菜单、量程菜单、下拉菜单三套。
- JS 内联脚本已通过 node 语法校验（52,705 字符，0 错误）。

---

## 三、其它支持文件

- `web_fx/eonun_logo.png`：新增（1200×350 白字透明底）。
- `src/FxWebEditor.h`：`allParamIDs` 末尾追加 `"airProtect","evenOdd","msMode","autoGain","scRange","pqRange"`；`getResource` 加 `eonun_logo.png` 路由；`loadPresetFile` 保 osFactor。
- `CMakeLists.txt`：`FxWebAssets SOURCES` 加 `web_fx/eonun_logo.png`。

---

## 四、验证脚本 `verify_dsp.cpp`

已扩展为 v2.0 验证套件。**本机无 g++ / clang++，需用户自跑。**

编译命令：

```bash
g++ -O2 -std=c++17 verify_dsp.cpp -o verify_dsp && ./verify_dsp
```

| 测试 | 内容 |
|---|---|
| TEST 1–3 | 三种谐波提取算法对比 + active-distortion-EQ 身份验证（原有，保留） |
| TEST 4 | **因果 / 前振铃**：脉冲过谐波支路，t₀ 前残留必须精确为 0；线性区基频泄漏 < −60 dB |
| TEST 5 | **混叠回折衰减**：128x 参考测 tanh 谐波复振幅，解析折叠枚举对比 1x–32x 各档带内混折垃圾电平；内置 1x 直接仿真自校验（模型误差 < 0.1% 才判可信） |
| TEST 6 | **±24/48/90 dB 压力**：完整链路逐级峰值、NaN 计数、钳位激活率，判定 SAFE |

TEST 5 说明：测试音用 6 kHz，使 5f=30k 在 1x 时就折叠回带宽内，是最能暴露混叠的点；参考采 128x（Nyquist 3.072 MHz），0.2 s = 1200 整周期，无泄漏。

---

## 五、构建状态（已完成）

VST3 已成功导出并多次实机测试，构建链路验证通过（含 16x/32x 过采样、四档量程、WebView2 UI、EDIX 字体嵌入）。

本机 `cmake` 不在 PATH，用 VS 18 Community 自带的那份：

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
```

- configure：`& $cmake -S . -B build -A x64`
- build： `& $cmake --build build --config Release --target HarmonicInjector_VST3`
- 产物： `build\HarmonicInjector_artefacts\Release\VST3\Overteon.vst3\`

> 注意：内部 target 名仍是历史名 `HarmonicInjector`，实际产品名由 `PRODUCT_NAME "Overteon"` 决定，所以命令行写 `HarmonicInjector_VST3`、出包叫 `Overteon.vst3`，这是正常的，不是错配。

---

## 六、目前仍未闭环的事项

1. **`verify_dsp.cpp` 未在本机跑过**（无 g++），TEST 4/5/6 的数值结论仍属"设计预期"，不是实测。
2. **±90 dB 的音乐性听感**需继续实机判断（电平安全已由审计+钳位保证，不是听感问题）。
3. **20k 频率上限议题**：见第十节。当前维持 20k 上限，**已决定暂不改动**，讨论与实测数据全部存档在第十节。

---

## 七、改动文件总表

| 文件 | 类型 | 内容 |
|---|---|---|
| `src/FxProcessor.cpp` | 改 | 量程倍率、五档 OS、全局设置读写、cacheParams |
| `src/FxProcessor.h` | 改 | 新参数成员、osN/osD/osLat、倍率平滑状态 |
| `src/FxWebEditor.h` | 改 | 新参数中继、logo 资源、预设保 osFactor |
| `CMakeLists.txt` | 改 | logo 资源登记 |
| `web_fx/index.html` | 改 | 全部 UI 改动，v2.0.0 |
| `web_fx/eonun_logo.png` | 新 | About 用 |
| `verify_dsp.cpp` | 改 | TEST 4/5/6 新增 |
| `_backup_v1.9.0\` | 备份 | 六份原件，回滚用 |

---

## 八、工程结构（2026-11-04 瘦身后）

```
SupersawOmega\
├── CMakeLists.txt          唯一构建入口：一个 juce_add_plugin + 一个资源嵌入目标
├── src\
│   ├── DSP.h               共享 DSP 原语（omega::Biquad、RBJ 系数、幅频响应）
│   ├── FxProcessor.h       处理器声明：参数包、过采样器、平滑状态
│   ├── FxProcessor.cpp     处理器实现：参数布局 + 整条 DSP 链 + 全局设置
│   ├── FxWebEditor.h       WebView 编辑器声明：参数中继、资源分发
│   └── FxWebEditor.cpp     编辑器实现（含 openExternal 等原生函数）
├── web_fx\
│   ├── index.html          全部 UI（HTML + CSS + JS，单文件，Canvas 绘图）
│   ├── eonun_logo.png      About 弹窗 logo
│   └── edix.otf            标题字体
├── JUCE\                   JUCE 8.0.13 源码（编译依赖，勿删）
├── webview2\               WebView2 SDK 静态库（编译依赖，勿删）
├── build\                  CMake 生成 + MSVC 中间产物（可删，删后需全量重编）
├── dist\                   打包输出（zip / 解包后的 .vst3）
├── _backup_v1.9.0\         回滚基线（v1.9.0 六份原件）
├── verify_dsp.cpp          离线 DSP 验证脚本
└── CHANGELOG_v2.0.0.md     本文档
```

各文件职责要点：

| 路径 | 职责 | 能不能删 |
|---|---|---|
| `src/DSP.h` | 双二阶滤波器实现，SC 与 Post 两条 EQ 共用；内部有 `jlimit(10, fs*0.45)` 钳制，这是超高频安全的唯一防线 | **不能** |
| `src/FxProcessor.*` | 插件本体。DSP 链顺序：`SC EQ → 过采样 → 饱和 → 谐波提取 → 逆 EQ → +干声 → Post EQ → 输出钳`。全局设置写在 `%APPDATA%\Eonun\HarmonicInjector.settings` | 不能 |
| `src/FxWebEditor.*` | 编辑器壳：`WebSliderRelay` 负责参数双向同步；30 Hz 推送 meter / spectrum；`.hinj` 预设读写；DPI 缩放与 jiggle | 不能 |
| `web_fx/index.html` | 全部界面。改这里等于改 UI，**但必须重新编译才生效**（见第九节） | 不能 |
| `JUCE\` | 框架源码。已就位，**不要重新下载** | 不能 |
| `webview2\` | WebView2 SDK。`JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1` 依赖它 | 不能 |
| `build\` | 全是可再生成的中间物。`build\juce_binarydata_FxWebAssets\` 里是资源转成的 C++ 头文件 | 可删，代价是全量重编 |
| `dist\` | 出包目录，纯输出 | 可删 |
| `_backup_v1.9.0\` | 回滚源 | 建议保留 |

合成器的源码（`PluginProcessor.*`、`PluginEditor.*`、`WebEditor.*`、`web\`）以及它在 build 里的 16 个产物目录已全部清除。目录名 `SupersawOmega` 只是文件夹名字，与插件无关。

---

## 九、编译与打包流程

### 1. 资源是编译期嵌入的

`CMakeLists.txt` 里这段是关键：

```cmake
juce_add_binary_data(FxWebAssets
    HEADER_NAME FxData.h
    NAMESPACE FxData
    SOURCES web_fx/index.html web_fx/eonun_logo.png web_fx/edix.otf)
```

它把 `index.html`、`eonun_logo.png`、`edix.otf` 在**编译时**转成 C++ 字节数组（`FxData::index_html` 等），由 `FxWebEditor::getResource()` 分发。

**所以：改了 UI 必须重新编译，直接替换 VST3 里的文件是无效的。** 反过来，往 `web_fx/` 加新素材，必须先登记进上面这段 `SOURCES`，否则编译时不会被嵌入。

### 2. 编译

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

# 首次或改了 CMakeLists.txt：configure
& $cmake -S . -B build -A x64

# 只改了 src / web_fx：直接 build（增量，几十秒）
& $cmake --build build --config Release --target HarmonicInjector_VST3
```

- target 名是历史名 `HarmonicInjector`，产品名由 `PRODUCT_NAME "Overteon"` 决定
- JUCE 通过 FetchContent 拉到 `build\_deps\juce-src`；**清空 build 目录等于要重新联网拉 JUCE**，除非配置 `-DFETCHCONTENT_SOURCE_DIR_JUCE=./JUCE` 指向本地那份

### 3. VST3 bundle 是怎么组装出来的

JUCE 在 `--target HarmonicInjector_VST3` 这一步直接产出完整 bundle 目录：

```
build\HarmonicInjector_artefacts\Release\VST3\Overteon.vst3\
├── Contents\
│   ├── x86_64-win\
│   │   └── Overteon.vst3          ← 真正的 DLL（约 6.7 MB）
│   ├── Resources\
│   │   └── moduleinfo.json        ← 插件元数据：Name / Vendor / 版本 / 参数
│   └── PkgInfo                    ← bundle 类型标记
```

`moduleinfo.json` 是验证改名是否生效的地方——里面必须是 `"Name": "Overteon"`、`"Vendor": "Eonun"`。它由 CMake 里的 `PRODUCT_NAME` / `COMPANY_NAME` 生成，**不是手写的，也不能手改**（改了下次编译会被覆盖）。

### 4. 打包到 dist

用 PowerShell 把整个 bundle 压成 zip：

```powershell
Compress-Archive -Path "build\HarmonicInjector_artefacts\Release\VST3\Overteon.vst3" `
                 -DestinationPath "dist\Overteon_v2.0.0_HHMM.zip" -CompressionLevel Optimal
```

- 包内结构是 `Overteon.vst3\Contents\...`，约 2.88 MB（压缩后）
- 名字带时间后缀是为了绕开"上一次的 zip 被占用导致写入失败"
- 解包后得到 `dist\Overteon.vst3\`，就是可直接安装的文件夹

### 5. 安装 / 卸载

- **安装**：把整个 `Overteon.vst3` 文件夹复制到 `C:\Program Files\Common Files\VST3\`，重启 DAW
- **升级**：先关闭 DAW（否则文件被锁），再覆盖
- **卸载**：删掉该文件夹；全局设置残留在 `%APPDATA%\Eonun\`，可一并删除
- **改名带来的影响**：插件 UID 由"厂商码 + 产品名"决定，从 `Harmonic Injector` 改成 `Overteon` 后，旧工程里的实例会显示为"插件丢失"，必须重新插入一次

---

## 十、20 kHz 以上频段议题（讨论存档，2026-11-04 · 已暂停）

### 1. 起因

EQ 的 20k 标号贴在画布最右缘，与右侧响度表重叠；用户希望像 Pro-Q 一样让 20k 退到右侧、留出更远的高频空间，并能把频点拖到 20k 之后。

### 2. 三家做法

| 厂家 / 产品 | 手段 | 频段上限 | 代价 |
|---|---|---|---|
| FabFilter Pro-Q 4 | 可听区贴合（官方仅承诺"尽可能匹配模拟幅频响应"，实现未公开） | 10 Hz – 30 kHz，显示轴画到 30k | 低延迟 |
| 三体声音科技 Kirchhoff-EQ | **可开关的线性相位 2x 升采样** | 未公开（官网/PA 页面均未标注） | 0.3~0.6 ms 级延迟 |
| 大多数插件 | 限制在 20k | 20 kHz | 无 |

Pro-Q 4 手册原文："Even though frequencies above 20 kHz are generally inaudible, the display extends to 30 kHz so you can put filters above this limit. The left part of the filter, extending into the audible frequency spectrum, still affects the sound."

**关键认知**：中心频率在 20k 以上的滤波器，其裙边会垂进 18~20k 的可听区，所以"放一个 26k 的高切"是有意义的——这才是允许拖过 20k 的真正理由。

顺带记录：Kirchhoff-EQ 的可调增益显示范围是 3 / 6 / 12 / 18 / 30 dB，与本插件 Post EQ 的 3 / 6 / 12 / 30 几乎同款（我们少一档 18 dB）。

### 3. 我们当前的真实限制

[DSP.h](file:///c:/Users/Eonun/Documents/SupersawOmega/src/DSP.h) 的每个 biquad setter 都有一句：

```cpp
const double w = twoPi * juce::jlimit (10.0, fs * 0.45, (double) fIn) / fs;
```

因此：

| 采样率 | 实际可算上限 | 30k 角点 |
|---|---|---|
| 44.1k | 19.85k | 不可能，需近似 |
| 48k | 21.6k | 不可能，需近似 |
| 96k | 43.2k | **完全真实，零误差** |
| 192k | 86.4k | **完全真实，零误差** |

UI 曲线用与 DSP 相同的 RBJ 公式绘制，所以画面与声音**一起**被钳住，二者一致，只是上限真实存在。

### 4. 拟合方案实测结果（`tools/fit_check.js`）

方法：reference = 用 fs=20 MHz 计算同一 f0（等价模拟原型）；candidate = 用真实 fs、角点钳在 0.45·fs、对 (fc, Qc) 做坐标下降拟合；误差 = 20 Hz–20 kHz 上的最大 dB 偏差。

可信部分（Bell / Notch）：

| 类型 | 常规 Q≈2 | 高 Q≈5 | 低 Q≈0.7 |
|---|---|---|---|
| Bell @22k–35k | 0.33 ~ 0.73 dB | 0.06 ~ 0.18 dB | 最差 1.0 dB |
| Notch @22k–35k | 0.23 ~ 0.64 dB | 0.04 ~ 0.14 dB | 最差 1.6 dB |

**不可信部分**：Shelf 与 Cut 的误差数字（8~17 dB）源于优化器陷入局部最优（例如 LowShelf 全部收敛到 `fc≈19845, Q≈0.2` 的明显错解），不是物理极限。若重启议题必须先重测。

连续性问题：脚本曾报告"20k 处最大跳变 0.27 dB @18.4k"，经复核属于**判据过严**——那是 12 dB/oct 高切在 18.5k 的自然斜率，不是突跳。真实行为是 21.6k 之后曲线冻结（斜率突然变平，形成折角），**不存在听觉上的突变**。

### 5. 结论

**"全域 ≤0.2 dB"不可达。** 单级 biquad 在角点被钳制后只剩 Q 一个自由度，贴不住模拟曲线形状。若强行上线，会造成"画面与声音在高频段相差 0.5~1.5 dB"的不诚实状态。

### 6. 四条候选路线（未选）

| 路线 | 内容 | 精度 | 代价 |
|---|---|---|---|
| A | 仅 Bell/Notch 拟合并放宽到 30k，Shelf/Cut 保持 20k 硬墙 | 0.05~1.6 dB | 中等 |
| B | 换全局优化器 + 两级串联 | 未知，可能 0.3~0.5 dB | 大，不保证达标 |
| C | 轴画到 30k，节点限 20k，20k 之后灰色禁区 | 0 dB（不撒谎） | 极小，仅 UI |
| D | 仿 Kirchhoff-EQ：Post EQ 走可选 2x 线性相位升采样 | 0 dB（真实） | 延迟 +0.3~0.6 ms、CPU×2、需干声延迟补偿 |

路线 D 的可行性依据：本插件已有 `dryFifoL/dryFifoR` 干声延迟缓冲与精确的延迟上报（[FxProcessor.cpp:246-248](file:///c:/Users/Eonun/Documents/SupersawOmega/src/FxProcessor.cpp#L246-L248)），延迟补偿的机械结构是现成的；96k 及以上工程本身不需要 2x。

**落脚点**：若将来重启，优先考虑 **D**（唯一"不撒谎"的实现），其次 **C**（零风险、解决刻度重叠）；A/B 会造成画面与声音不一致，不建议。

### 7. 当前决定

**暂停，不做任何改动。** 插件维持 20k 上限与现有画布比例。

保留物：

- `tools/fit_check.js` — 数值验证脚本，`node tools/fit_check.js` 可直接重跑，无需编译器
- 已知待办：若要重启议题，第一步是修好 Shelf/Cut 的优化器（目前该部分数据不可用）

---

## 十一、Schranz 模式（2026-11-04）

### 11.1 它是什么

原来侧链 EQ 标题栏里的 `SC LISTEN` 按钮被**删除**，它的行为升级为 MODE 的第三项：

```
CLASSIC   |   HARMONIC   |   SCHRANZ
```

信号链（保持原样，未做任何 DSP 改动）：

```
干声 → 侧链 EQ（±12/24/48/90 四档全量可用）→ jlimit(-8, +8) → OUT GAIN × AUTO GAIN → 输出
```

它输出的就是"饱和器马上要听到的那一路"：**失真前、后 EQ 前**。

### 11.2 三个模式的有效区块

| 模式 | 侧链 EQ | Post EQ | 说明 |
|---|---|---|---|
| Classic | **灰**（不参与） | 活 | 原版交叉淡化饱和 |
| Harmonic | 活 | 活 | 减法拓扑，只叠谐波 |
| Schranz | 活 | **灰**（早退，不经过） | 侧链塑形的信号直接输出 |

灰化实现：区块加 `.inactive` 类，画布 `opacity .22` 且 `pointer-events:none`，量程徽章同样禁用，副标题前自动出现 `INACTIVE ·` 前缀。

### 11.3 AUTO GAIN 接入

Schranz 原先走早退路径，绕过了 auto gain。现已接入主路径同一套规则：

```cpp
target = sqrt(输入能量 / 输出能量)      // RMS 匹配
clamped = jlimit(0.25, 4.0, target)
agComp += 0.05 * (clamped - agComp)   // 一阶平滑
```

参考输入取 `dryBufD` 里的干声，与另两个模式一致。AUTO GAIN 关闭时平滑回到 1.0。

代码位置：`src/FxProcessor.cpp` 的 `if (dmode == 2)` 分支。其余判据：
- 侧链 EQ 执行条件从 `dmode == 1` 改为 `dmode >= 1`
- 早退条件从 `par.scListen->load() > 0.5f` 改为 `dmode == 2`
- 参数 `scListen` 已从 ParameterLayout、`Params` 结构、`allParamIDs` 中全部移除

老预设若存过 `scListen = on`，载入时该参数被忽略，其余参数正常。

---

## 十二、Resonant Carving：深削侧链会产生"铃声"（机制已实测确认）

### 12.1 现象

在 **Harmonic 模式**下，把侧链 EQ 某个频段**削得很深**（±48/±90 档下很容易），

对应的频段反而会冒出一个**有音高、会鸣响的窄带**。用户描述为"像铃声一样"、"pumping"。

这不是 bug，也不是心理声学错觉——**是真真实实被放大出来的能量**。

### 12.2 实测数据

条件：48 kHz、f0 = 120 Hz、Q = 2、drive k = 2、白噪输入。用 `tools/harmonic_bell.js` 复现。

| 侧链削减 | 谐波残差 @f0 | 输出 @f0 | 输出总 RMS |
|---|---|---|---|
| 0 dB | −96.0 dB | −96.0 dB | −32.1 dB |
| −12 dB | −95.9 dB | −90.0 dB | −32.1 dB |
| −30 dB | −87.6 dB | −58.6 dB | −31.0 dB |
| −45 dB | −91.9 dB | −50.0 dB | −26.9 dB |
| −60 dB | −95.9 dB | −43.9 dB | −21.6 dB |
| −75 dB | −91.9 dB | −26.3 dB | −9.0 dB |
| −90 dB | −94.2 dB | −14.9 dB | +0.8 dB |

关键对比：**残差在 f0 处基本不变（一直在 −92 dB 附近），输出却涨了 81 dB。**

### 12.2b 隔离实验：三个成分逐一拿掉（`tools/harmonic_origin.js`）

条件：f0 = 120 Hz、Q = 2、侧链削 −60 dB、drive k = 2.5、2 秒。测 f0 处的相干电平。

| 激励 | 链路 | f0 处输出 | 结论 |
|---|---|---|---|
| 单一 1 kHz 正弦（窄带） | 完整（饱和 + 逆 EQ） | −110.0 dB | 无铃声 |
| 单一 1 kHz 正弦（窄带） | 去掉非线性（线性直通） | −600 dB（精确 0） | 无铃声 |
| 白噪声（宽带） | 完整（饱和 + 逆 EQ） | −49.4 dB | **有铃声** |
| 白噪声（宽带） | 去掉非线性（线性直通） | −600 dB（精确 0） | 无铃声 |

**结论：必须同时满足两条——宽带激励 + 非线性。**

- 窄带激励（1 kHz 正弦）的谐波只落在 2k/3k/4k…，**根本不到 120 Hz**，所以 f0 处无线可抬 → 没有铃声
- 去掉非线性后，f0 处是数值零，不是"很小" → 铃声 **100% 来自非线性产物**，不是干声残留、也不是滤波器振铃（振铃是线性的，去掉非线性仍然存在）

干声掩蔽测试（同一铃声，叠在"有 / 无 120 Hz 原声"之上）：

| 原声在 f0 | 输出 @f0 |
|---|---|
| 有（0.4 幅度正弦） | −9.4 dB（主要是原声） |
| 无 | −12.1 dB（铃声裸露） |

铃声在两行里**都在**；原声只是把它盖住。这就是"某个乐器根本没有低频，我把低频 EQ 拉到底，它反而冒出一层低频"的真实来源——听到的不是那个乐器，是整段音乐的互调。

### 12.3 机制

```
侧链 EQ 在 f0 挖 −X dB 的坑
        ↓
饱和器 对"剩下的频段"做非线性 → 互调产物铺满全频段，坑底也有
        ↓
去强调（de-emphasis）是侧链 EQ 的精确逆运算 → 在同一个 f0 抬 +X dB
        ↓
坑底那层互调底噪被抬成一条清晰的窄带
```

三句话总结：

1. **逆 EQ 必须存在**，否则你在侧链做的染色会污染谐波层
2. **互调底噪无处不在**，坑底也有（约 −92 dB，跟削多深无关）
3. **逆 EQ 在坑底是等量正增益**，削 −90 dB 就是抬 +90 dB

因此：**侧链挖多深，谐波层就在那个频点被抬多高。** 这是拓扑的数学必然，不是巧合。

### 12.4 为什么是"铃声"而不是噪声

去强调不是宽带提升，而是**在 f0 处的一个谐振峰**。它把坑底那层宽带互调底噪筛成一条以 f0 为中心的窄带，所以有音高、会鸣响。

- **任何深度的削减都会触发**，差别只在抬升后的电平够不够听见
- **削得越深 → 抬得越高 → 越响**，这一点与 Q 无关
- **Q 越高 → 窄带越窄 → 越接近纯音"叮"**。带宽 ≈ f0 / Q（120 Hz、Q=2 时约 60 Hz；Q=6 时约 20 Hz）
- **Q 越低 / 频段越宽 → 窄带越宽 → 越接近低频的 pong / pumping**
- 只有 **Bell / Shelf** 会这样。Notch 与 LowCut / HighCut 在代码里标记为不可逆（`inv = false`），不做去强调，所以不会产生铃声

所以 Q 决定的不是"有没有"，而是**什么性格**。

### 12.5 说明书（修正版，可直接用于发布）

**先记住一句话：侧链 EQ 只改谐波的味道，不碰干声。**

干声从入口到出口一次都没有经过侧链 EQ，它只是被延迟到与谐波层对齐，然后原样加回去。所以在 Harmonic 模式下，你听到的"某频段被削掉了"并不会真的发生在干声上——被改的只有叠在上面的那层谐波。

由此产生两种完全相反的手感：

| 你把侧链 EQ 往哪推 | 谐波层发生什么 | 听感 |
|---|---|---|
| **推高（boost）** | 送进饱和器的该频段能量更多 → 由它生成的谐波更密 | 那个频段变"沙"、变"糙"、有颗粒感，像被砂纸打磨 |
| **拉低（cut）** | 由该频段生成的谐波变少（沙消失），但坑底的互调被去强调**等量抬回来** | 整体变干净、变薄，同时该频点冒出一条**有音高的窄带**——铃声 / pong |

**为什么拉低反而会"多出声音"？**

饱和器处理的不是"你留下的频段"，而是整个信号。只要音乐里还有别的频段在响，饱和器就会持续产生铺满全频段的互调产物——包括你挖的那个坑底。去强调阶段又会把坑底按你削掉的量**等量抬回来**（削 −60 dB 就抬 +60 dB）。于是坑底那层原本听不见的底噪，被抬成一条清晰的窄带。

这也解释了"这个乐器根本没有低频，我反而把低频 EQ 拉到底，它突然冒出低频"：**你听到的不是那个乐器，是整段音乐在饱和器里互调、再从坑底被抬起来的声音。**

**那为什么有时候感觉是"减少"，有时候是"增加"？**

看你挖的频段**原本有没有东西**：

- 原本有能量 → 干声盖住铃声，你主要感受到的是谐波被抽走、沙感消失 → **减少 / 变干净**
- 原本没有能量 → 干声本来就是空的，抬起来的铃声没有任何东西盖住 → **凭空出现 / 增加**

**Q / 频宽怎么用**

- **频宽（低 Q）** → 抬升带宽宽 → 低频 pong、pumping，适合做 sub 的律动
- **窄（高 Q）** → 抬升带窄 → 有明确音高的铃声，适合做 formant / 哨音
- **深度决定响不响，Q 决定什么性格**，两者独立

**相位**

1. **干声**：不走侧链 EQ、不走逆 EQ，只与谐波层共用同一套过采样器和保护滤波 → 与谐波层严格同延迟，结构上不存在相位错位
2. **谐波层线性分量**：在过采样域同批样本内按最小二乘增益减去 → f0 处的线性残留被清零，所以被抬起来的东西里没有"原信号的相位残留"
3. **侧链与去强调**：同一组最小相位 biquad 正逆使用 → 不引入前振铃；保护滤波是线性相位 FIR，只做带宽保护，不参与音色

**一句话**

> **Resonant Carving：你削掉的坑不会让干声变安静，只会把饱和器填进坑里的互调，
> 按你削掉的量等量抬起来。削得越深越响，Q 决定它是 pong 还是叮。**

### 12.6 使用注意事项

| 项 | 数值 |
|---|---|
| f0 处提升量 | ≈ 侧链削减深度 × DE-EMPH 百分比（仅 Bell / Shelf） |
| 最大实测 | 削 −90 dB → 输出 @f0 提升 81 dB |
| 副作用 | **谐波层整体响度会同步上升**（−90 dB 时总 RMS 由 −32 涨到 +0.8，+33 dB） |
| 建议 | 极端深度下配合 OUT GAIN / AUTO GAIN 使用，切换回 0 dB 时会有明显电平跳变 |

### 12.7 已排除的错误解释（存档，避免重犯）

| 假设 | 实测结论 |
|---|---|
| ±8 硬削顶产生互调填进坑里 | **否**：白噪 0.5 幅度下削顶命中 0 次；且滤波器全频段最大增益 ≤ 0 dB |
| 极深增益导致滤波器振铃增强 | **否**：削得越深，脉冲在 f0 处的振铃反而越弱（−72 → −98 dBFS） |
| 心理声学掩蔽解除 | **不是主因**：真实能量确实涨了 81 dB，不是错觉 |

### 12.8 保留物

- `tools/schranz_probe.js` — 验证 Schranz 链路线性、全频段无 boost
- `tools/harmonic_bell.js` — 复现 12.2 表（`node tools/harmonic_bell.js`）
- `tools/harmonic_origin.js` — 复现 12.2b 隔离实验（`node tools/harmonic_origin.js`）
- `tools/harmonic_split.js` — 验证"侧链 EQ 不碰干声"的负结果，即它**不是**干声/谐波的分流器（`node tools/harmonic_split.js`）

---

## 十三、顶部综合电平表 + 谐波监控环（2026-11-04）

### 13.1 为什么删掉 EQ 里的竖条

旧表（IL/IR/OL/OR 四根竖条）贴在 Post EQ 左缘，有三个问题：压住 0–150 Hz 频谱区；表的 dB 刻度与 Post EQ 自身刻度撞在同一条竖列里；且三个模式下两个 EQ 都会灰化，插件级 I/O 表不该跟着灰。现整组删除，刻度数字 x 由 58 回到 6。

### 13.2 新表布局（顶栏居中）

- 顶栏加高到 64 px，三段式：左 logo+版本 / 中 `#hmeter`（352×56 SVG）/ 右 INIT·SAVE·LOAD。
- 右侧三按钮去框去底，纯文字，hover 提亮。
- 综合表：中心圆环 + 四条等长横线（每侧 110 px），从圆环向外涨。
  - **上对 = 输出**：左 OUT L、右 OUT R，梅红 `#ff5c8a`
  - **下对 = 输入**：左 IN L、右 IN R，青绿 `#43d9c4`
  - 映射 `(dB + 60) / 60`，0 dB 满格。

### 13.3 谐波监控环（HARM）

- 物理量：**谐波层 RMS / 干声 RMS**（输出侧、Post EQ 之前），C++ 每块累计能量，0.15 一阶平滑，以 dB 推进 `meterHarm`，30 Hz 随 meter 事件发 `hm`。
  - Harmonic：直接累加合并后的谐波贡献 `hL/hR` 与干声贡献
  - Classic：累加「湿 − 干」（饱和器加进来的东西）× dmix
  - Schranz：无饱和器，按比例衰减到 0
- UI 归一化 `f = (hm + 48) / 48`（−48 dB → 0，0 dB → 100%），圆环弧长 = f，中心显示百分比。
- 颜色非线性分段（平台 + 陡峭过渡）：0–45% 绿 `#43d9c4` → 45–60% 陡变 → 60–75% 琥珀 `#f0a23b` → 75–88% 陡变 → 88%+ 梅红 `#ff5c8a`；≥97% 亮红 `#ff2a44` + 辉光，百分比数字同步变红。

### 13.4 改动文件

`src/FxProcessor.h`（meterHarm / harmRatioS）、`src/FxProcessor.cpp`（能量累计 + Schranz 衰减）、`src/FxWebEditor.h`（meter 事件加 hm）、`web_fx/index.html`（顶栏重构、旧表删除、新表 JS）。已编译通过并出包。

### 13.5 弧形顶栏 + 按像素复刻设计稿（2026-11-04 第二轮）

- **弧形顶栏**：`#hpod`（600×96 SVG）绝对定位于顶栏中央，底边 `M0 64 H130 C180 64 190 96 300 96 C410 96 420 64 470 64 H600`，中央下凸 32 px，给大表让位。侧链标题行背景透明、文字 z-index 12 浮在容器上，弧线与文字互不遮挡。
- **设计稿实测**（原图 1216×169，PowerShell 逐像素取色）：
  - 圆环：圆心 (606,76)、r≈64、灰环宽 ≈5；直线线芯 3 px、辉光约 4 px
  - 直线段：左 128→535、右 676→1083（各 407 px）；线 y=53 / 105（圆心 −23/+29）
  - 颜色：梅红实测 `#fa3c7f`、青实测 `#23ecd7`、灰环 `#3b4350`
- **复刻比例 34/64**：环 r=34 @ (300,48)；线宽 1.8 + `feGaussianBlur 1.7` 霓虹；直线末端距圆心 37.7 px 后接三次贝塞尔，在 35° 处**沿圆环切线方向**接入圆环（不再生硬的直角）。
- **电平填充改用 `pathLength="100"` + dashoffset**：每条路径起点在圆环、终点在外侧，因此填充自然沿「切线钩 + 直线」从圆向外长，无需手算 x 坐标。
- 百分比 16 px 微辉光，下方细分隔线 + HARM；HARM 绿相也改用实测青 `#23ecd7`，极端亮红逻辑不变。

### 13.6 顶栏 / SC EQ 标题行精修（2026-11-04 第三轮）

- **精简杂字**：删除顶栏 `v2.0.0 · EONUN` 与 SIDECHAIN EQ 右侧灰色操作说明；About 弹窗内的版本信息保留。
- **缩小综合表**：`#hpod` 由 600×96 缩为 480×88，圆环由 r=34 @ (300,48) 缩为 r=28 @ (240,44)，百分比改为 14 px；四条横线保持左右等长及原 OUT 上 / IN 下映射。
- **收敛下凸弧线**：中央下凸由 32 px 减为 24 px，横向过渡区由 340 px 缩为 260 px；底边最低 y=88，只进入 SC EQ 标题行，不再侵入 EQ 画布。撤掉上一轮侧链标题的临时 z-index/背景修补。
- **弯钩完整可见**：按参考图实测保留上排向上、下排向下的切线钩几何；彩色电平路径改在圆环之后绘制，避免末端弯曲被圆形填充遮住。`pathLength=100` 的填充逻辑不变。
- 内联 JS 语法检查通过（56,808 字符）；Release VST3 编译通过并出包 `dist/Overteon_v2.0.0_1744.zip`。最终视觉效果仍需在 DAW 实机确认。

### 13.7 中置 Title bar 布局试验 + 弯钩离环（2026-11-04 第四轮）

- **Title bar 移至两个 EQ 之间**：`#eqzone` 内顺序改为完整 SC EQ（标题行 + 曲线画布）→ 64 px `#hdr` → 完整 Post EQ；标题栏内部 logo、综合表、INIT/SAVE/LOAD 布局不变。
- **废弃外扩弧线**：`#hpod` 改为 480×64，删除下凸底座与描边路径；圆环改为 r=28 @ (240,32)，综合表完全限制在标准标题栏内。
- **弯钩不再相接**：四条弯钩以圆心为基准按 32/28 等比外移，末端与圆环保持 4 px 径向间隙，形成近似平行收边，而非真正相切；颜色、填充方向和 OUT 上 / IN 下映射不变。
- 内联 JS 语法检查通过；Release VST3 编译通过并出包 `dist/Overteon_v2.0.0_1755.zip`。该布局为实机视觉试验版。

### 13.8 Post EQ 标题栏下移，形成上下对称（2026-11-04 第五轮）

- `#secPQ` 内顺序改为 Post EQ 曲线画布 → Post EQ 标题栏；整体结构成为 SC 标题栏 → SC 画布 → 中置 Title bar → Post 画布 → Post 标题栏。
- Post 标题栏的文字、说明、量程徽章与尺寸保持不变，仅改变位置。
- `#pqRangeMenu` 改为 `top:auto; bottom:8px`，量程菜单从底部标题栏向上展开；SC 菜单方向不变。
- 内联 JS 语法检查及 Release 编译通过，出包 `dist/Overteon_v2.0.0_1811.zip`。

### 13.9 综合表周围边界局部淡出（2026-11-04 第六轮）

- **设计目标**：保留 SC 标题栏 / 中置 Title bar / Post 标题栏的对称布局，只削弱综合表周围的横向盒子感；不恢复弧形底座，不改变 64 px 标题栏高度。
- **中央背景融合**：`#hdr::before` 在中央 480 px 内从 Title bar 底色渐变到 EQ 画布底色，左右 Logo 与按钮区域保持原结构。
- **边线局部淡出**：移除原先完整贯通的上下 1 px 边线，由 `#hdr::after` 绘制左右保留、中央透明的渐变边线。
- **相邻画布羽化**：SC 画布底部与 Post 画布顶部各增加 16 px 中央渐变，横向范围同为 480 px，并以 mask 羽化两端，避免硬边。
- 综合表 SVG、文字和彩色电平线不使用模糊滤镜，尺寸与位置不变；频谱仅在靠近边界的 16 px 内受到局部柔化。
- 内联 JS 语法检查及 Release 编译通过，出包 `dist/Overteon_v2.0.0_1825.zip`。

### 13.10 上下网格真实连接 + 中央毛玻璃悬浮表（2026-11-04 第七轮）

- **推翻单纯淡出方案**：删除 SC 底部 / Post 顶部的 16 px 不透明渐变遮罩，不再阻断网格。
- **真实上下连接**：`#eqzone` 改为定位容器，`#hdr` 绝对定位于两块 EQ 的中缝；SC 与 Post 画布实际延伸至标题栏下方，纵向网格在中央连续，而不是用背景色模拟连接。
- **局部毛玻璃**：`#hdr::before` 为中央 520×64 px 区域添加 `backdrop-filter: blur(8px) brightness(.72) saturate(.65)`，两端 mask 羽化；综合表 SVG 位于其上且保持清晰。
- **尺寸与排版**：Title bar 由 64 缩至 56 px；`#hpod` 由 480×64 等比放大至 520×69.33 px，垂直居中并允许轻微越出标题栏，圆环及文字同步放大。
- **操作穿透**：悬浮标题栏空白与中央区域不拦截画布鼠标事件，Logo 和 INIT/SAVE/LOAD 保持可点击。
- 内联 JS 语法检查及 Release 编译通过，出包 `dist/Overteon_v2.0.0_1837.zip`。毛玻璃在 WebView2 中的实际视觉效果待实机确认。

### 13.11 连续图表修正版：移除暗带与过强模糊（2026-11-04 第八轮）

- **纠正上一轮理解偏差**：目标不是中央一条模糊通道，而是上下 EQ 在全宽范围内看起来像同一张连续图，Title bar 只是透明地插在上面。
- **彻底移除隔断来源**：`#hdr` 背景改为完全透明；删除 `#hdr::after` 上下边线；不再使用左右深色渐变、中央暗色底或 brightness 压暗。
- **保留真实连续背景**：上下画布仍延伸至悬浮标题栏后方，纵向网格在全宽范围直接显示；即使 WebView2 毛玻璃效果较弱，也不会再出现黑色遮挡带。
- **轻霜化而非阴影**：中央滤镜降为 `blur(3px) saturate(.85)`，背景仅 `rgba(255,255,255,.018)`；遮罩从矩形横向羽化改为椭圆径向羽化，中央外无明确边界。
- 56 px Title bar、520×69.33 px 综合表与操作穿透逻辑保持不变。内联 JS 检查及 Release 编译通过，出包 `dist/Overteon_v2.0.0_1844.zip`。

    ## 13.12 中央 EQ 留白 + OVER / TEON 分置（2026-11-04 第九轮）

- **保留隐形 Title bar 与连续纵向网格**，但不再让 SC 最低刻度与 Post 最高刻度直接贴在中缝。
- `EQPanel.plot()` 在 SC 底部和 Post 顶部各划出 32 px 显示留白，合计 64 px；有效绘图区高度仍为画布高度减 32 px，相当于恢复连接前的有效区域，同时保留向中缝延伸的背景。
- `dy()` 增加有效区顶/底参数；dB 横线、刻度数字、频谱、总曲线、分段曲线与节点全部按有效区重映射。纵向频率线仍画满画布，中央背景保持连续。
- 曲线与节点绘制增加有效区 clip；拖拽与双击新建使用同一 `dbAt()` 逆映射，中央留白内双击不会创建节点，拖入留白时增益只停在原 ±R 极限。此改动仅影响 UI 映射，不改变 DSP 参数范围。
- Logo 拆为白色 `OVER` 与红色 `TEON`：OVER 左移进入综合表左侧负空间，TEON 放在右侧电平线与 INIT 之间；INIT/SAVE/LOAD 仍保持原靠右位置。
- 内联 JS 语法检查及 Release 编译通过，出包 `dist/Overteon_v2.0.0_2007.zip`。

### 13.13 恢复完整 Logo，综合表与毛玻璃同步放大（2026-11-04 第十轮）

- 撤销上一轮的 Logo 拆分：恢复左侧原位完整 `OVER<b>TEON</b>`，白色 OVER / 红色 TEON；删除右侧 TEON 和左侧 72 px 偏移，INIT/SAVE/LOAD 布局不变。
- 综合表由 520×69.33 px 等比放大至 550×73.33 px；毛玻璃同步由 520×64 px 放大至 550×68 px，模糊强度、透明背景与椭圆羽化不变。
- 中央 64 px EQ 留白、连续纵向网格、有效区映射、节点交互和透明 Title bar 均保持上一轮状态。
- 内联 JS 语法检查及 Release 编译通过，出包 `dist/Overteon_v2.0.0_2016.zip`。

### 13.14 收紧中缝并修复边界节点裁切（2026-11-04 第十一轮）

- 完整 OVERTEON Logo 仅向右移动 8 px，其余位置、颜色与尺寸不变。
- `EQPanel.plot()` 的内侧留白由 SC/Post 各 32 px 缩至各 24 px，中缝总留白由 64 px 缩至 48 px；连续纵向网格、综合表和毛玻璃尺寸不变。
- **节点不再被硬裁切**：canvas clip 在曲线与填充绘制完成后立即 restore，再绘制节点。节点中心仍受有效区边界限制，但完整圆点可探入留白，不会只剩半圆。
- 双击逻辑改为先检测节点，再判断中央空白：节点探入留白的可见部分仍可打开编辑器；空白区域双击仍不会创建节点。拖拽增益映射与 ±R 限制保持不变。
- 内联 JS 语法检查及 Release 编译通过，出包 `dist/Overteon_v2.0.0_2034.zip`。

### 13.15 窗口位置/缩放记忆 + 四路 Peak Hold（2026-11-04 第十二轮）

- **编辑器几何持久化**：新增独立设置文件 `%APPDATA%/Eonun/OverteonEditor.settings`，根节点 `OVERTEONEDITOR`，保存屏幕坐标 `x/y` 与尺寸 `w/h`；不进入 `.hinj` 预设，也不覆盖 `HarmonicInjector.settings` 中的全局过采样偏好。
- 打开编辑器时先校验并恢复尺寸；等宿主创建窗口 peer 后恢复上次屏幕位置。30 Hz 轮询屏幕 bounds，因此宿主直接拖动顶层窗口、未向编辑器发送 `moved()` 的情况也能记录。
- 几何变化后设置 15 tick 防抖，稳定约 0.5 秒才写盘；关闭编辑器时再保存一次。默认仍为 840×624，合法恢复范围沿用 560×416 至 1960×1456。
- **四路 Peak Hold**：OUT L/R 与 IN L/R 各增加一条 2.6 px 低亮度同色尾条，瞬时条仍为 1.8 px 原亮色；尾条位于瞬时条后方。OUT 使用暗梅红 `#6f2946`，IN 使用暗青绿 `#155d58`，组透明度 0.72。
- 每条尾条末端增加 1.7 px 原色亮尖：OUT `#fa3c7f`、IN `#23ecd7`，沿用现有 neon 辉光。亮尖通过 `getPointAtLength()` 精确贴合直线与弯钩路径。
- 瞬时电平创新高时，峰值立即跟随并重新保持 1100 ms；保持结束后按归一化 0.18/s 回落，相当于约 10.8 dB/s，且永远不会低于当前瞬时值。四路独立计时。
- 内联 JS 语法检查及 Release 编译通过，出包 `dist/Overteon_v2.0.0_2048.zip`。窗口位置恢复与峰值回落节奏需在当前 DAW 中实机确认。

### 13.16 透明核心 HARM 环 + 上下斜切控制凹槽（2026-11-04 第十三轮）

- HARM 圆内底色由 `#16181d` 改为 `fill="none"`，圆环内部现在可直接透出连续 EQ 网格与中央毛玻璃，不再是一块黑色圆片；外轨道与四路电平结构不变。
- 按“核心监控仪表”参考加强视觉层级：HARM 动态弧线 3.5→4 px，独立 `harmGlow` 高斯辉光 2.5 px，百分比 14→15 px；删除百分比与 HARM 之间的短分割线。极端红色的动态提示逻辑保留。
- 完整 Logo 在上一轮基础上再右移 8 px，当前左边距 16 px。
- SC 标题栏中央新增 640×38 px 上斜切凹槽，放入 `STEREO / OVERSAMPLING`；Post 标题栏中央新增反向斜切凹槽，放入 `MODE / SATURATION`。上槽窄顶宽底，下槽宽顶窄底，呼应参考红线。
- 凹槽采用 `#171a20` 深底、低对比 1 px 切边与内侧轻阴影；按钮本身仍无边框。clip-path 只用于背景伪元素，不裁切控件或菜单。
- `dropdown()` 新增展开方向：上方 STEREO/OS 向下弹出并显示 ▾，下方 MODE/SATURATION 向上弹出并显示 ▴，避免插件上下边缘截断菜单。
- 底部原四个下拉项移除；AUTO GAIN / SOLO HARM 非弹窗控件，继续保留在底部右侧。Post 标题栏长灰色操作说明移除，为中央凹槽让位。
- 内联 JS 语法检查及 Release 编译通过，出包 `dist/Overteon_v2.0.0_2127.zip`。

### 13.17 制作署名、HARM 字体/辉光与斜切槽去框（2026-11-04 第十四轮）

- About 制作署名改为 `Produced by Eur9ka & Eonun · Designed by Eonun`，明确 Eonun 同时承担制作与设计。
- HARM 百分比增加 `font-weight="600"`，保持 15 px 字号不变，仅略微加粗。
- `#hpod` 增加 `overflow:visible`，`harmGlow` 滤镜区域由 170% 扩至 200%（x/y −50%），解决圆环亮灯后辉光在 SVG 上下边界被截断的问题；不改变仪表几何和其他元素排版。
- 斜切控制槽仅移除完整外框伪元素，内层深色体块改为 inset 0；突兀的上下横线及斜边描线一并取消，现有切角、尺寸、控件和菜单方向不变。进一步造型留待讨论，未提前实施。
- 内联 JS 语法检查及 Release 编译通过，出包 `dist/Overteon_v2.0.0_2149.zip`。

### 13.18 标题栏真实梯形挖空 + 画布延伸毛玻璃（2026-11-04 第十五轮）

- SC/Post 标题栏改为 32 px 绝对覆盖层，EQ canvas-wrap 现在填满整个 section并真实延伸到标题栏后方；不是复制一张背景图。
- `EQPanel.plot()` 分别加入 SC 顶部 32 px、Post 底部 32 px 延伸区，同时保留中缝各 24 px；曲线、频谱、节点和 dB 标尺的有效高度不被拉伸，交互边界仍排除标题栏。
- 标题栏背景使用 `clip-path:polygon(evenodd,…)` 形成真实梯形镂空：SC 窄顶宽底，Post 宽顶窄底；孔洞与 640 px 中央控制区逐点对齐，其余标题栏区域继续遮住画布。
- 原深色梯形填充和内阴影全部移除，改为与中央 Logo 区一致的 `blur(3px) saturate(.85)` 与 0.018 白色玻璃层；无上下横线、无完整描边。
- 原画布纵向频率线自然贯穿延伸区；同一 `freqLabels` 另绘制到标题栏延伸位置，只在梯形镂空内可见，确保频率刻度与网格严格同源、同 x 坐标。
- 毛玻璃只作用于梯形伪元素，四个下拉按钮和菜单保持清晰且不被裁切；上向下、下向上的菜单方向不变。
- 内联 JS 语法检查及 Release 编译通过，出包 `dist/Overteon_v2.0.0_2201.zip`。WebView2 的实际梯形镂空与 backdrop-filter 效果需实机确认。

### 13.19 修复梯形镂空左侧三角伪影（2026-11-04 第十六轮）

- 实机截图显示 SC/Post 镂空左侧出现额外暗三角，右侧正常；问题来自 WebView2 对 `polygon(evenodd,…)` 多子路径挖孔的渲染，不是 EQ 网格或曲线破损。
- 移除 evenodd 挖孔，标题栏改为 `::before` 左侧、`::after` 右侧两个独立简单多边形遮罩，中央自然形成同一梯形空白。
- 四个梯形顶点仍使用 center ±192 px / ±320 px，玻璃蒙版、频率刻度、控件位置和有效 EQ 绘图区均未改变；实现更对称，也避免多子路径填充规则差异。
- 内联 JS 语法检查及 Release 编译通过，出包 `dist/Overteon_v2.0.0_2210.zip`。需实机复核左右斜边，确认暗三角已消失。

### 13.20 电平驱动玻璃背光 + 全局开关归位（2026-11-04 第十七轮）

- 上方 SC 梯形新增梅红径向背光，光源位于玻璃上缘中央并向下衰减；颜色 `rgba(255,92,138,…)`，跟随 OUT L/R 中较高的一路。
- 下方 Post 梯形新增青绿径向背光，光源位于玻璃下缘中央并向上衰减；颜色 `rgba(67,217,196,…)`，跟随 IN L/R 中较高的一路。
- 光强由现有 30 Hz meter 事件驱动，采用归一化电平平方响应与 100 ms opacity 过渡；低电平克制、高电平明显。光效仅绘制在原有梯形蒙版内，不改变布局、模糊参数或控件清晰度。
- SOLO HARM 移到 SIDECHAIN EQ 量程徽章左侧，AUTO GAIN 移到 POST EQ 量程徽章左侧；两者保持原有点击逻辑和状态指示灯。
- 底部原工具行删除，deck 只保留一排八个旋钮，原行高与 flex gap 被释放，界面更紧凑，空间自动分配给两块 EQ。
- 内联 JS 语法检查及 Release 编译通过，出包 `dist/Overteon_v2.0.0_2225.zip`。

### 13.21 跨面板曲线渐进雾化（2026-11-04 第十八轮）

- 新增透明共享画布 `#eqOverflow`，横跨 SC、中央 Title bar 与 Post；`z-index:6` 位于两块图表上方、外侧标题栏及中央 Logo/毛玻璃之下，`pointer-events:none`，不增加任何底色或有色遮挡。
- 原面板画布恢复为范围内清晰层；SC 只向下、Post 只向上绘制中央方向的越界部分，外侧边界行为不变。共享层通过各面板 `getBoundingClientRect()` 与 eqzone 的坐标差映射，窗口缩放时随现有 `redraw()` 重算。
- 主曲线与各激活频段辅助线按越界距离分为 16 级：`blur` 从 0 逐渐增加至约 6.65 px，alpha 同步非线性下降，224 px 后自然消失；线段在雾化范围处钳制，避免 Notch 等极大响应产生超长硬线。
- 不再给越界曲线画大面积填充，只让线条本身失焦，符合“曲线进入雾化玻璃”的视觉效果；中央 Logo 的现有 backdrop-filter 会进一步柔化从其下方穿过的曲线。
- 节点中心仍受原增益量程限制，但原清晰层与共享层分别绘制边界两侧，圆点和编号不再被硬切成半圆；原空白区不新建节点、节点拖拽与数值编辑逻辑不变。
- Classic/Schranz 导致面板 inactive 时，共享层同步使用 0.22 亮度；模式切换会主动刷新越界层。
- 内联 JS 语法检查及 Release 编译通过，出包 `dist/Overteon_v2.0.0_2241.zip`。需实机检查极端曲线雾化、跨面板层级及拖拽流畅度。

### 13.22 修复雾化层导致的节点拖拽跳动（2026-11-04 第十九轮）

- 实机反馈拖动节点跳来跳去、不跟随鼠标。代码路径检查确认：拖拽的坐标换算、抓取偏移和命中检测没有改动；问题来自 13.21 的同步重型重绘。一次 pointermove 中 `setScaled(fF)`、`setScaled(fG)` 的参数回调与末尾显式 `drawCurve()` 可重复触发绘制，而旧 `drawEQOverflow()` 每次都会重算两块 EQ 并执行多轮 Canvas blur，造成主线程事件积压。
- 新增 `requestEQOverflow()`：所有参数回调只设置 `_fogDirty` 并请求一次 `requestAnimationFrame`，同一帧内重复请求自动合并，不再在 pointermove/参数中继中同步执行重型 blur。
- 原共享 `#eqOverflow` 拆为独立 `#scOverflow` / `#pqOverflow` 两张透明画布；拖动 SC 只重绘 SC 越界层，Post 保留现有位图，反之亦然。模式切换和窗口尺寸变化会同时将两块标脏。
- 雾化视觉范围仍为 224 px；内部分级从 16 降为 12，曲线采样从 2 px 改为 3 px，并重新分配 blur 半径使末端仍约 6.45 px，降低 CPU/栅格化成本而不改变设计方向。
- 范围内清晰曲线、节点坐标和 mouse capture 均保持原有同步更新；只有越界雾化层延后到下一动画帧，避免视觉延迟反向阻塞拖拽事件。
- 内联 JS 语法检查及 Release 编译通过，出包 `dist/Overteon_v2.0.0_2251.zip`。需实机确认快速拖动、极端曲线及双面板切换时的跟手性。



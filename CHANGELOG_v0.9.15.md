# OnyxTEON v0.9.15 — 改动日志

记录时间：2026-11-06
上一版本：**OVERTEON v0.9.14**（WebView / HTML+CSS+JS 编辑器）
当前版本：**OnyxTEON v0.9.15**（Qt6/QML 编辑器）
工作目录：`/home/x_x/build/Onyx-z/OnyxTEON`

> **一句话版本**：DSP 一个字节没动，UI 整个换掉。
> 声音、58 个参数、`.hinj` 预设与 0.9.14 完全一致；界面从 HTML/CSS/JS + WebView
> 重写成 Qt6/QML，并且把网页里那两个 `backdrop-filter` 毛玻璃**自己算**出来——
> 因为插件宿主根本不认 `backdrop-filter`。

---

## 一、版本与产品身份

为了两个版本能在同一个宿主里并排加载，OnyxTEON 用了独立的产品身份。
**参数 ID 一个都没改**，所以预设互通。

| 项目 | OVERTEON 0.9.14 | OnyxTEON 0.9.15 |
|------|-----------------|-----------------|
| 产品名 | `Overteon` | `OnyxTEON` |
| LV2 URI | `https://eonun.com/plugins/Overteon` | `https://eonun.com/plugins/OnyxTEON` |
| bundle id | `com.eonun.overteon` | `com.eonun.onyxteon` |
| 插件代码 | `Onyx` | `Onyx` |
| bundle 名 | `Overteon.lv2` / `.vst3` | `OnyxTEON.lv2` / `.vst3` |
| 预设目录 | `~/Eonun/Overteon Presets` | `~/Eonun/OnyxTEON Presets` |
| QML import 命名空间 | — | `OnyxTEON 1.0` |
| wordmark | `OVER\|TEON` | `ONYX\|TEON` |
| 参数 ID / 数量 | 58 | **58（完全相同）** |

`project(OnyxTEON VERSION 0.9.15)`，JUCE 会把这个版本号带进
`JucePlugin_VersionString`，所以 LV2/VST3 的元数据会一起变成 0.9.15。

---

## 二、UI 架构：WebView → Qt6/QML

### 2.1 换了什么，没换什么

- **没换**：`src/FxProcessor.cpp` 的 DSP、参数表、预设读写、过采样、限幅器、
  Schranz 模式、AUTO GAIN——整个音频路径与 0.9.14 逐字节相同。
- **换掉的只有一个函数**：

```cpp
juce::AudioProcessorEditor* InjectorProcessor::createEditor()
{
   #if OVERTEON_QT_UI
    return new OvQmlEditor (*this);      // Qt6/QML
   #else
    return new InjectorWebEditor (*this); // 原来的 WebView
   #endif
}
```

`-DOVERTEON_QT_UI=OFF` 仍然能编出原来那套 WebView 编辑器，
两个前端共享同一份 DSP，随时可以对照。

### 2.2 新的目录结构

```
ui/qt/
  OvSource.h        抽象数据源（只用 std，不依赖 JUCE）
  OvJuceSource.*    接真处理器：参数、电平、FFT、预设
  OvMockSource.h    同一张参数表 + 会动的假电平（只给预览用）
  OvParam.*         一个参数 = 一个 QML 对象（norm / value / display）
  OvEngine.*        交给 QML 的 `engine` 上下文对象（门面）
  OvEqView.*        EQ 画布：RBJ 算法、网格、频谱、节点、拖动
  OvMeterView.*     顶部电平舱：四条 IN/OUT 条 + 谐波环
  OvSeamFrost.*     两个 EQ 之间的接缝毛玻璃（要同时拿到两半的画）
  OvQmlEditor.*     把 QQuickView 塞进宿主窗口的 JUCE editor
  OvQmlTypes.h      qmlRegisterType + 统一帧率常量
  qml/              界面本体（13 个 .qml，与 web_fx/index.html 一一对应）
  preview/          overteon-qml-preview：不依赖宿主的独立查看器
  res/              Inter 字体 + EDIX + 厂牌图
```

### 2.3 视觉对照表（CSS → QML）

| 设计（CSS） | QML |
|---|---|
| `:root` 颜色变量 | `OvTheme.qml`（同一批 hex） |
| `#wrap` 1040×772 | `main.qml` 里的设计项，整体等比缩放 |
| `.kring` / `.karc` / `.kndl` | `OvKnob.qml`（12 点起 270°） |
| `.ddbtn` / `.ddmenu` | `OvDropdown.qml` |
| `.tbtn` | `OvToggle.qml` |
| `.rangebadge` / `.rangemenu` | `OvRangeBadge.qml` |
| `#presetMenu` | `OvPresetButton.qml` |
| `.ctxmenu`（节点类型） | `OvBandMenu.qml` |
| `#hpod` SVG 电平舱 | `OvMeterView`（QPainter 重画） |
| `canvas` EQ 引擎 | `OvEqView`（QPainter，RBJ 同一套公式） |
| `.nodetip` | `OvEqSection.qml` 里的 `tip` |

### 2.4 嵌进宿主（这是最容易翻车的一块）

JUCE 插件从不拥有消息循环，所以 Qt 只能被"喂"。三条都必须成立：

1. **自己 map 宿主的 peer window**。`jalv.gtk3` 会交给你一个**未 map** 的
   `GtkSocket` 子窗口，而且之后也不替你 map——所以自己在 `XMapWindow` 之前先补上。
2. **不能用 `QWindow::isExposed()` 判断**。窗口被埋在未 map 的祖先下面时它照样
   返回 true。改成持续检查 X 的真实 map 状态。
3. **补完 map 之后要让视图"抖"一下**（hide/show），否则 Qt 的渲染循环拿不到
   新的 expose，画面就是死的。

另外 `ONYXTEON_CLEAR_INPUT` 可以让 Qt 窗口对输入透明，绕过某些宿主
在 `GtkSocket` 里吞按键的问题（默认关闭）。

---

## 三、毛玻璃与辉光

### 3.1 为什么不能直接用 CSS

设计里那两块玻璃是：

```css
backdrop-filter: blur(3px) saturate(.85);
```

插件宿主里跑的是 WebKitGTK / WKWebView，**`backdrop-filter` 不生效**，
所以 0.9.14 在宿主内看到的是两块平的半透明色块，不是毛玻璃。
0.9.15 两个前端都改成自己算：把玻璃下面那块内容抓下来，做真正的模糊，再贴回去。

### 3.2 算法：Dual Gaussian

用的是**可分离的两次一维卷积**（先横后竖）。二维高斯本来就等于两个一维高斯的外积，
所以这**不是近似**，代价从 `O(r²)` 降到 `O(2r)`。

算法形状参照 **X-PostProcessing-Library**（QianMo）的 `DualGaussianBlur`：
两次一维 pass、抽头权重和为 1。**没有复制任何 C# 代码**，实现是从这个数学描述重写的。

- **HTML 端**（`web_fx/index.html` 的 `FrostEngine`）：7 抽头
  （`0, ±1, ±2, ±6`，权重 `.40/.15/.10/.05`），偏移量由
  `sigma / sqrt(4.70)` 反推，因为该核的方差是 `4.70·offset²`。
  `body.frostjs` 会关掉 CSS 的 `backdrop-filter`，避免模糊两次。
- **Qt 端**（`OvEqView::gaussianBlur`）：抽头不再用固定间距，而是**按 sigma 推半径**
  （3σ 截断），并且用**双线性取样**，保证没有一次取样落在像素正中心。
  这样它才真正等于 `backdrop-filter: blur(Npx)`——固定间距的核对 3px 条纹会产生
  拍频残留，「糊了但没糊干净」。

两端都做 `saturate(.85)`，再补上 CSS 的 `.018` 白色薄纱，
最后按 `clip-path` 梯形 / `mask-image` 椭圆裁形。

### 3.3 辉光（`.controlpit::before`）

网页的写法是：

```css
background: radial-gradient(ellipse 70% 160% at 50% 0%,
            accent .36 0%, accent .13 38%, accent 0 78%);
opacity: var(--light);       /* = level²，.1s linear */
```

两个坑：**渐变的 alpha 是写死的**，只有整层 `opacity` 跟着电平走；
而且最后一个 stop 是 **78%**，不是 100%——所以中间有一段平的核心，然后提早淡出。
之前把 `level² × 0.36` 同时算进 C++ 又传进 QML，等于 `level⁴`，任何真实读数下都是 0，
所以"辉光根本不亮"。

实测跟随电平（连续 8 帧，上暖下冷）：

```
        顶部（粉）                   底部（青）
第1帧   [37.7 34.1 39.8]            [36.5 42.8 47.5]
第5帧   [42.4 35.4 42.0]            [37.3 46.6 50.7]
第8帧   [53.2 38.4 47.0]            [39.1 55.1 58.1]
```

### 3.4 验证

| 检查 | 结果 |
|---|---|
| 模糊单调性（`onyx-blur-test`） | 3px 条纹 σ=3 后对比度 119.9 → 7.06，单调下降，**PASS** |
| 与标准高斯对比（`tools/preview-server/check-frost.py`） | 最差 RMS **1.32**（参照 `GaussianBlur(3)`），**PASS** |
| 峰值保持（`onyx-blur-test`） | 保持 1.1 s 后开始回落，**PASS** |
| 接缝玻璃边缘 | 梯形边与 CSS `polygon` 逐像素吻合 |

---

## 四、EQ 编辑（原来是"根本不能用"）

### 4.1 真根因：归一化换算用了线性

`OvParam::value()` 把 norm→value 按**线性**换算，而 JUCE 用的是
`pow(norm, 1/skew)`。于是拖完读回来的频率/Q 全是错的，节点和曲线永远对不上——
看起来就是"曲线乱飞、不跟节点"。

修法：`OvSource` 增加 `valueForNorm()`，两个数据源都实现 JUCE 的约定，
`OvParam` 全部走这条路径。

### 4.2 拖动语义对齐画布

- 按下时记下**抓取偏移**，不要瞬移
- 绝对 x → log 频率；绝对 y → dB（走 `dbAt()` / `displayMult()`）
- Cut / Notch 只跟水平移动
- 读数挂在节点旁边
- 双击空白 = 激活最近的空闲节点（和网页一致）
- 双击节点 = 复位；右键 = 8 种类型的菜单；滚轮 = 调 Q（Shift 精调 0.01）

**实测跟随误差 0.0 px**，曲线峰值与节点中心重合。

---

## 五、电平表与峰值保持

### 5.1 "三角被填满"的原因

`QPainter::drawPath()` **描边同时也会填充**。谐波环留下了一个 brush，
于是每条电平路径都被从起点填到它的截断终点——就是那个"被填满的直角三角形"。
现在每次 stroke 之前都清掉 brush，条宽固定 3px，和网页一致。

### 5.2 峰值点不是线的端点

它是一条**峰值保持**（peak-hold）的拖尾。状态机与 `drawLevel()` 完全一致：

```js
if (level >= ch.p) { ch.p = level; ch.holdUntil = now + 1100; }  // 创新高，重新保持 1.1 秒
else if (now > ch.holdUntil) ch.p = Math.max(level, ch.p - dt * .18); // 之后约 10.8 dB/s 回落
```

- **一个**峰值变量 + **一个**保持计时器。之前用同一套计时器同时管电平和峰值，
  互相覆盖，所以表现诡异。
- 小圆点骑在**拖尾尖端**（保持住的峰值），不是活的电平条。
- 回落速度 0.18/s，且永远不低于当前电平。

---

## 六、性能

### 6.1 用户报的 19%

那是 `top` 的**单核**读数。这台机器 8 核，换算成全机是 19/8 ≈ 2.4%…但实测
Qt 重写后的第一版确实偏重。`tools/bench.py` 给出的对比（同一台机器、同一个进程）：

| 指标 | 优化前 | 当前 |
|---|---|---|
| live CPU | 61.0 % 单核 | **48.7 % 单核（6.1 % 全机）** |
| 帧率 | 62.5 fps | **62.5 fps**（这台 X 的上限） |
| idle CPU | 有持续重绘 | **1.0 % 单核（0.1 % 全机），0 帧** |
| 风格差异 | — | **0 px**（对比 `ref_lkg.png`，阈值 max 1–2） |

> `top` 的 %CPU 是**单核**百分比。48.7% 单核 = 6.1% 全机。
> bench 的 idle 段仍会把启动余温算进去（显示 5.9%），稳定后直接测是 1.0%、0 帧。

### 6.2 具体做了什么

| 改动 | 效果 |
|---|---|
| 只在**数据/几何真的变了**才重绘（脏标记 + 量化状态键） | 不再每帧全量重画离屏图 + 跑一遍高斯 |
| `drawPod` 的钩形路径**构造时展平一次**，按索引裁剪 | 3.55 ms/帧 → **0.77 ms/帧** |
| 去掉 `QPainterPath::pointAtPercent` 每帧 1600 次 O(n) 调用 | 同上 |
| 辉光缓存键 = 四个峰值 + 环（只有活电平在动时必命中） | 辉光几乎免费 |
| 统一时间源 `OvTiming` | 120 Hz 重绘 / 60 Hz 数据 / 60 Hz 参数轮询 |
| `now()` 改用 `steady_clock` | 之前用 `std::clock()`（进程 CPU 时间），**动画速度取决于自己吃多少 CPU** |

### 6.3 关于 "120 fps"

这台机器的 `:3`（Xephyr）一个空窗口就只有 62.5 fps，`:0` 更是**没有
vblank 计数器**（只有 `IDLETIME` / `SERVERTIME`）。实测：

```
glXSwapBuffers(interval 1) = 904.89 ms/帧   ← 就是那个"冻结"
glXSwapBuffers(interval 0) =   0.02 ms/帧
glxgears: 1.279 fps  vs  33 755 fps
```

**所以那个"冻结"从来不是 Qt 也不是插件，是 X 的 vsync。** 预览程序现在会
检测 `hasVblankCounter()`，没有就自动 `QSG_NO_VSYNC=1` 并打印原因。
装在有正常 vblank 的机器上就跑满屏。

---

## 七、这一轮修掉的真 bug

每一条都有实测，不是"应该好了"。

| # | 现象 | 真根因 | 怎么验证的 |
|---|---|---|---|
| 1 | 拖 EQ 节点时刷屏 `Cannot assign to non-existent property "text"`，界面像卡死 | `onHoverInfo` 往 `tip.text` 赋值，但 `tip` 是外框 `Rectangle`，标签是它里面的 `tipText`。每次鼠标移动抛一个 JS 异常，**中断了handler 后半段**，还同步写 stderr | 真实 X 拖拽：错误数 从每次拖拽上百条 → **0** |
| 2 | 节点**完全抓不住** | `plotLayer` 上有 `layer.enabled: true`。分层项把子项渲染进离屏纹理，Qt 就**不再给子项派发鼠标事件** | 冻结渲染上做真实点击（唯一会变的就是拖动本身）：节点 y=172 → 140 |
| 3 | 点空白却把节点 2 拖走了 | `mouseMoveEvent` 只看 `m_dragBand < 0`，**没检查按键是否真的按着**。一次丢失的 release 就让它一直拖 | `ONYXTEON_DRAGTEST2` 双向：无按键移动 0 个节点移动 / 真拖 250.0 → 180.4 Hz，**PASS** |
| 4 | UI 冻结在 1 fps | 无 vblank 时 Qt Quick 渲染循环等不到帧回调 | 见 6.3 的 glXSwapBuffers 实测 |
| 5 | 动画速度随 CPU 占用变化 | `std::clock()` 返回**进程 CPU 时间**，不是墙钟 | 换 `steady_clock` |
| 6 | 工具提示框在，但**没有字** | 同 #1：异常发生在 `tipText.text = …` 之前/之后，位置和内容都没更新 | 修后实测 457 个字形像素，y 146…153，肉眼可读 |
| 7 | 窗口大小不跟屏幕走 | 默认尺寸在 `QApplication` 构造**之前**算，`primaryScreen()` 返回 null，于是永远是 840×624 | 现在打印：`screen 1951x1183 dpr 1.302 logicalDpi 96.0, window 1434x1065` |
| 8 | 缩放后字比框大 / 比框小 | `font.pointSize` 是 DPI 相关的，而所有矩形都是逻辑像素 | A/B 实测：dpr 1.302 下 `logicalDotsPerInch()` 仍是 **96.0**，Qt 会整体缩放（含字体）→ 换算在 0.95–1.05 死区外才启用，普通机器保持 CSS 原尺寸 |
| 9 | 宿主里界面被裁 / 宿主窗口不跟着缩 | 编辑器没把真实缩放尺寸告诉宿主 | `fitToHost()` 读容器 X 几何，保持 1040×772 比例后 `setSize()`，JUCE 转成 `ui:resize`，jalv 自己缩窗口 |
| 10 | LV2 编不出来（切到 Qt 编辑器之后） | 三个原因见第八节 | `libOnyxTEON.so` 7.9 MB，jalv 能加载并内嵌 |

> 这里有个方法论教训值得写下来：**合成事件（`sendEvent` / `QTest`）会绕开命中测试**，
> 它能"通过"却掩盖了 #2 这种真实点击才有的问题。所有涉及可交互性的结论，
> 都改用真实 X 点击验证（`xdotool` + 嵌套 Xephyr）。

---

## 八、构建系统

- 统一 **CMake + Ninja**，`cmake -S . -B build -G Ninja && ninja -C build -j$(nproc)`。
- `-DOVERTEON_QT_UI=ON|OFF` 切换 Qt 编辑器 / WebView 编辑器（默认 ON）。
- Qt 资源（QML + 字体）**编进每一个 plugin wrapper**。共享代码目标是静态库，
  没人引用 rcc 生成的对象，链接器会直接丢掉——这是必踩的坑。
- 每个目标单独开 `AUTOMOC` / `AUTORCC`（`juce_add_plugin` 之前设的全局变量不管用）。
- Linux 额外链 `X11` + `Xext`（窗口 map 修补、可选的 input shape）。
- 构建目标：
  `overteon-qml-preview`、`HarmonicInjector_LV2`、`HarmonicInjector_VST3`、
  `HarmonicInjector_Standalone`、`onyx-blur-test`。

### 让 LV2 在 Qt 编辑器下重新编起来的三个条件

1. `src/FxWebEditor.cpp` 实现的是 JUCE `WebBrowserComponent` 编辑器；
   开了 Qt 编辑器就不存在这个类。把它标成 `HEADER_FILE_ONLY`——
   文件还留在源列表里（IDE / WebView 构建仍能看到），但没人编译它。
2. `JUCE_WEB_BROWSER` 原来是**写死 1**，会把 `juce_gui_extra` 的
   gtk / webkit2gtk 头文件拖进来，缺 `webkit2gtk-4.1` 开发包就编不过。
   现在它跟随 `OVERTEON_QT_UI`。
3. `option(OVERTEON_QT_UI)` 原来**声明在第一次使用之后**，上面的排除逻辑会读到空变量。
   已提到最前面。

外加 `OvQmlEditor.cpp` 缺 `#include <QQmlContext>`（`QQuickView` 只做前向声明）。

---

## 九、Windows / wine

Qt 前端只依赖 Qt，所以**能**交叉编译；JUCE 插件本体不能
（`juce_TargetPlatform.h` 在 MinGW 下直接报错，不值得往 `juce_core` 里打补丁）。

- `cmake/toolchain-clangcl-xwin.cmake`：clang-cl + lld-link，
  对着 xwin 抓下来的 MSVC SDK/CRT；钉死 `MultiThreadedDLL` / Release
  （xwin 那套没有 debug CRT）；`QT_HOST_PATH` 指向同版本的原生 Qt
  （moc / rcc 要在构建机上跑）。
- `cmake/toolchain-mingw64.cmake`：MinGW 的尝试，留档。
- `tools/deploy-win.sh`：像 windeployqt 那样组装 `dist-win/`，但用 shell 写，
  在 Linux 上也能跑——所有 Qt6*.dll、`plugins/{platforms,imageformats,styles,
  iconengines,general,networkinformation}`、整个 qml 树、`qt.conf`、MSVC 运行库。
- wine 下两个坑，都是隔离实验找出来的：
  1. Qt6 在 Windows 上默认选 **D3D11 RHI**，wine 的 DXGI 开不出输出
     （`dxgi_device_init Failed to get output`），全黑。
     现在 `ntdll!wine_get_version` 检测到 wine 就设 `QSG_RHI_BACKEND=opengl`。
  2. Qt Quick 自己的窗口在 wine 下**不 present**（场景在跑，窗口停在第 1 帧）。
     实测：普通 `QWidget` 加 50ms 重绘定时器完全正常；`QQuickView` 窗口不动，
     但 `grabWindow()` 在 400ms 和 1600ms 差了 4800 px——说明场景图在跑，
     坏的只有"上屏"。
     现在的 `--compat` 走 `QQuickWidget`（`QQuickRenderControl` → FBO →
     走 QWidget 的绘制路径），完全不涉及窗口 present。

> **注意**：`--compat` 只在 wine 下需要，原生 Linux 不要开。
> 更不要强制 `QSG_RENDER_LOOP=basic`——那一条就能把 Qt Quick 钉死在 1 fps。

---

## 十、工具

| 工具 | 作用 |
|---|---|
| `tools/demo.sh` | 按 **PID** 起停 demo（`setid` + pidfile + pgid kill，**从不 pkill**）。`jalv` / `preview` / `jack` / `wine` / `all`，各有 `start|stop|status` |
| `tools/bench.py` | 三件事：像素差（风格回归）、帧率、CPU（**单核 + 全机都给**）。窗口按 `_NET_WM_PID` 匹配；抓不到就说"未知"，不会假装"冻结" |
| `tools/compare-ui.py` | 与网页做**地标**对比（旋钮圆心、梯形边、网格线、文字段），逐项报偏移，而不是一整张图求差 |
| `tools/preview-server`（vibe.d） | 在 `0.0.0.0:8888` 同时提供 `/original`（0.9.14 原版）和 `/current`，带 mock JUCE 后端，方便并排看 |
| `check-frost.py` | 玻璃 vs 参照 `GaussianBlur(3)` 的 RMS |
| `onyx-blur-test` | 无头断言：模糊核单调性 + 峰值保持状态机 |
| `ONYXTEON_DRAGTEST2` | 进程内自己驱动一次真实拖动（双向断言） |

`overteon-qml-preview` 的参数：

```
--size 1040x772      指定窗口
--shot out.png       截图
--shot-delay 2500    等多久再截
--freeze             mock 数据冻结（做像素对比用）
--set id=value       preset 式改参数，可叠加
--compat / --no-compat   wine 的 QQuickWidget 路径
--qml file           临时换一份 QML
```

---

## 十一、兼容性

- **参数**：58 个，ID 与 0.9.14 完全相同 → `.hinj` 预设双向可读。
- **DSP**：未改动。过采样档位、限幅器、Schranz、AUTO GAIN 行为与 0.9.14 一致。
- **过采样是全局偏好**，不进预设（沿用 0.9.14 的三处保护）。
- **宿主**：LV2 / VST3 / Standalone 都保留；jalv（gtk3 / qt5 / qt6）实测可内嵌。
- **并排安装**：OnyxTEON 与 OVERTEON 的 URI / bundle id 不同，可同时装。

---

## 十二、已知限制 / 未闭环

1. **帧率上限取决于显示器**。这台机器的 `:0` 没有 vblank 计数器，
   `:3`（Xephyr 空窗口）上限 62.5 fps。"跑满 120"只能在有正常 vblank 的桌面上验。
2. **`compare-ui.py` 的剩余差异是动画状态**，不是布局。
   网页启动时 `secSC` 带 `inactive` 类（`.eqsec.inactive .eqcanvas-wrap{opacity:.22}`），
   要对齐必须用 `--set driveMode=0` 跑 QML，否则整个 SC 区看起来"不对"。
   要做干净的一次像素对比，需要两边同时冻结在同一组电平上——还没做。
3. **`ui/qt/README.md` 仍在描述已删除的 `OvGlass.qml`（shader 方案）**，
   实际现在是 `OvEqView::paintFrost` + `gaussianBlur`（QPainter 方案）。文档待更新。
4. `web_fx/index.html` 里 `.frostcanvas` 上面那条 CSS 注释还写着 "dual-Kawase"，
   实现已经是 dual-Gaussian。纯注释问题，不影响行为。
5. wine 下的表现只做到"能看能交互"，没做性能基线。

---

## 十三、改动文件总表

**新增（Qt 前端）**

```
ui/qt/OvSource.h  OvEngine.*  OvParam.*  OvJuceSource.*  OvMockSource.h
ui/qt/OvEqView.*  OvMeterView.*  OvSeamFrost.*  OvQmlEditor.*  OvQmlTypes.h
ui/qt/ov_ui.qrc   ui/qt/preview/main.cpp   ui/qt/README.md
ui/qt/qml/{main,OvAbout,OvBandMenu,OvDeck,OvDropdown,OvEqSection,OvHeader,
           OvKnob,OvMenu,OvPresetButton,OvRangeBadge,OvTheme,OvToggle}.qml + qmldir
ui/qt/res/{edix.otf, eonun_logo.png, Inter-*.ttf, Inter-LICENSE.txt}
```

**新增（工具 / 交叉编译）**

```
tools/bench.py  tools/compare-ui.py  tools/render-diff.py  tools/deploy-win.sh
tools/units/blur_test.cpp  tools/preview-server/check-frost.py
cmake/toolchain-clangcl-xwin.cmake  cmake/toolchain-mingw64.cmake  cmake/README-cross.md
```

**修改**

| 文件 | 改动 |
|---|---|
| `CMakeLists.txt` | 112 → 292 行：Qt6 目标、`OVERTEON_QT_UI`、交叉编译、AUTOMOC/AUTORCC、X11 |
| `src/FxProcessor.cpp` | 815 → 824 行：**只有 `createEditor()` 按开关二选一**，DSP 未动 |
| `web_fx/index.html` | 2025 → 2372 行：`FrostEngine`（真模糊）+ 三块 `.frostcanvas` |
| `README.md` | 改成 OnyxTEON 的定位说明 |
| `tools/demo.sh` | 加 `wine` 目标（PID 管理） |

---

## 十四、构建与运行

```sh
# 配置（首次或改了 CMakeLists.txt）
cmake -S . -B build -G Ninja

# 编译（全量）
ninja -C build -j$(nproc)

# 只有 Qt 编辑器（不编 JUCE，交叉编译也用这个）
cmake -S . -B build -G Ninja -DOVERTEON_PREVIEW_ONLY=ON
ninja -C build -j$(nproc) overteon-qml-preview

# 回到原版 WebView 编辑器
cmake -S . -B build -G Ninja -DOVERTEON_QT_UI=OFF
```

```sh
# 跑预览
./build/overteon-qml-preview --size 1040x772

# 基准（像素差 / 帧率 / CPU）
./tools/bench.py --display :0 --ref /tmp/ref_lkg.png

# 在 jalv 里验内嵌（按 PID 起停，别用 pkill）
./tools/demo.sh jalv start
./tools/demo.sh jalv status
./tools/demo.sh jalv stop
```

**验收基线**（本轮结束时的实测值）：

```
style   同一构建渲染两次        0 px 差（阈值 max 2）
style   对比 ref_lkg.png        0 px 差（阈值 max 2）
live    CPU                    48.7 % 单核 = 6.1 % 全机（8 核）
live    fps                    62.5（这台 X 的上限）
idle    帧数                    0
idle    CPU                    1.0 % 单核 = 0.1 % 全机
插件    libOnyxTEON.so         7.9 MB，jalv 加载 + 内嵌 OK
```

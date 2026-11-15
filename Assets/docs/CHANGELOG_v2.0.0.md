# Overteon v2.0.0 — 改动日志

记录时间：2026-11-14
上一版：**v0.9.16**（Qt6 编辑器，产物名 OnyxTEON）
当前版本：**Overteon v2.0.0**
工作目录：`/home/x_x/build/Onyx-z/OnyxTEON`

> **一句话**：编辑器换成了 Qt6/QML，所有自绘内容跑在 RHI 场景图上；
> DSP、58 个参数、`.hinj` 预设、产品身份全部保持原样。
> 这一版把名字改回 **Overteon**、把版本定为 2.0.0，并修掉了在
> Ardour / Mixbus 里"一加载就炸"的崩溃。

规模：`v0.9.15` 之后的 71 个提交；相对最初导入 86 个提交，1826 个文件，+142,616 行。

---

## 一、本版要点（相对 0.9.16）

### 1. 名字回到 Overteon

开发期曾用独立的 `OnyxTEON` 身份，好让新旧两个编辑器能在同一个宿主里并排加载。
这个身份**取消了** —— 这是本项目，不是第二个插件，所以沿用原版那一套：

| 项目 | 开发期 | **2.0.0** |
|------|--------|-----------|
| 产品名 | OnyxTEON | **Overteon** |
| LV2 URI | `.../plugins/OnyxTEON` | `.../plugins/Overteon` |
| bundle id | `com.eonun.onyxteon` | `com.eonun.overteon` |
| 插件代码 | `Onyx` | **`Ovtn`** |
| bundle | `OnyxTEON.lv2` / `.vst3` | `Overteon.lv2` / `Overteon.vst3` |
| 动态库 | `libOnyxTEON.so` | `libOverteon.so` |
| 预设目录 | `OnyxTEON Presets` | `Overteon Presets` |
| QML 命名空间 | `OnyxTEON 1.0` | `Overteon 1.0` |
| wordmark | `ONYX\|TEON` | **`OVER\|TEON`** |
| 环境变量前缀 | `ONYXTEON_*` | `OVERTEON_*`（48 个开关） |

**后果**：身份回到原版那一套之后，这个构建和原版 WebView 构建**不能并排安装**，
二选一。这正是当初做独立身份的唯一理由。

### 2. 版本号 2.0.0

`project(Overteon VERSION 2.0.0)`。版本字符串现在是**单一来源**：
CMake 定义 `OVERTEON_VERSION`，QML 通过 `ovVersion` 上下文属性读取。
About 面板之前硬编码 `v0.9.14`，落后了三个版本 —— 这类漂移不会再发生。

### 3. 界面不再标注自己是怎么做的

About 面板里的 `COPY QML UI · EONUN` 改成 `COPY EONUN`。
UI 是产品，不该在界面上展示实现技术。全项目复查：界面里已无任何
`QML` / `Qt` / RHI 之类的字样（源码注释保留，那不是界面）。

### 4. Ardour / Mixbus 里"一加载就炸"（真 bug，已修）

用户报的现象：Ardour 加载 LV2 就崩。`coredumpctl` 拿到的栈是决定性的：

```
#0  0x0000004000000000         <- 跳到非法地址
#1  QCryptographicHash::QCryptographicHash
#2  QV4::CompiledData::CompilationUnit::localCacheFilePath
#3  loadFromDisk                （线程 QQmlThread）
```

**根因不在我们的代码。** Ardour 9 / Mixbus 12 自带的
`lib/LV2/Harrison.lv2/` 下 20 个插件都静态链接了一个老 OpenSSL 并默认可见地导出：

```
nm -D --defined-only XT_EQ.so  →  00000000000b8090 T EVP_DigestInit_ex
```

它们在会话加载时就进了进程的**全局符号域**。`libcrypto` 是后来才随
`libQt6Core` 进来的，glibc 优先搜全局域，于是 Qt 的所有 OpenSSL 重定位都绑到了
Harrison 那一份。Qt 先调 `EVP_MD_fetch()`（3.x/4.x 接口，Harrison 没导出，绑到真库）
拿到一个 `EVP_MD`，再交给 Harrison 那个 1.1 时代的 `EVP_DigestInit_ex`
—— 它按自己的结构体去读，读出垃圾指针。

所以崩点必然落在 Qt 第一次编译 QML 的时候，也就是**编辑器打开的那一瞬**。

**修法**：新增 `ui/qt/OvCryptoShim.cpp` —— `ovqt::bindQtToRealLibcrypto()` 读
`libQt6Core` 自己的重定位表，找出那 14 个 libcrypto 导入，把**它本该链接的那个**
libcrypto 里的地址写回槽位（先 `mprotect` 开写，RELRO 已置只读）。
干净宿主里每个槽位本来就对，函数一个字都不写。

排除了三个看似可行的方案：自带 libcrypto（全局域照样赢）、要求宿主 RTLD_LOCAL
（写了作用域测试，全局定义对依赖内部的重定位仍然优先）、自己导出正确的 `EVP_*`
（同一顺序问题）。另外 `QML_DISABLE_DISK_CACHE=1` **不管用** —— Qt 每次编译都哈希，
崩溃只是挪到 `generateUnitChecksum`。

**验证**：用真实 `XT_EQ.so` 抢先加载，未修复版 SIGSEGV 且栈逐帧一致，修复版
`4 of 14` 重绑、窗口正常；Ardour 9.8 里把 XT-EQ 放在 Overteon 之前也正常。

### 5. 预览窗口跟随构建开关

`overteon-qml-preview` 是独立的 `QQuickView`，JUCE 的 `setResizable` 到不了它，
原先能拖成任意形状 —— 但 `main.qml` 是**等比**铺满框架的，换成别的形状只会多出
一圈空白。现在由 `OVERTEON_RESIZABLE_WINDOW` 统一管：

| 构建 | 预览窗口 |
|------|----------|
| 默认（OFF） | 固定在设计比例（`min == max`，没有可拖的边） |
| `-DOVERTEON_RESIZABLE_WINDOW=ON` | 可拖，但比例锁死 |

---

## 二、编辑器：WebView → Qt6/QML（0.9.15 起）

- 独立渲染，不再需要 QtWebEngine，不再往 JS 里塞桥
- 网页里那两个 `backdrop-filter` 毛玻璃是**自己算**的 —— 插件宿主不认这个属性

### 渲染必须走 RHI（硬约束）

Qt 6 **没有** RHI 版的 QPainter 引擎（只有 `raster` 和 `QOpenGL2PaintEngineEx`），
所以"用 QPainter 画到 GPU 上"永远等于强制 OpenGL；`QQuickFramebufferObject` 也是
OpenGL-only。目标用户是 Win/Mac，因此所有自绘内容改用场景图：

- `QSGImageNode` 承接缓存图层（文字、渐变底、辉光贴图），只在关键参数变化时重绘
- `QSGGeometryNode` 承接动态几何，渐变变成逐顶点颜色

没有 `QPainter`、没有 `QQuickFramebufferObject`、没有 X11/HWND/NSView。

期间踩到并记录的三个坑：`QSGSimpleTextureNode` 在
`QSGOpaqueTextureMaterial::compare` 里首帧崩；零顶点的 `QSGGeometryNode` 同样崩；
`QSGImageNode` 不拥有纹理，必须晚一帧 release。

### 毛玻璃与辉光

严格复刻原网页 FrostEngine：半分辨率、7 抽头双高斯。CPU 与 GPU 在同一张照片上
误差 2/255。帧成本从单核 91 % 降到 37 %。

### EQ 编辑

- 命中半径统一 `kNodeHitRadius = 12.0`
- 滚轮调 Q 有 1.2 s 抓取锁定，避免扫过一个点就把邻居的 Q 改掉
- 频率轴是 `timeRange(5, 20000, 800)`（`setSkewForCentre 800`，skew ≈ 0.214936），
  **不是纯对数** —— 按对数实现时低频段手感完全不同

### 电平表与频谱

- 所有表走同一条曲线：时间用 smootherstep，轴用 Catmull-Rom
- 真正的 RMS 检波器，以及它蕴含的回落速度（11.8 dB/s，不是 57）
- 分析器在功率域平均，半倍频程宽，16.6 dB/s
- 频谱两端修掉两个真 bug：20 Hz 左侧"硬切面"（不再凭空补斜坡到地面线）、
  填充在轴的两端都锚到绘图区底边

### 预览宿主真实 DSP

频谱和电平表从"照着网页行为模拟"改成**测出来的** —— 预览与插件共用同一份 DSP，
所以"界面看起来对、插件里不对"这类问题现在能被发现。

### 数值编辑窗

1:1 复刻 WebUI 双击频点弹出的数值窗。实测灵敏度与原网页一致：
dB 轴 **0.1199 dB/px**（网页 0.12），频率轴 16 px → Δnorm 0.0800。
修掉的三个真 bug：QML 只读属性被当可写用（`Cannot assign to read-only property
"text"`）、dB 字段拖动是反馈环、拖字段时框不跟随节点。

### 与原版 WebUI 的逐项审计

两轮审计分别修正五处视觉漂移、四处交互差异。一个必须先解决的前提：原网页只嵌
EDIX 一种字体、`Inter` 靠系统安装；测试机没装 Inter 时整页 **9.43 %** 像素不同，
所以在那之前所有"文字不一样"的结论都是字体差异，不是移植错误。

### 编辑器尺寸、DPI 与宿主嵌入

- 设计尺寸要**乘上**桌面缩放，子控件与之一致
- Ardour 里能加载并填满宿主给的缩放后框架（根因是替身 shell 少了 15 个 glib
  符号，见 `ui/qt/OvGlibShim.cpp`）
- HiDPI 上均衡图被缩放两次的 bug 已修

---

## 三、测试通道 `ovctl`

```sh
./build/overteon-qml-preview --size 1040x772 --preset /tmp/flat.hinj --ctl ovctl
./build/ovctl --name ovctl set sc1Gain 18 --tau 0.4
./build/ovctl --name ovctl sweep pq1Gain -12 18 --time 3 --step 0.03
```

`attach()` 而非创建（创建会清空共享块），命令之间不互相擦除。预览按 8 ms 一步
指数逼近目标值，界面看到的是拖动而不是跳变 —— 这样测出来的帧率和手感才有意义。

---

## 四、构建

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build

# 只有 Qt 编辑器（不编 JUCE，交叉编译也用这个）
cmake -S . -B build -G Ninja -DOVERTEON_PREVIEW_ONLY=ON

# 允许宿主拉伸编辑器（默认关）
cmake -S . -B build -G Ninja -DOVERTEON_RESIZABLE_WINDOW=ON
```

`OVERTEON_RESIZABLE_WINDOW` 是**编译期**开关，不是运行时环境变量 —— 固定构建
不可能被任何东西拉开，这正是把它做成编译开关的意义。两态的可查差别在生成的
`Overteon.lv2/ui.ttl`：默认写 `lv2:optionalFeature ui:noUserResize`，打开后写
`ui:resize`（VST3 侧对应 `IPlugView::canResize()`）。

产物：

```
build/HarmonicInjector_artefacts/Release/LV2/Overteon.lv2/libOverteon.so
build/HarmonicInjector_artefacts/Release/VST3/Overteon.vst3/Contents/x86_64-linux/Overteon.so
build/HarmonicInjector_artefacts/Release/VST/libOverteon.so
```

安装（VST2 的文件名必须是 `<Name>.so`，不能是 `libOverteon.so`）：

```sh
cp -r build/.../LV2/Overteon.lv2    ~/.lv2/
cp -r build/.../VST3/Overteon.vst3  ~/.vst3/
cp    build/.../VST/libOverteon.so  ~/.vst/Overteon.so
```

---

## 五、兼容性

| 项目 | 状态 |
|------|------|
| `.hinj` 预设 | 双向互通 |
| 参数数量 | 58，未变 |
| DSP | 未改动 |
| 产品身份 | 与原版一致（工程可直接打开，无需重映射） |
| LV2 / VST3 / VST2 / Standalone | 均已构建 |
| 宿主 | Ardour 9.8、Mixbus 12 已验证；jalv 全流程可跑 |
| 显示 | 高 DPI 与普通 DPI 均按设备像素渲染 |
| 平台 | Linux（已验证）、Windows（wine 中已验证）、macOS（未测） |

---

## 六、已知限制

1. **中央表头 `OvMeterView` 仍是 `QQuickPaintedItem`。** 光晕在 `paint()` 里构建，
   完整 RHI 迁移试过一次已回退。这是唯一还留在 CPU 光栅路径上的自绘控件。

2. **macOS 完全未测试。**

3. **`OVERTEON_RESIZABLE_WINDOW=ON` 的拉伸行为未在真实宿主里逐项跑过。**
   已验证开关翻转产物、`ui.ttl` 按预期改变、两种构建都能加载出界面；
   `jalv.gtk3` 不读那个声明，所以拿它的 `WM_NORMAL_HINTS` 验不出来。

4. **AUTORCC 偶发不重建。** 改了 QML 之后 ninja 有时认为无需重跑 rcc，
   二进制里还是旧界面。判断同步要比哈希或者 `touch` 一下 QML。

5. **`console.log` / `console.warn` 在 release 构建里不输出**，不能当证据。
   可靠手段只有 `[hit]` 追踪、C++ 侧 `fprintf`、把状态渲染进控件再截图。

---

## 七、DSP 层历史

DSP 的增益量程四档（`scRange` / `pqRange`）、过采样 2x–32x 等改动记录在
`CHANGELOG_v2.0.0-dsp-webview-archive.md`（WebView 时代的 2.0.0 草稿，
编号后来重排过，内容仍有效）。更早的版本见 `CHANGELOG_v0.9.0.md`、
`CHANGELOG_v0.9.15.md`、`CHANGELOG_v0.9.16.md`。

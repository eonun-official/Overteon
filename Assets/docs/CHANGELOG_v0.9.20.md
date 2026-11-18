# Overteon v0.9.20 — 改动日志

记录时间：2026-11-16
上一版：**v0.9.16**
当前版本：**Overteon v0.9.20**
工作目录：`/home/x_x/build/Onyx-z/OnyxTEON`

> **版本号说明**：之前那份文档把头衔写成 2.0.0，但 2.0.0 这个编号属于
> WebView 时代的旧记录（见 `CHANGELOG_v2.0.0-dsp-webview-archive.md`），
> 早已被 0.9.15 / 0.9.16 取代。这是 0.9.x 线的延续，所以定版 0.9.20。

> **一句话**：编辑器换成了 Qt6/QML，所有自绘内容跑在 RHI 场景图上；
> DSP、58 个参数、`.hinj` 预设、产品身份全部保持原样。
> 本版把名字改回 **Overteon**，修掉了在 Ardour / Mixbus 里"一加载就炸"
> 的崩溃，并补齐了全局控制与显示设置。

规模：`v0.9.15` 之后的 71 个提交；相对最初导入 86 个提交，1826 个文件，+142,616 行。

---

## 一、本版要点（相对 0.9.16）

### 1. 名字回到 Overteon

开发期曾用独立的 `OnyxTEON` 身份，好让新旧两个编辑器能在同一个宿主里并排加载。
这个身份**取消了** —— 这是本项目，不是第二个插件，所以沿用原版那一套：

| 项目 | 开发期 | **0.9.20** |
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

### 2. 版本号 0.9.20

`project(Overteon VERSION 0.9.20)`。版本字符串现在是**单一来源**：
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

### 6. 全局控制（表头右侧）

| 控件 | 行为 |
|------|------|
| **BYPASS** | 真正的 bool 参数，宿主能看到、能自动化；同时经 `getBypassParameter()` 交给宿主，所以宿主自己的 bypass 按钮和这里同一个开关 |
| **A / B** | **两个独立按钮，同时常显**（不是按一个键来回切）。左键选中，右键把**该槽**复制到另一槽 |
| 琥珀下划线 | bypass 生效时，颜色之外再加一条线——单靠颜色在这个尺寸不够 |

bypass 在 `processBlock` 最前面短路，但**表头继续显示输入电平**：旁通时进出
是同一个信号，冻住的表会显示一个已经不再送出的电平。

A/B 快照存的是**归一化值**，并且通过宿主参数接口带手势读写——这样一次召回是
真实编辑，而不是偷偷换状态。切换时先记录要离开的槽，所以来回切是**比较**而不是
单向棘轮。`abCopyFrom` 显式指定来源，因为两个按钮都可能被右键。

### 7. 显示设置（点击中央谐波环）

悬停时环变绿、百分比换成 `SETTINGS`，点击打开面板。**全部是显示设置**，
不碰一个采样点——所以它们是属性而不是参数（参数会出现在宿主自动化列表里，
等于宣称自己属于声音）。

三个表**各自**配置：

| 设置 | 选项 |
|------|------|
| Detector | `RMS` / `TRUE PEAK` |
| Interpolation | `SMOOTH` / `NONE` |

- **TRUE PEAK** 是 BS.1770-4 真峰值：4 相位、32 抽头窗函数 sinc 过采样，
  历史**跨块保留**——否则跨块边界的采样点峰值永远找不到。
- 谐波环的 Peak 读 DSP 里**未平滑**的比值（`meterHarmPk`）：这个表量的是
  两块能量之比，不是波形，对它做过采样没有意义。
- 频谱的 Peak 取**频带内最响的 bin**，不是频带均值：半倍频程的均值会把
  窄音调平均掉。
- 三个表独立，因为没有理由让一个表的读数方式强制另外两个。

### 8. 单带 On/Off

每带一个 `On` 参数（`sc1On` … `pq5On`），**追加在所有既有参数之后**。

追加顺序不是随意的：参数索引是插件接口的一部分，把这 10 个插在每带的
`Type` 旁边，会让 `scRange`、`pqRange`、`protectMode`、`limiterOs` 的索引
整体后移，任何基于索引的宿主自动化都会被静默改指向。

muted 带的 biquad 变成**恒等**（`b0=1`，其余 0），而不是从链路里拿掉：
拿掉会让状态变陈旧，重新打开时会响一下。画面上 muted 节点画成空心。

### 9. EQ 交互

- **悬停节点**直接就地打开数值编辑框（原来那行"#2 Bell · 1.2k · +3.0 dB"
  的简略提示取消）——它告诉你这个带是什么，然后还要你双击才能改，多一步。
- **双击节点**切换该带的 `On`。双击原来是打开编辑框，现在悬停已经打开了，
  再打开一次是空操作，正好把双击解放给一个以前做不到的动作。
- About 面板加上 **Xyas**。

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

0. **EQ 悬停编辑框未在最后一次布局改动后复验。**
   悬停节点打开数值框的路径（`hoverBandChanged` → `nodeEdit.open`）接好了、
   编译通过，但最后一次改完表头之后没有重新确认"指针真正进入节点命中半径
   时框会打开"。这一项**不作"已完成"声称**。其余三项（全局控制、logo 提示、
   显示设置）都有像素统计或 C++ 追踪作证。

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

---

## 八、Windows x64 VST3 静态单文件适配记录（2026-11-16）

本节记录本次 Windows 原生构建、Qt/QML 静态整合和编辑器尺寸修正。
完整的环境准备、命令、目录说明及故障排查见同目录的 `BUILD_Windows.md`。
本次补写文档没有重新生成或改动当前 VST3 产物，也没有迁移、删除既有预设。

### 1. 最终结果

- 平台：Windows x64，MSVC Release，JUCE 8.0.13，Qt 6.9.3。
- Qt Core / Gui / Qml / Quick、QML 插件、Windows 平台插件和 MSVC CRT 全部静态链接。
- QML、字体及 `.qsb` shader 继续通过 qrc 编译进二进制。
- 最终宿主加载的 VST3 二进制不依赖额外 Qt DLL、`OverteonCore.dll`、加载壳、
  VC++ 运行库 DLL、系统 Qt 安装或 Qt 环境变量。
- JUCE 的标准构建输出仍位于
  `Build/win-static/Overteon_artefacts/Release/VST3/Overteon.vst3/`；
  当时交付的独立二进制为 `Build/dist-win/VST3/Overteon.vst3`。
- 独立二进制大小为 **35,494,912 字节**，SHA256 为
  `DB05C22B25BA30DDB4BDEAD84FBFB9F5606D5600B9CC9249E4792E2D85138280`。
- 当时安装到 `C:/Program Files/Common Files/VST3/Overteon.vst3` 的文件与交付文件哈希一致。

### 2. 源码与构建系统修改

| 文件 | 修改内容 |
|------|----------|
| `Config/CMakeLists.txt` | 将 Windows 的 `qsb` 调用改为 `Qt6::qsb`；限制 `OvGlibShim.cpp` 仅在 Unix 非 Apple 平台编译；静态 Qt 下为共享代码和各格式目标统一 `/MT`；显式链接 Windows/QML 静态插件；加载壳只在动态 Qt 构建中生成。 |
| `Source/ui/qt/OvQmlEditor.cpp` | 使用 `Q_IMPORT_QML_PLUGIN` 和 `Q_IMPORT_PLUGIN` 保留静态插件；Windows 的 JUCE 请求尺寸不再重复乘 DPR；动态 Qt 的 bundle 路径兼容逻辑保留。 |
| `Source/ui/qt/OvQmlEditor.h` | 修正 `targetSize()` 的单位说明，不再把 Windows 逻辑像素描述为设备像素。 |
| `Source/ui/qt/OvQtComponent.cpp` | Windows 下读取宿主原生客户区，以物理像素除以 Qt DPR 得到 QWindow 的 DIP 几何，修复内容贴左上、右侧和下方留白；将变量 `far` 改名为 `bottomRight`，避开 Windows 宏。 |
| `Source/src/OvVst3Bootstrap.cpp` | 动态 Qt 阶段增加的无 Qt 依赖 VST3 加载壳。最终静态构建不编译、不交付该壳。 |

DSP、参数 ID、插件身份和 `.hinj` 文件格式未因本次 Windows 封装而改变。

### 3. 静态 Qt 与 Overteon 构建

Qt 源码安装到 `C:/Qt/6.9.3/Src`，静态安装前缀为 `Build/qt-static`，
构建目录为 `Build/qt-static-build`。实际配置使用：

```text
-static -static-runtime -release -opensource -confirm-license
-nomake examples -nomake tests
-opengl dynamic -no-openssl
-no-feature-quickcontrols2-basic
-no-feature-quickcontrols2-fluentwinui3
-submodules qtbase,qtshadertools,qtdeclarative,qtsvg
```

Overteon 使用 `Build/win-static`，并通过
`-DCMAKE_PREFIX_PATH=<项目>/Build/qt-static` 和
`-DOVERTEON_FORMATS=VST3;Standalone` 配置。
静态 Qt 下 `QT6_IS_SHARED_LIBS_BUILD` 为假，因此不会进入动态加载壳分支，
格式目标直接生成宿主加载的 `Overteon.vst3`。

### 4. 已完成的独立验证

- 清空 `QT_*` / `QML2_*` 环境变量，并将 `PATH` 限制为 `C:/Windows/System32` 后：
  `InitDll = True`，`FactoryClasses = 2`，加载进程中的 Qt DLL 数量为 0。
- `dumpbin /DEPENDENTS` 未列出 Qt、`MSVCP` 或 `VCRUNTIME` 依赖，仅剩 Windows 系统 DLL。
- 同一静态 Qt 代码生成的 Standalone 在清空 Qt 环境变量后启动成功：
  窗口标题为 `Overteon`，进程保持响应，stdout/stderr 日志为空。
- 用户随后在实际宿主中确认本次结果成功，编辑器界面由用户验证。
- 替换系统目录前，旧预设曾备份到
  `Build/win-static/system-presets-backup`；本次文档整理未再触碰该目录或现有预设。

---

## 九、UI 交互与 0.9.10 行为还原（2026-11-16）

本节修改仅涉及 Qt/QML 编辑器及显示层，不改变 DSP、参数 ID、预设格式或 VST3 目录结构。

### 1. 中央全局控制区

- `Source/ui/qt/qml/OvHeader.qml`：右侧改为固定宽度的两行布局，上行等距放置 BYP、A、B、INIT，下行放置等宽的 `A → B`、`B → A` 按钮；保留 A/B 字母左键切换和右键复制。
- Settings 热区由整个复合仪表缩至中央谐波环附近的 `96 × 60` 设计像素，避免遮挡上下 EQ 边缘节点。
- `Source/ui/qt/OvEngine.cpp`：复制到当前活动 A/B 槽时同时恢复快照参数并刷新两个 EQ 区域，修复活动槽只更新快照、不改变当前声音的问题；复制到非活动槽仍不切换当前声音。

### 2. 梯形玻璃动态光源

- `Source/ui/qt/OvEngine.cpp`：恢复 0.9.10 的 `(dB + 60) / 60` 映射，左右声道取较大值，上梯形使用 OUT、下梯形使用 IN，不再叠加额外 Fall 平滑。
- `Source/ui/qt/qml/main.qml`、`Source/ui/qt/qml/OvGpuGlass.qml`：保留平方响应，并增加与原版 CSS 对应的 100 ms 线性透明度过渡。
- `Source/ui/qt/OvEqView.cpp`：CPU/备用绘制路径也改为由统一电平驱动椭圆径向渐变，不再按频谱频段调制光源。

### 3. 复合仪表分层电平表

- `Source/ui/qt/OvMeterView.cpp`：上层高亮瞬时条默认直接显示测量值；底层 Peak Hold 独立在创新高后保持 1.1 秒，再以归一化 `0.18/s`（60 dB 量程下 10.8 dB/s）线性回落，峰值的检测和回落不受瞬时条平滑设置影响。
- 瞬时条平滑与 HARMONIC MONITOR 的 Interpolation 完全解耦；光晕缓存键同时包含瞬时条和 Peak，避免亮条移动时光晕沿用旧帧。
- `Source/ui/qt/OvEngine.h/.cpp`、`Source/ui/qt/qml/OvSettings.qml`：新增 `LEVEL METERS / SMOOTHING`，提供 OFF、LIGHT、MEDIUM、HEAVY 四档；默认 OFF 对应 0.9.10，后三档仅改变瞬时条的显示释放速度。

### 4. EQ 数值编辑弹窗

- `Source/ui/qt/qml/OvEqSection.qml`：恢复双击节点打开 Frequency、Gain、Resonance/Q 数值窗；普通悬停不再自动弹出，双击不再切换频段 On/Off，双击空白创建节点的行为保持不变。
- 保留 0.9.10 的跟随节点、空间不足时翻转、移出弹窗关闭、离节点超过 30 px 关闭、空白按下关闭及 Esc 关闭规则。
- `Source/ui/qt/qml/OvNeField.qml` 的实时数值拖动继续使用窗口坐标计算增量，普通灵敏度 `0.005 norm/px`、Shift 精细灵敏度 `0.0012 norm/px`，拖动时同步刷新参数、读数和 EQ 节点。

### 5. 验证记录

- 使用 Visual Studio 2026 的 x64 MSVC 环境，在 `Build/win-static` 成功构建 `Overteon_VST3` 与 `Overteon_Standalone`；VST3 仍输出到原有 `Overteon_artefacts/Release/VST3/Overteon.vst3` 目录。
- 修改后的 QML 已随 qrc 重新编译并链接进两个目标。
- Standalone 启动后保持运行，3 秒冒烟检查期间 stdout/stderr 均为空，随后由验证脚本正常关闭。
- 实际宿主内的视觉、音频响应和完整鼠标手势回归仍需在宿主中确认；本次未改动或迁移任何既有预设。

---

## 十、右侧布局、About、Settings 与键盘焦点修订（2026-11-16）

- `Source/ui/qt/qml/OvHeader.qml`：BYP、A、B、复制、INIT 恢复为单行垂直居中布局；只保留一个复制按钮，A 槽固定显示并执行“A到B”，B 槽固定显示并执行“B到A”。按钮统一使用 Inter 10 px、1.5 px 字距，移除右键复制、闪烁提示和相关过渡动画。
- `Source/ui/qt/qml/OvAbout.qml`：按 0.9.10 的 450 px 卡片、边距、字号、行距和顺序恢复完整三段正文、制作署名、官网入口及 CLOSE；仅在制作名单中新增 Xyas，官网点击继续调用系统浏览器。
- `Source/ui/qt/qml/OvSettings.qml`、`Source/ui/qt/OvEngine.h/.cpp`、`Source/ui/qt/OvMeterView.h/.cpp`：删除 `LEVEL METERS / SMOOTHING`、`meterSmoothing` 属性与状态、瞬时条 Fall 和释放速率常量；Settings 分段按钮过渡动画同步移除。复合仪表保留 0.9.10 的瞬时亮条与独立 Peak Hold 分层逻辑。
- `Source/ui/qt/OvQmlEditor.cpp`、`Source/ui/qt/OvQtComponent.cpp`：编辑器和 Qt 嵌入组件允许键盘焦点；插件内鼠标按下时同步请求 Qt 原生焦点和 JUCE 逻辑焦点，ShortcutOverride 不再漏给宿主；Qt FocusOut 时清除 JUCE 焦点，使点击插件外部后宿主重新接管空格及走带按键。
- DSP、参数 ID、预设体系和 VST3 目录结构均未改变。宿主内的最终视觉与键盘回归仍需实际宿主确认。
- 已用 `Build/win-static` 重新构建 `Overteon_VST3` 并导出到 `Build/dist-win/VST3/Overteon.vst3`（35,491,840 字节，SHA256 `BAE0F72F7E9A38FFDB7D06414283EF4063C367A43344234979630CFB1FF29A90`）；工厂自检 `InitDll=True`、`FactoryClasses=2`、`QtDllCount=0`。

---

## 十一、About QML 修复、EQ 悬停编辑与 UI 参数刷新（2026-11-16）

- **About 界面无法创建**：`OvAbout.qml` 把参考 CSS 的小数字号直接写进了 `font.pixelSize: 9.5 / 10.5`，而 QML 的 `pixelSize` 只接受整数，报错 `Invalid property assignment: int expected`，导致整个 QML 场景加载失败、编辑器只显示兜底文字。工厂自检不会实例化 QML，所以上一轮的"构建通过"没有覆盖到。已全部改为整数 10 px，Standalone 冒烟测试 stderr 无任何 QML 错误。
- **验证流程补漏**：`Assets/docs/BUILD_Windows.md` 新增 7.4"界面自检（导出前必做）"——Standalone 运行 6 秒、stderr 无 `[ov] qml error` 才允许导出；工厂自检仅作补充。
- **UI 改参数不刷新**：`OvParam::setNorm()` 在 UI 写入时就更新了自己的缓存，`OvEngine::tick()` 用"poll 前后差值"检测变化，永远看不到 UI 发起的修改，`paramRevisionChanged` 只对宿主自动化有效。右键菜单切换频段类型后曲线因而不重绘，只能关掉编辑器重开。修复：`OvEngine` 构造时连接每个参数的 `edited()` 信号，UI 编辑立即按 sc/pq/drive 分组递增 `m_paramRevision` 并发射 `paramRevisionChanged`。
- **EQ 数值弹窗改为悬停打开**：`OvEqView::hoverMoveEvent()` 命中节点即发 `nodeEditRequested`，双击节点不再必需（双击空白建节点保持不变）。新增 `nodeEditHoverEnded()` 信号：离开节点超过 30 px 或离开绘图区时不再立即关闭，而是通知 QML 启动 400 ms 关闭定时器；悬停同一节点只取消关闭不重置输入框，悬停其他节点直接换绑；进入弹窗取消定时器、离开弹窗重新启动；点空白、✕、Esc 仍立即关闭；拖动字段（scrubbing）期间不启动关闭。
- 已重新构建 VST3 + Standalone，QML 冒烟测试通过后导出 `Build/dist-win/VST3/Overteon.vst3`（35,492,864 字节，SHA256 `D960BC858A2251FAD274C6BF4498C3E7A5C41AF7B1B14E347E766D732A89C493`）。悬停弹窗手感与宿主内键盘行为仍需用户实测确认。

---

## 十二、修复左键点击/拖动全部失效（2026-11-16）

- **症状**：宿主内左键点击、拖动全部无反应。
- **根因**：第十节的键盘焦点修复把 `component->grabKeyboardFocus()` 放在 `OvQtComponent` 事件过滤器的 `QEvent::MouseButtonPress` 同步路径里。该过滤器在 Qt 仍在分发这次按下事件时运行，而 JUCE 的 grab 会对 peer HWND 做原生 SetFocus，原生焦点在分发中途被切走，QML 项目收不到这次按下，点击与拖动全部失效。用户判断正确：是键盘/宿主交互分离改动引入的回归。
- **修复**：`OvQtComponent.cpp` 中键盘焦点抓取改为 `QTimer::singleShot(0, ...)` 延迟到事件循环下一轮，等按下事件完整分发后再抓取；`FocusOut` 处理增加判断，焦点在 Qt 子窗口与 JUCE peer（foreignWindow）之间移动仍属插件内部，不再误清 JUCE 焦点，只有焦点真正离开插件（点击宿主外部）才 `unfocusAllComponents()` 归还宿主键盘。
- **验证**：重新构建 VST3 + Standalone（二进制时间戳晚于源码修改），Standalone 冒烟测试 stderr 无 QML 错误后导出 `Build/dist-win/VST3/Overteon.vst3`（35,492,864 字节，SHA256 `5A5ECEE290BFA5F5577AE4242518F090C46CEE5AA8F317F5353C0459FDCCFCC0`）。宿主内点击/拖动与键盘接管行为需用户实测确认。

---

## 十三、彻底回滚键盘焦点功能（2026-11-16）

- 第十二节的延迟抓取方案实测后左键仍然失效，按用户要求**整体回滚**该功能，不再在无法实测宿主行为的情况下继续猜测修复。
- 回滚内容：`OvQtComponent.cpp` 事件过滤器中的 `MouseButtonPress`/`FocusOut`/`ShortcutOverride` 三个分支及相关 `QGuiApplication`/`QTimer` 头文件全部移除；`OvQmlEditor.cpp` 中编辑器与 qtComponent 的 `setWantsKeyboardFocus(true)` 移除。这两处恢复到键盘功能加入之前的交互状态，其余修复（About 字号、UI 参数刷新、EQ 悬停弹窗）保留。
- **已知取舍**：空格等按键被宿主接管的原问题随之回到原状，后续需在可实测宿主键盘行为的前提下重新设计。
- 已重新构建并通过 Standalone 冒烟测试，导出 `Build/dist-win/VST3/Overteon.vst3`（35,492,864 字节，SHA256 `8BC6C9EC60EAC3AB5C951DC9278E7D5ABD3A9B157D165D48231C49C99ABC44EF`）。若此版本左键仍失效，则根因不在键盘代码，下一步排查 EQ 悬停弹窗。

---

## 十四、补齐开源授权声明（2026-11-16）

- 新增根目录 `LICENSE`（AGPL-3.0 全文，34,523 字节），项目整体以 AGPLv3 发布。
- 新增根目录 `NOTICE`：声明 JUCE 8.0.13（AGPLv3 选项）、Qt 6.9.3（GPLv3 开源选项、静态链接，含 AGPLv3 §13 组合依据）、Inter（OFL 1.1）、edix、VST3 SDK（MIT）、原始 WebView 参考实现与 logo 的授权归属，并附完整对应源码声明与中文摘要。
- 新增 `LICENSES/`：`AGPL-3.0.txt`、`GPL-3.0.txt`、`LGPL-3.0.txt`、`JUCE-8-LICENSE.md`（自 vendored JUCE 复制）、`OFL-1.1-Inter.txt`（自 Inter-LICENSE.txt 复制），以及 `LICENSES/README.md` 说明各组件对应关系与静态链接合规依据。
- `README.md` 增加 Licence 章节，指向上述文件。
- 未改动任何代码、参数 ID、预设格式或数据结构。

---

## 十五、插件内按键正确转交宿主（2026-11-16）

- **症状**：在插件内点击、拖动后再按空格等按键，宿主没有反应，按键像是"死在插件里"。
- **根因**：编辑器是嵌入在 JUCE peer 里的 QQuickView 子窗口。Qt 子窗口一旦持有原生焦点，Windows 就把所有 WM_KEYDOWN 直接投递给 Qt，JUCE peer 根本收不到，于是 JUCE 原有的"未处理按键转发给父窗口（宿主）"路径完全不会执行。这并不是焦点策略错了，而是消息到不了 JUCE。
- **修复**：`OvQmlEditor.cpp` 在 QQuickView 上安装事件过滤器，把 QML 未消费的按键重新注入 JUCE peer 的消息队列：
  - 用 `QKeyEvent::nativeVirtualKey()` / `nativeScanCode()` 还原 WM_KEYDOWN / WM_KEYUP（Alt 组合走 WM_SYSKEY*，方向键等补齐扩展键标志位），`PostMessageW` 到 `peer->getNativeHandle()`。
  - JUCE peer 分发后编辑器不处理，由 `forwardMessageToParent` 交给宿主——与原生 JUCE 插件完全同一条路径。
  - 不改变焦点归属，因此点击/拖动和 EQ 数值输入不受影响；Escape 保留给 QML 弹窗；当 QML 有 TextInput 获得焦点（`inputMethodQuery(Qt::ImEnabled)` 为真）时不转发，保证数值输入正常；Qt 未持有焦点时不转发，避免重复触发。
- 已构建 VST3 + Standalone，Standalone 冒烟测试无 QML 错误后导出 `Build/dist-win/VST3/Overteon.vst3`（35,494,400 字节，SHA256 `B39403B6B61B0C9AFEF223DA9655845C44B2F94F764D007DDD0D1AAB9A6887F4`）。宿主内按键转交需实测确认。

---

## 十六、右侧控制区字体与排版统一（2026-11-17）

- **问题**：BYP / A / B / A到B / B到A / INIT 用的是 Inter 10 px + 1.5 字距，与头部 EDIX 字标割裂；"A到B" 里的中文在 Inter 中回退到系统字体，视觉上更杂；整排在 1040 宽度下被右边缘裁切。
- **字体统一**：该控制区全部改用 `OvTheme.displayFont`（EDIX），字号 10 px、字距 0.8，和 OVER|TEON 字标同一字族。
- **排版重构**：抽出内联组件 `Ctrl`（居中文本、状态细线、扩大热区），BYP / A / B / INIT 复用同一规格；行间距 12、起点 x=800，右侧留出安全边距，`INIT` 不再被裁切。
- **A→B 箭头**：EDIX 只有 124 个 ASCII 字形，没有箭头也没有中文，所以方向不再用 "A到B" 文本，改为 "A" + 1 px 线段与两段 34° 短线组成的箭头 + "B"，随 A/B 模式自动反向；三根 1 px 线与界面其他细线语言一致。
- **验证**：Standalone 实机截图逐像素核对，五项完整可见、间距均匀、字族统一；QML 冒烟测试无错误。导出 `Build/dist-win/VST3/Overteon.vst3`（35,494,912 字节，SHA256 `281201223BA0444096D107586A972DC00BE876E4543F0607C63798168700F3C2`）。

---

## 十七、右侧控制区放大字号并右对齐（2026-11-17）

- 字号由 10 px 提升到 12 px，字距 0.8，仍是 EDIX，可读性明显改善。
- 整排由固定 `x: 800` 改为 `anchors.right: parent.right` + `rightMargin: 26`，`INIT` 贴右边缘对齐；左侧仍从仪表尾部之后开始，与 pod 无重叠。
- Standalone 实机截图确认字号、对齐、间距正常；QML 冒烟测试无错误。导出 `Build/dist-win/VST3/Overteon.vst3`（35,494,912 字节，SHA256 `40F3FF7D5E45D815452DF72A96101D157D784E2FE654544577CFC5E58EB92F1E`）。

---

## 十八、右侧控制区再压缩：A|B 合并、A>B 取代箭头（2026-11-17）

- **A/B 合并**：两个独立按钮合成一个 `A｜B` 控件（中间 1 px 竖线分隔），当前槽位的字母下方用细线标记——A 为青绿 `pq`，B 为粉红 `sc`；点击整体在 A/B 之间切换。省掉一组间距，视觉上不再像两个孤立的字。
- **方向标记**：去掉三段线绘制的箭头（连同两侧 5 px 间距共约 22 px），改用 EDIX 自带的 ASCII `>`，即 `A>B` / `B>A`，随活动槽自动反向。
- **排版**：字号 12 → 13 px，行距 12，右侧边距 26 → 10，`INIT` 进一步贴住右边缘；左侧仍从仪表尾部之后开始，截图确认与 pod 不重叠。
- **验证**：Standalone 实机截图确认 `A｜B`、`A>B`、`INIT` 完整显示且无挤压；QML 冒烟测试无错误。导出 `Build/dist-win/VST3/Overteon.vst3`（35,502,080 字节，SHA256 `58D639F98B8447D163E96556423468A0528C3B7E22FE09D61DAB9DFEEC1B53AA`）。

---

## 十九、间距定稿（2026-11-17）

- 过程中试过两轮进一步收紧（行距 9→8、固定槽宽等），实机对比后用户确认过窄，已回退到中间版本。
- 最终参数：`Ctrl` 宽 = 文本宽度 + 4；`A｜B` 宽 = 内容宽 + 6；行距 9；右边距 10；字号 13 px、字距 0.6。
- Standalone 实机截图确认与回退目标版本一致；QML 冒烟测试无错误。导出 `Build/dist-win/VST3/Overteon.vst3`（35,502,080 字节，SHA256 `53B0F9081FA8EC71BD0A1945B313B5D17E360706E09D3D4BD414BD50BDE19D60`）。

---

## 二十、频谱 RAW 真正生效、电平表改采样峰值并按 VU 回落（2026-11-17）

- **频谱 RAW 一直是假的**：`OvEqView` 早有 `specNoInterp`（raw = 不做时间平滑），但 `main.qml` 只绑定了 `specPeak`，从未绑定 `specNoInterp`；设置里的 INTERPOLATION → NONE 只写进了没人读的 `engine.upperInterp/lowerInterp`。已在两个 `OvEqView` 上分别绑定 `specNoInterp`，上下频谱各自独立生效。
- **停止播放后频谱定格**：分析器只在完成新的 FFT 块时发布表格，宿主停止调用 `processBlock` 时表格永远保留最后一帧。`OvEngine::tick()` 现在记录每个分析器最后一次拿到块的时刻，超过 150 ms 没有新块就把对应表格清零并发出 `spectrumChanged`。150 ms 而不是"第一次未命中就清零"，因为 FFT 每 43 ms 完成一次而宿主可能 15 Hz 轮询，后者正是过去频谱闪烁的原因。
- **电平表亮条改采样峰值**：`OverteonProcessor` 新增 `meterInPkL/R`、`meterOutPkL/R` 四个原子（每块 max|x|，无任何时间平滑），`OvMeters`/`OvJuceSource` 一并返回；`OvMeterView` 的亮条改读峰值，因此 INSTANT 模式下音频一停、下一块就归零。
- **可配置回落**：`OvEngine` 新增 `meterRelease`（0 = INSTANT，1 = VU 300 ms）。VU 按表头标准 300 ms 到达 99 % 阶跃，即 65 ms 一阶时间常数。设置面板新增 `LEVEL METERS / RELEASE`。
- **设置面板收窄**：卡片 560 → 520，内边距 26 → 22，行距 16 → 12，选项块高 24 → 20、标签列 168 → 150。
- peak 尾迹（1.1 s 保持 + 10.8 dB/s 回落）保持不变。
- Standalone 冒烟测试无 QML 错误；VST3 构建成功并导出（35,504,640 字节，SHA256 `468156AA12E0E9488E1F5B4FDEDE1E27EEF0337E97053DF53D463D4DCA2A4FB9`）。设置面板的实际观感需用户确认。

---

## 二十一、频谱校准修正与显示范围（2026-11-17）

- **根因是电平校准高了 6 dB**：FFT 用了 Hann 窗（2048 点，相干增益 0.5），满幅正弦在 bin 中心是 `N/4`；原代码 `magnitude / fftSize × 8.0` 等于除以 `N/8`，读数比真实高 6.02 dB。结果是 -6 dBFS 就显示成 0 dBFS，低中频整段撞到 0..1 上限被削平，形成截图里 100 Hz–1 kHz 的水平平台，自然斜率也被抹掉。已改为 `× 4.0`（即除以 `N/4`）。
- **显示范围扩到 -90..+6 dBFS**：`ovspec::kSpanDb 85 → 96`、`kFloorDb -85 → -90`；`OvJuceSource::analyse()` 的映射改为 `-90..+6`；`OvFft.h`（预览/JACK 路径）与 `OvJackSource.h` 同步更新。0 dBFS 现在落在绘图区高度 93.75 % 处，上方留 6 dB 余量。
- **填充不再越界**：去掉原来乘的 0.92 压缩系数，按全高映射，并把每个采样点的 y 限制在 `[top, bottom]` 内——Catmull-Rom 在尖峰处会过冲，不限制就会戳出上边框。
- 斜度未做任何人为补偿：校准正确后，音乐本身的 -4.5 dB/oct 会自然呈现。
- Standalone 冒烟测试无 QML 错误；VST3 构建并导出（35,504,640 字节，SHA256 `4C38A7F2499D0241FC1C60B011761C88C131BE4531A6F262AA563B2BF3E17F7A`）。频谱形态需在宿主内播放素材确认。

---

## 二十二、频谱分辨率提升与数值框悬停联合区域（2026-11-17）

- **FFT 2048 → 4096**：`OverteonProcessor::fftOrder 11 → 12`，`OvJuceSource` 的 FFT、Hann 窗与工作缓冲同步改为 `1 << 12`。频率分辨率 23.4 Hz → 11.7 Hz，低频细节明显增加；窗口 42.7 ms → 85.3 ms，瞬时模式仍逐帧显示。
- **显示 bin 128 → 256**：`ovspec::kBins` 改为 256；`OvEngine` 的表格/数组、`OvEqView` 的采样数组、样条控制点、拷贝长度、`reserve` 全部改用 `ovspec::kBins`；对数频率轴的分母由 127 改为 `kBins - 1`。
- **平滑相对变轻**：频率核仍按 bin 计（sigma 2.5、半宽 7），bin 密度翻倍后等效平滑从约 ±0.55 八度降到约 ±0.27 八度，窄峰不再被半八度平均掉。时间常数不变。
- **数值框悬停**：新增 `OvEqView::setEditingRect(x,y,w,h)`，QML 在 `place()` 里把数值框矩形映射到视图坐标并提交；`hoverMoveEvent` 的 30 px 规则改为"节点半径 ∪ 数值框外扩 8 px"联合区域，只有离开两者才通知 QML 启动关闭延迟。从节点走向数值框不再中途消失。
- CPU：FFT 与绘图点数各约 2×，均在小节级别；释放后仍受频谱 30 Hz 门控限制。
- Standalone 冒烟测试无 QML 错误；VST3 构建并导出（35,504,128 字节，SHA256 `5911C1659734427C63C7B1260B547D71D2110789127B68AA686C53C63C253581`）。频谱细节与悬停手感需宿主内实测。

---

## 二十三、频谱改为逐帧连续分析，按 MiniMeters 的密度重做曲线（2026-11-17）

### 卡顿的根因：数据每 85 ms 才来一帧

旧分析器由音频线程填满 `fftSize` 个样本后才置 `fftReady`，UI 只能在这些块边界取数：
4096 点 / 48 kHz = 85 ms，约 11.7 帧/秒。界面画得再快，曲线也只能每 85 ms 跳一次，
这正是"一卡一卡"的观感来源。

- `OverteonProcessor` 新增两路单生产者环形缓冲 `specRingIn/specRingOut`
  （`specRingSize = 2 * fftSize`，自由计数 + release/acquire），音频线程每样本只多一次写入。
- `OvJuceSource::analyse()` 改为每次调用都从环里取最新 `fftSize` 个样本、在 UI 线程做 FFT；
  没有新样本（宿主停止调用 `processBlock`）时才返回 false，原有的 150 ms 清零逻辑继续生效。
- 旧 `fftData/fftReady` 保留原样，WebView 编辑器路径不受影响。

### 密度：8192 点 FFT、1024 个显示 bin、按列取最大值

- `fftOrder 12 → 13`（8192 点，48 kHz 下 5.9 Hz/bin，窗口 170 ms）。
- `ovspec::kBins 256 → 1024`，1024 个对数 bin 铺满 20 Hz–20 kHz，绘图区每 bin 约 1 px。
- **每列取该频率范围内最响的 FFT bin**，而不是点采样：10 kHz 的窄音只有 5.9 Hz 宽，
  而一个显示 bin 在高频约 135 Hz 宽，点采样会让稳定的正弦时隐时现；取最大值既保证
  不丢峰，也正是参考图那种密集尖峰的来源。低频（一列窄于一个 FFT bin）仍用线性插值。
- **RAW 现在是真 RAW**：`SpectrumBallistics` 在 `interp == false` 时完全跳过频率核与时间
  一阶滤波，直接输出测量值；切回平滑模式时用当前测量重新播种滤波器状态，避免跳变。
  平滑模式（SMOOTH）的频率核仍保持半八度（sigma 5 bins）。
- **RAW 的折线不再走 Catmull-Rom**：样条会把窄峰磨圆；1024 点本身已约 1 px 间距，
  直接连线即可。平滑模式仍保留样条，细分从 5 段降到 2 段。
- 同时修复第二节遗留的硬编码 `127`：上下两个绘制路径在 `kBins` 改成 256 后仍只画到
  第 127 个 bin（约 631 Hz），高频一半根本没有被画出来。现全部改为 `kBins - 1`。

### 刷新率与成本

- `OvTiming::kPlotHz 30 → 60`，与引擎的频谱采样对齐；曲线每帧重建，
  背景/叠层/发光条仍按缓存键命中，只有频谱三角带每帧提交。
- 引擎的频谱读取由"每两个 8 ms tick"改为 12 ms 墙钟门控：插件里宿主通常只以
  30–60 Hz 泵 Qt 事件，旧写法实际只有 15–30 次/秒；12 ms 门控保证插件与独立版
  都稳定在约 60 次/秒。
- 增量成本：UI 线程每 16 ms 两次 8192 点 FFT（仿真量约 1 % 单核）+ 每帧一条
  约 1026 点的三角带；音频线程每样本多一次 float 写入。GPU 侧点数翻倍，但仍是
  一条顶点色三角带，不增加纹理上传。

### 验证

- Standalone 运行 7 秒无崩溃、stderr 无任何 `[ov] qml error`。
- VST3 构建成功并导出 `Build/dist-win/VST3/Overteon.vst3`
  （35,504,640 字节，SHA256 `B35026C7107FF9D85541F148ADDB200115D4619D13CFBA811573A7A5F4BEE085`）。
- 频谱的精细度、流畅度与 CPU 占用需在宿主内播放素材确认；若细节仍不足，下一步只
  降低 `freqSigma`（平滑模式），RAW 模式已不再受频率平滑影响。

---

## 二十四、频谱显示斜率 + 上下两个监测点（2026-11-17）

### 显示斜率（tilt）

- `OvJuceSource::analyse()` 在 dB 映射前给每个显示 bin 加 `4.5 dB/oct` 的显示斜率，
  以 **1 kHz** 为支点：20 Hz 约 -25 dB，1 kHz 为 0，10 kHz 约 +15 dB，20 kHz 约
  +19 dB。这正是 SPAN 默认的 slope / Pro-Q 的 Slope 控件所做的事，只改画面，不改音频。
- 不参与音色的任何计算；SC 与 POST EQ 两条频谱都应用。
- 数字静音不倾斜：低于 -89.5 dB 的读数直接归 0（画面底部），避免停止播放后右侧
  留下一段被抬高的“斜线底噪”。
- 可临时用环境变量 `OVERTEON_SPECTILT` 改斜率（单位 dB/oct，`0` 恢复旧显示）。

### 监测点

- **上框（SIDECHAIN EQ）默认监测点移到 SC EQ 之后、失真之前**：在 SC EQ 的滤波循环
  之后、过采样失真之前取样，所以拖动 SC EQ 会立即反映在频谱上。旧版本取的是原始
  输入，这就是“频谱不随 SC EQ 变化”的原因。
- **打开 SOLO HARM 时，上框监测点移到失真模块之后**：solo 会静音干信号，失真级输出
  的就是纯谐波，频谱直接显示它。为避免两种信号在同一环形缓冲里交错，这一块会跳过
  “失真前”的取样，只写入失真后的信号。
- **Classic 模式**不运行 SC EQ，上框监测点等于输入（M/S 模式下取 Mid）。
- **下框（POST EQ）完全不变**：始终监测最终输出，与 SOLO HARM 无关。
- M/S 模式下频谱的 mono 取值改用 Mid（原来用 (M+S)/2，实际是解码后的左声道）。

### 验证

- Standalone 运行 7 秒无崩溃、stderr 无 QML 错误。
- VST3 重新导出 `Build/dist-win/VST3/Overteon.vst3`
  （35,506,176 字节，SHA256 `EBF747C5E7BDB1BE25F466882D00077870E49B7B72FC2825441F8877AC9F380C`）。
- 斜率是否合适、SOLO HARM 时上框是否显示纯谐波，需在宿主内实测；斜率可用
  `OVERTEON_SPECTILT=3` 或 `6` 对比。

---

## 二十五、底部旋钮标签不再截断 + 窗口四角缩放（2026-11-17）

### 旋钮标签

- 截图里 `EVEN/O…`、`HARMO…`、`HARM G…`、`AIR PRO…` 的省略号来自 `OvKnob`：
  标签 `Text` 被固定成旋钮槽宽 58 px 并开了 `elide: ElideRight`。
- 现在标签不再设宽、也不再 elide，只保持水平居中。Deck 的槽间距即使在最窄设计宽度
  下也有约 59 px，长标签（`AIR PROTECT`、`HARMONICS`）落在空白里，不会碰到相邻旋钮。

### 窗口缩放

- `OVERTEON_RESIZABLE_WINDOW` 默认改为 **ON**：编辑器 `setResizable(true, true)`，
  宿主窗口可缩放；限制仍为设计尺寸的一半到 4096×4096。
- 右下角新增一个 20×20 的斜线抓手（默认 40 % 透明度，悬停 85 %），鼠标指针为
  `SizeFDiagCursor`。拖动它请求的是**窗口**缩放，而不是把场景拉伸进固定框架：
  `OvEngine::requestWindowResize()` → 编辑器 `setSize()` → JUCE 的
  `ui:resize` / `IPlugFrame::resizeView`。
- 拖动增量取自全局光标（`QCursor::pos()`），而不是 MouseArea 局部坐标：窗口一边
  变大、抓手一边在指针下移动，用局部坐标会把缩放反馈进增量，表现为抖动。
- 固定尺寸构建里 `windowResizable` 为 false，抓手直接隐藏，不会出现“拖了没反应”。
- 预览程序仍使用自己的窗口逻辑，不受影响。

### 验证

- Standalone 运行 7 秒无崩溃、stderr 无 QML 错误（`windowResizable` 绑定被求值，
  说明新属性/可调用函数已成功注册）。
- VST3 重新导出 `Build/dist-win/VST3/Overteon.vst3`
  （35,492,864 字节，SHA256 `6F9C92F94472056490F7AE68B129FBA1C629FD98159B758B8EC2D6C6FFC857B1`）。
- 宿主内的实际缩放行为（不同 DAW 对 `resizeView` 的响应速度）需实机确认；标签是否
  完整显示同样需要在窗口内确认。

---

## 二十六、缩放改为等比例 + 记住窗口尺寸（2026-11-17）

### 等比例缩放（已在第二十七节撤回，恢复自由缩放）

- 右下角抓手的拖动改为**锁定设计比例 1040:772**：取指针相对位移更大的那个轴作为
  主导，另一个轴由比例推出；到达最小/最大尺寸时再反推，始终不会变形。
- 宿主自己提供的窗口边缘缩放仍然可能给出任意比例（控件不在我们手里），但下次打开
  时会按保存的宽度还原成设计比例。

### 窗口尺寸持久化

- 保存位置：与过采样偏好相同的**每用户**设置文件（`%APPDATA%\Eonun\Overteon.settings`，
  若机器上已有旧的 `HarmonicInjector.settings` 则沿用），元素 `HINJGLOBAL` 里的
  `editorW` / `editorH`。
- `OverteonProcessor::saveGlobalOsFactor()` 同时改为**读-改-写**：原实现每次都新建一个
  只含 `osFactor` 的元素再覆盖文件，加入窗口尺寸后两个写入方会互相擦除。
- 保存时机：`resized()` 只重置一个 30 tick（约 0.5 s）的倒计时，停止拖动后写一次文件；
  编辑器析构时若尺寸与已保存值不同再补写一次。拖动过程不会每帧写盘。
- 读取时机：`OvQmlEditor::targetSize()` 优先使用保存的宽度（高度按比例推出），再套用
  屏幕尺寸与 520×386..4096 的限制。因此关闭编辑器再打开、删除插件重新添加、甚至
  新建另一个实例，都会以用户上次的尺寸打开。
- 在 `editorW=1200 editorH=891` 的设置文件下启动 Standalone，
  `OVERTEON_SIZEDBG` 输出 `want 1200x891`，确认读取与申请链路生效。

### 验证

- Standalone 启动正常、stderr 无 QML 错误；尺寸恢复日志见上。
- VST3 重新导出 `Build/dist-win/VST3/Overteon.vst3`
  （35,496,448 字节，SHA256 `4BF251924A62B47C07CAC4ACE97F9C59AC4A12B0AA9A860AE299B2F9E0CFE693`）。
- 宿主内的实际保存/恢复（关闭工程、删除插件、重新添加）需实机确认。

---

## 二十七、WINDOW SIZE 预设：ORIGINAL / DEFAULT / SAVE AS DEFAULT（2026-11-17）

### 缩放恢复自由

- 撤回第二十六节的等比例锁定：右下角抓手的两个轴重新独立，最小 520×386、
  最大 4096×4096。
- 打开时的尺寸不再按比例归一化：内存里存的是什么，就原样还原什么。

### 设置面板

- 删除底部整段 RMS/True Peak/Raw/Release 说明文字。
- 新增 `WINDOW SIZE` 区块，一行 `PRESET` + 三个动作 chip：
  - `ORIGINAL`：固定 1040×772（沿用平台 DPI 与屏幕适配），永远不变。
  - `DEFAULT`：读取 `SAVE AS DEFAULT` 存下的预设；**从未保存时行为与 ORIGINAL 完全一致**。
  - `SAVE AS DEFAULT`：只把当前窗口尺寸记成 DEFAULT，不改变“打开时用上次尺寸”的规则。
- 不显示像素读数。
- 高亮：当前窗口尺寸精确等于 ORIGINAL 或已保存的 DEFAULT 时对应 chip 点亮；
  `SAVE AS DEFAULT` 点击后短暂亮 1.2 s 作为确认。
- 语义要点：窗口每次打开仍是**上次关闭时的尺寸**；保存 DEFAULT 只是存一个可一键
  回去的预设。保存后再次自由拉伸、关闭、重开，拿到的仍是拉伸后的尺寸。

### 存储与实现

- 设置文件 `HINJGLOBAL` 新增 `defaultW` / `defaultH`，与 `editorW` / `editorH` 并存；
  所有写入仍走读-改-写，不会互相擦除。
- `OvEngine` 新增 `windowSizeOriginal/Default/SaveDefault` 三个可调用动作，以及
  `defaultWindowW/H`、`windowDefaultSaved` 供 QML 判断高亮。
- `OvQmlEditor` 拆出 `designSize()`（固定设计尺寸）与 `clampSize()`（屏幕与上下限），
  `targetSize()` 优先返回上次尺寸。
- Standalone 验证：设置 `editorW=1200 editorH=900`，日志为
  `want 1200x900`、`editor 1200x900 ... ok`，确认自由尺寸原样恢复。
- VST3 重新导出 `Build/dist-win/VST3/Overteon.vst3`
  （35,501,568 字节，SHA256 `B2E3CAC3C063D3E473C28B5962494EFB240AD7E416F2C34E7F9828F36E9967D3`）。
- ORIGINAL/DEFAULT 点击后的宿主缩放、以及三个 chip 的高亮，需在宿主内实机确认。

---

## 二十八、设置里的 MODE 合并：RMS / TRANSIENT（2026-11-17）

### 三个仪表统一成一个 MODE

- `HARMONIC MONITOR`、`UPPER SPECTRUM`、`LOWER SPECTRUM` 的 `DETECTOR` +
  `INTERPOLATION` 两行合并为一行 `MODE: [RMS] [TRANSIENT]`，四个按钮变两个，
  默认全部 `TRANSIENT`。
- 上下频谱的绑定关系固定下来：
  - `RMS` = 功率域平均检测器 + SMOOTH 显示（时间缓动、Catmull-Rom）。
  - `TRANSIENT` = 频段内最大 bin + max-hold + RAW 显示（无时间平滑、直线折线）。
  - 原来“RMS + RAW”“PEAK + SMOOTH”这类混合组合不再存在。
- 谐波监控环彻底去掉显示平滑：
  - 删除 `harmInterp` 属性、Settings 里的 INTERPOLATION 行、`OvMeterView` 的
    `m_noInterp` / `m_harmPeakMode`，以及环专用的 `ovb::Fall` 缓动。
  - 环的 `MODE` 只决定测量值：`RMS` = DSP 已平滑的 131 ms 比值，
    `TRANSIENT` = 当前块自己的未平滑比值。
- 原 `TRUE PEAK` 选项在环上改名为 `TRANSIENT`（测量本身未变，仍是块峰值）。

### 代码与持久化

- `OvEngine` 的属性简化为 `harmMode` / `upperMode` / `lowerMode`（0 = RMS，
  1 = TRANSIENT）；`main.qml` 的 `specPeak` 与 `specNoInterp` 都由同一个
  `*Mode === 1` 驱动。
- 三个模式写入每用户设置文件的 `HINJGLOBAL`：`harmMode`、`upperMode`、
  `lowerMode`；编辑器创建时读取并在首帧前应用，面板切换时写回。
- 其它设置（LEVEL METERS / RELEASE）行为不变。

### 验证

- Standalone 运行 7 秒无崩溃、stderr 无 QML 错误。
- VST3 重新导出 `Build/dist-win/VST3/Overteon.vst3`
  （35,503,616 字节，SHA256 `34042AFC6679034832B354CCB43310F3B88D7C924D3A3DDBB29C0CD4A0D0D4D6`）。
- 三个 MODE 的实际切换效果、默认 TRANSIENT 的观感、以及重开插件后的记忆需在宿主内确认。

---

## 二十九、谐波环上的限制器弧（GR，逆时针，交叉变白）（2026-11-17）

### 新弧

- 谐波环上新增第二条弧：12 点起**逆时针**，长度 = 限制器的增益衰减（GR）。
- 谐波弧仍然 12 点起顺时针，颜色渐变与百分比读数不变。
- 限制弧固定红色 `#ff2a44`；悬停设置时跟随谐波弧一起变绿。
- 两条弧同半径、同 4 px 描边。`谐波占比 + 限制占比 > 100%` 时，它们在环上重叠的
  那一段画成**白色**（glow 与 sharp 两遍都画，所以白段也有外发光）。

### GR 测量

- `OverteonLimiter` 新增 `getLastGainReductionDb()`：
  - MODERN / TRUE PEAK：取本块内 lookahead gain 的最小值，`GR = -20·log10(gMin)`。
  - CLIP：逐样本比较输入与软削波输出，取最大 `20·log10(|x|/|y|)`。
  - OFF：0。
- 用限制器自己的 gain，而不是输入/输出峰值差：lookahead 会把引发衰减的峰值推到
  后面的样本，峰值差恰好会低估瞬态。
- `OverteonProcessor` 新增 `meterGrPk`（本块最深 GR）与 `meterGr`（同一 131 ms 一阶
  平滑）；旁通时都归零。

### 模式与设置

- 限制弧的值跟随 `harmMode`：`TRANSIENT` 用 `grPeak`，`RMS` 用 `gr`；显示端不再加工。
- Settings 新增 `LIMITER RING / RANGE`：`[6 dB] [12 dB] [24 dB] [48 dB] [96 dB]`，
  默认 12 dB，持久化为 `grRange`（设置文件里存实际 dB 值）。
- 环的 9 点 / 3 点位置显示标度：`范围/4` 与 `范围×3/4`，7 px 灰字，落在环与上下
  两组计量表钩子之间的空隙里。12 dB → 左 3 右 9；6 dB → 1.5 / 4.5；24 dB → 6 / 18；
  48 dB → 12 / 36；96 dB → 24 / 72。非整数保留一位小数。

### 验证

- Standalone 运行 7 秒无崩溃、stderr 无 QML 错误。
- VST3 重新导出 `Build/dist-win/VST3/Overteon.vst3`
  （35,507,712 字节，SHA256 `326E29B154BA705CF78FE64FD3D5067A13D4B6B269FC2A7284B2FCB1076247DF`）。
- 红色弧的实际 GR 观感、白色交叉段、以及 6/24/48/96 档位的标度需在宿主内实测。

---

## 三十、限制弧定稿：12 点双色圆头、即时刷新、去掉 96 dB（2026-11-17）

> 本节“12 点交界”的**三个图形叠加**方案已在第三十一节被“单路径重画”取代；
> 即时刷新与去掉 96 dB 两条仍然有效。

### 12 点交界

- 两条弧仍从 12 点出发、仍用**圆头**，不加缺口、不做角度偏移。
- 限制器未工作时（GR = 0）**什么都不画**：环与加入限制弧之前逐像素一致。
- 限制器一露头（GR > 0），12 点那颗直径 4 px 的起点圆变成双色：
  - 顺时针半边 = 当前谐波色；
  - 逆时针半边 = 红色；
  - 中间用水平线性渐变过渡，长度正好是这颗点的直径 4 px（半宽 2 px）。
- 谐波为 0、只有限制器时保持纯红，不做双色过渡。
- 悬停时两色都变绿，过渡自然消失。
- glow 与 sharp 两遍都绘制，交界点同样有外发光。

### 即时刷新

- `OvMeterView::stateKey()` 加入 `m_limitFrac` 与 `m_grRange`，标度/弧长变化会触发重绘。
- `setEngine()` 连接 `OvEngine::settingsChanged`：切换 RANGE 时同步 `m_grRange`、重算
  `m_limitFrac` 并 `update()`，静音状态下也立即刷新，不再等音频。

### 档位

- 删除 96 dB：RANGE 变为 `[6] [12] [24] [48] dB`；`OvEngine::setGrRange()`、
  设置加载校验、QML 选项三处同步。
- 标度仍为 9 点 = 范围/4、3 点 = 范围×3/4。

### 验证

- Standalone 运行 7 秒无崩溃、stderr 无 QML 错误。
- VST3 重新导出 `Build/dist-win/VST3/Overteon.vst3`
  （35,508,224 字节，SHA256 `1404ED1F74BB1327B08BFEC4552A13C217B6503A48AEF7FBBB6AF021C60901D7`）。
- 12 点双色过渡的观感、静音下切换 RANGE 的立即刷新需在宿主内确认。

---

## 三十一、谐波环改为单路径绘制（消除 12 点接缝）（2026-11-17）

### 问题

第三十节把环画成三个独立图形：谐波弧、限制弧、12 点的渐变圆。三者各自栅格化、
各自带抗锯齿边缘；在 12 点，两个半圆帽边对边相接，再多盖一个圆，等于同一圈边缘被
覆盖了两到三次。悬停时两边都是绿色、颜色完全一致，剩下的就是这层重复的
抗锯齿/透明度密度差——看起来像“有一个隐隐约约的断点、像贴了一层”。

### 修复

- 整条环改成**一条连续路径**：从限制弧末端出发，经过 12 点，到谐波弧末端，
  一次性描边。12 点不再有起点帽重叠，也不再有单独的补丁图形。
- 颜色由 `QConicalGradient` 提供：
  - 12 点逆时针侧 = 红（悬停为绿）；
  - 顺时针侧 = 当前谐波色；
  - 交界过渡宽度 = 一个描边直径（4 px 弧长，环半径 28 px 上约 ±4.09°）。
- 限制弧末端另有一个硬切色点，位置在端点圆帽之外，因此末端圆帽仍是红色。
- GR = 0：走原来的单条谐波弧绘制，单次描边、圆头，与加入限制弧之前逐像素一致。
- 谐波为 0：只画一条红色弧，不做过渡。
- 两弧重叠时路径退化为整圆，白色重叠段仍单独绘制。
- 12 点圆头的形状、两条弧的圆头观感都没有改变。

### 验证

- Standalone 运行 7 秒无崩溃、stderr 无 QML 错误。
- VST3 重新导出 `Build/dist-win/VST3/Overteon.vst3`
  （35,508,736 字节，SHA256 `427300CB604555A21E39EB7458A97B604C210088AEB50FFA72A8C29CB871D3A8`）。
- 悬停全绿时 12 点是否彻底连续、限制器露头时过渡是否按一个点直径出现，需在宿主内确认。

---

## 三十二、限制弧末端硬切与残留光晕修复（2026-11-17）

### 红色弧末端不再渐变到谐波色

- 原因：锥形渐变里 `t1`（12 点过渡结束、红色开始）与 `tip`（限制弧末端之后）之间只有
  两个颜色停靠点，QGradient 在它们之间做**线性插值**，于是整条限制弧从红渐变到谐波色。
- 修复：在限制弧末端圆帽之外增加一个硬切停靠点（`tip - 0.0005` 仍为红色，`tip` 变回
  谐波色），停止点不在路径上，圆帽保持纯红。12 点那 4 px 的过渡不受影响。
- 硬切位置额外留 1°，避免切进圆帽内缘。

### 停止播放后残留的毛玻璃弧

- 原因：环的光晕是缓存层，重建键只包含左右电平、谐波占比和悬停状态，**没有限制弧长度**。
  音频停止后限制弧长度归零，但光晕键不变，缓存的红色光晕就一直留在画面上。
- 修复：光晕键加入 `m_limitFrac` 与 `m_grRange`；限制弧一消失，光晕层随之重建并清空。

### 验证

- Standalone 运行 7 秒无崩溃、stderr 无 QML 错误。
- VST3 重新导出 `Build/dist-win/VST3/Overteon.vst3`
  （35,508,736 字节，SHA256 `7A71CBA3B84903F6401B98DED7742D046FB07DD5EB074A38044DFE9B4EFD6B21`）。
- 需在宿主内确认：限制弧全程纯红、只有 12 点有过渡；停止播放后光晕不再残留。

---

## 三十三、EQ 节点双击静音（2026-11-17）

### 交互

- **双击节点**切换该频段的 mute（`sc{n}On` / `pq{n}On`），再双击恢复。
- 节点的频率、增益、Q、类型全部保持不动；DSP 用 identity 系数旁通，不会因为跳过滤波器
  而在恢复时产生振铃。
- 数值编辑框仍由**悬停**打开，双击不再承担打开编辑器的职责。
- 双击空白画布仍然是“创建最近的池中节点”，行为未变；双击未激活的池中节点不会被
  误判为静音切换，会落入创建逻辑。

### 显示

- 总曲线（含填充、溢出雾）只累加 `On` 的频段，静音频段从主曲线中移除。
- 静音频段自己的曲线保留，但 alpha 从 0.22 降到 0.10，作为“被移除的形状”的提示。
- 节点本身已经是空心圆（原有逻辑），位置不变。
- 曲线缓存键（`curveCacheKeyTag`、`curveSignature`、总曲线键）全部加入 `On` 状态，
  静音后叠加层与 GPU 雾立即重建，不会留旧曲线。

### 验证

- Standalone 运行 7 秒无崩溃、stderr 无 QML 错误。
- VST3 重新导出 `Build/dist-win/VST3/Overteon.vst3`
  （35,509,248 字节，SHA256 `1130E9D20C612AFB66E392C67E08A39B4A86FFD815A0A11DBA55EA3068557F21`）。
- 需在宿主内确认：双击静音/恢复、主曲线回退、暗色频段曲线保留、节点空心状态。

---

## 三十四、电平表停止后底部残留亮点修复（2026-11-17）

### 现象

暂停/停止播放后，peak 尾迹缓慢回落，但偶尔会停在电平表最底部：绿色亮点不消失，直到把鼠标移到
综合强度表上或切换 Settings 才刷新掉。左右两个点都会出现。

### 原因

电平表的 sharp 层每帧重画，光晕层是缓存的。光晕重建键里的电平/峰值用 7 位量化
（`int(v * 127)`）：

- 峰值低于 1/127 ≈ 0.0079 后，量化值变成 0，光晕键**不再变化**；
- 但 cap dot 的绘制阈值是 0.002，所以最后一次光晕重建（峰值约 0.0079）里仍然带着这颗点；
- 峰值继续回落到 0 时，sharp 层的点已经消失，光晕键却不再变化，缓存的光晕就把那颗
  模糊亮点永久留在了底部。
- 悬停设置会改变 `m_settingsHover`，恰好改变光晕键并触发重建，所以看起来是“鼠标一放上去就没了”。

### 修复

- 光晕键增加**存在位**：level / peak 是否超过 0.002、谐波弧与限制弧是否超过 0.0005，
  与绘制阈值完全一致。
- 另外计算一个只包含这些存在位（以及悬停、尺寸）的 `m_glowStructKey`。出现/消失类变化
  **绕过 30 Hz 的模糊节流**立即重建光晕——因为消失的那一帧往往就是最后一帧，
  一旦被节流推迟，就再也没有下一次重绘来补建。
- 连续的电平变化仍然按 33 ms 节流，不影响性能。

### 验证

- Standalone 运行 7 秒无崩溃、stderr 无 QML 错误。
- VST3 重新导出 `Build/dist-win/VST3/Overteon.vst3`
  （35,509,760 字节，SHA256 `BF85A10D01AF52EA866490D993A1B5E1BE15B0A03F01AEF8D63D7097C558C9D3`）。
- 需在宿主内确认：暂停后两个 peak 亮点都能自行完全消失，不再需要悬停或切换面板。

---

## 三十五、peak 保持时间延长到 2.0 s（2026-11-17）

- `kPeakHoldSeconds` 取代原来硬编码的 1.1 s。peak 到顶后先保持 **2.0 s**，再按 0.18/s 线性回落；
  参考 Pro-Q 4 分析器的观感，保持段明显更长，但不会变成“记忆”。
- 回落速度、绘制阈值、sharp/光晕两层逻辑都没有改。
- Standalone 冒烟通过；VST3 已导出
  （35,509,760 字节，SHA256 `A89F96F729B8FA30C344F693907B89C1D55AACA541EEB3896B222F213BA1E653`）。
- 需在宿主内确认 2.0 s 的保持手感是否合适；若还要再长或稍短，只改这一个常数。

---

## 三十六、插件内按键转发给宿主（Space / 走带快捷键）（2026-11-17）

### 之前为什么没用

旧实现把未消费的键 `PostMessage` 回**插件自己的 JUCE peer**，注释假设 JUCE 会把未处理的键
再转给父窗口。实际路径是：消息回到本编辑器，JUCE 可能直接消费掉，宿主根本没收到；另外还有
`QGuiApplication::focusWindow() == view` 的前置判断，嵌入式 QWindow 不满足时连转发都不会发生。

### 现在按大厂模型实现

研究结论（FabFilter 官方论坛 + Steinberg VSTGUI 讨论）：插件窗口**保留键盘焦点**（Windows 上
鼠标滚轮需要），但只消费自己真正使用的键，其余交给**宿主窗口**。VSTGUI 的具体做法是记住
`SetFocus` 之前拥有焦点的窗口，把未处理的 `WM_KEYDOWN/WM_KEYUP` 转发给它。

对应到本插件：

- 新增只读的 `FocusTracker`（`QAbstractNativeEventFilter`），监听嵌入 QWindow 的
  `WM_SETFOCUS`，把 `lParam`（之前拥有焦点的窗口）记到 `lastHostFocus`。
- 转发目标优先级：
  1. `lastHostFocus` 的根 owner（仅当它属于当前进程，避免把键发给别的应用程序）；
  2. 插件窗口自己的根 owner；
  3. 插件窗口的直接父窗口。
  目标等于插件自身时放弃，绝不回灌自己。
- 仍然留在 Qt 的键：`Escape`（关闭 About / Settings / 数值框）、IME 组合键、以及任何
  TextInput/TextEdit 有焦点时的输入。其余（Space、字母、功能键、带修饰键的组合）全部转发。
- 转发使用 `PostMessageW`，保留扫描码、扩展键位、Alt 的 `WM_SYSKEY*`、自动重复位和
  key-up 状态位。
- **完全不碰焦点**：没有 `grabKeyboardFocus`、`SetFocus` 或焦点切换；鼠标事件路径未改。
  这是与上次回滚版本的关键区别。

### 调试

- `OVERTEON_KEYDBG=1` 时每次转发打印：
  `[key] vk <code> down/up  lastHost <hwnd>  target <hwnd>  plugin <hwnd>  posted <0/1>`。
  可直接看出过滤器是否执行、目标窗口是否合理、PostMessage 是否成功。

### 验证

- Standalone 运行 7 秒无崩溃、stderr 无 QML 错误。
- VST3 重新导出 `Build/dist-win/VST3/Overteon.vst3`
  （35,513,856 字节，SHA256 `D1FE94CD873534E025C4D1B4FA6BCB5AA0162F8F91054E5399AE00DFA2390221`）。
- 需在宿主内确认：
  1. 调整参数后直接按 Space，宿主播放/暂停；
  2. 左键点击、拖动节点/旋钮仍然正常（不应受键盘改动影响）；
  3. 数值编辑框打开时仍然可以正常输入；Escape 仍然关闭弹窗；
  4. 如果宿主无反应，用 `OVERTEON_KEYDBG=1` 查看 `target` 是否等于宿主主窗口。

---

## 三十七、A/B 首次切换不生效修复（2026-11-17）

### 现象

第一次点 B：槽位标记切到 B，但 EQ 参数和界面内容仍是 A；之后再在 A/B 之间来回切换就正常。

### 根因

`OvEngine` 构造时两个快照都是 `valid = false`。`setAbSlot()` 进入一个空槽时不会恢复，而是
**当场**把当前实时参数拷进去：

```cpp
if (! m_ab[slot].valid)
    m_ab[slot] = m_source->snapshotParams();   // 拷贝的是已经改过的 A
else
    m_source->restoreParams (m_ab[slot]);
```

所以第一次 A→B 拷到的是“此刻已经编辑过的 A”，值和界面完全一样，看起来什么都没发生；
只有 B 变成有效快照之后，后续切换才走 `restoreParams`，于是“恢复正常”。

### 修复

在 `OvEngine` 构造时，参数表建立完成后立即把**初始状态**同时记录到 A 和 B：

```cpp
m_ab[0] = m_source->snapshotParams();
m_ab[1] = m_ab[0];
```

- A 仍然是实时编辑的那一份；
- 第一次切到 B 会恢复“插件刚打开时”的状态，是真正的切换；
- 之后 A/B 来回切换、`A>B` / `B>A` 复制逻辑完全不变；
- 宿主自动化或预设加载后打开编辑器，两个槽都以当时的参数为起点，符合 A/B 的常规语义。

### 验证

- Standalone 运行 7 秒无崩溃、stderr 无 QML 错误。
- VST3 重新导出 `Build/dist-win/VST3/Overteon.vst3`
  （35,513,856 字节，SHA256 `0A59028941CB0C01DDBD91C4A9C5A16A560A44A8FC423B64F43DA4EEA3B962F4`）。
- 需在宿主内确认：打开插件 → 改 A → 第一次点 B，EQ/界面应立即变成初始状态；再切回 A
  应恢复刚才的编辑；后续来回切换正常。

---

## 三十八、替换 VST3 文件后宿主闪退（仅排查，未改动代码）

### 复现路径

宿主保持打开 → 加载旧 VST3 → 在宿主里删除插件 → 用新文件覆盖旧 VST3 → 再次加载 → 宿主闪退。
旧版本同样的操作不会闪退。

### 排查结论（未改动）

1. **Windows 不支持热替换已加载的 DLL。** 宿主删除插件实例后，通常不会立刻
   `FreeLibrary`，模块仍留在进程里（宿主缓存模块以便本次会话再次加载）。此时覆盖文件，
   磁盘上是新文件，进程里仍是旧映像；下一次加载/执行会出现“代码与文件不一致”，闪退。
2. **这一版在模块里保留了进程级 Qt 状态。** 编辑器关闭后，`QGuiApplication` 是**故意泄漏**的
   （一个进程一份），静态链接的 Qt、`QSG_RENDER_LOOP=threaded` 的渲染线程、
   `QWindowsIntegrationPlugin` 注册的窗口类/钩子都属于这个模块。插件实例删掉不等于这些
   状态消失；模块被覆盖后，这些残留指针指向的代码已经变了，下一次打开就可能在 Qt/OpenGL
   初始化路径上崩溃。
3. **旧版本没有这么重的进程级状态**（或宿主当时能干净卸载模块），所以同样的操作不会崩。
   这不是新代码“写错了”，而是“静态 Qt + 进程级 GUI 状态”与“热替换 DLL”本身不兼容。
4. 建议先用 Process Explorer / `GetModuleHandle` 确认：删除插件后 `Overteon.vst3` 是否仍在
   宿主进程中；以及宿主是否把插件复制到缓存目录再加载。若模块确实还在，结论就成立。

### 建议（未实施）

- **替换 VST3 时关闭宿主**，或替换后重启宿主；这是所有带进程级状态的插件的通用做法。
- 不要在宿主运行期间覆盖已加载的插件文件；Windows 的加载器不支持这种“热插拔”。
- 若将来必须支持热替换，需要改成“稳定的小加载器 + 可显式卸载的核心”，但静态 Qt 的卸载
  本身不安全，收益很低，不建议为此改架构。

（本节只记录排查结论，没有改任何代码。）

## 三十九、预设无限层级级联菜单 + EQ 节点悬浮框保持（2026-11-17）

### 问题 1：预设菜单只有一层，Factory 里的子文件夹扫不出来

原实现有两处写死：

- `OvJuceSource::presetListJson()` 只遍历 `Factory` / `User` 两个名字，且
  `RangedDirectoryIterator(..., false, ...)` 不递归；
- `OvPresetButton.qml` 把结果摊平成一列，没有"悬停展开子菜单"的概念。

所以 `presets/Factory/Drums/...` 不会出现，`presets/` 下新增的顶层文件夹也不会出现。

### 改动

- **扫描**（`OvJuceSource.cpp`）：`presetFolderToVar()` 递归扫描整个 `presets/`
  目录树，输出
  ```json
  { "name": "Factory", "folders": [ ... ], "presets": [ "Air Lift" ] }
  ```
  文件夹在前、预设在后，两级都按名称忽略大小写排序；隐藏项跳过。没有任何
  写死的文件夹名，`Factory` / `User` 只是启动时自动创建的两个普通文件夹。
- **加载**（`OvJuceSource::loadPresetNamed`）：`folder` 参数从"一级文件夹名"改成
  **相对路径**（如 `Factory/Drums`），逐段 `getChildFile()` 解析，仍然用
  `isAChildOf(presets/)` 防越界；`..` 直接拒绝。
- **引擎**（`OvEngine::refreshPresets`）：JSON 递归转成嵌套的
  `QVariantMap` / `QVariantList`，`presets` 属性保持 `QVariantMap` 不变，QML 直接
  按树访问。
- **菜单**（新增 `qml/OvPresetFolderMenu.qml`）：一个面板对应一个文件夹节点。
  文件夹行悬停即打开子面板：右侧放不下时自动翻到左侧，与父面板**贴边重合 2 px**，
  指针没有空隙可掉；子面板里再悬停文件夹会继续分出新面板，深度不设限。
  **"指针是否还在面板链里"由窗口级指针服务判定**（见下面的 OvPointer）：
  每次鼠标移动只做一次矩形命中，离开整条链 250 ms 后才收起下一层。
  Qt Quick 的 hover 只给最上层 item，父面板的 leave 和子面板的 enter 谁先谁后
  没有保证；依赖它会在指针已经进入子面板时把子面板销毁。
  根面板仍走 `OvMenu.show()`，所以点击空白、Esc、打开其他菜单都会关闭整条链
  （每个面板 `onVisibleChanged` 递归关闭自己的子面板）。
- **递归实例化**：Qt 不允许一个 QML 文件静态实例化自己
  （`OvPresetFolderMenu is instantiated recursively`），因此子面板改用
  `Qt.createComponent("OvPresetFolderMenu.qml", Component.PreferSynchronous)`
  在运行时创建，根面板仍由 `OvPresetButton` 的 `Component` 实例化。

### 问题 2：鼠标移到数值框上，框反而消失

`nodeEdit` 的"指针在框内"判断挂在 `neBlock.containsMouse` 上，而 `neBlock`
是**垫在三个数值字段和 ✕ 下面**的 MouseArea。Qt Quick 的 hover 只给最上层
item，所以指针一碰到可编辑字段，`neBlock.containsMouse` 就变成 false，
400 ms 关闭计时开始，框在用户手还没放到数值上时消失。

即使把判断移到面板级 `HoverHandler` 也不够：指针从频点移到框上时，视图收到
leave（启动 400 ms）和框收到 enter 的先后顺序 QML 不保证；一旦 leave 排在后面，
计时器会在"已经进入框"之后才启动。

### 改动

- 新增 `OvPointer`（`OvPointer.h/.cpp`）：**窗口级指针服务**。它在
  `QQuickView`（预览的 `QQuickWidget` 同样支持）上装一个事件过滤器，缓存
  `MouseMove` / `HoverMove` / `Enter` / `Leave` 给出的全局光标位置和
  "是否在窗口内"，变化时发出 `moved()`。`inside(item, margin)` 用
  `QQuickItem::mapToGlobal()` 把 item 转成屏幕矩形做命中检测。
  没有任何轮询，鼠标不动就不产生工作。
- `OvQmlEditor`：创建 `OvPointer` 并以 `OvPointer` 的名字注入 QML 上下文；
  预览的 QQuickView / QQuickWidget 两条路径同样注入。
- `OvEqSection.qml`：`nodeEdit` 订阅 `OvPointer.moved()`，每次移动判定一次
  外扩 8 px 的矩形；在框内就取消 400 ms 关闭计时并同步
  `setEditingPointerInside()`，`✕` 的显隐也读这个状态。
- `OvPresetFolderMenu.qml`：每个面板订阅同一个 `moved()`，更新
  `pointerInside`；`outsideTimer` 只在存在子面板且指针不在面板内时运行，
  触发时再用 `pointerInsideChain()` 确认整条链。
- 删除了 `OvEngine::cursorInside()` 和两个 60 ms 轮询定时器：指针/屏幕几何
  不属于参数模型，Engine 回到纯数据层。
- `neBlock` 只保留吞掉点击的职责，不再承担 hover 判断。

### 验证

- `Overteon_VST3` / `Overteon_Standalone` Release 构建通过。
- `qmllint` 对两个修改的 QML 文件无 error。
- 启动 Standalone 并加载 Qt 编辑器，stderr 无 `qml error`。
- 鼠标路径交互由用户实测；不再用截图/移动光标做自动验证。

产物：`Build/win-static/Overteon_artefacts/Release/VST3/Overteon.vst3`
（同时覆盖到 `Build/dist-win/VST3/Overteon.vst3`）

## 四十、自由缩放下的 EQ 越界曲线与刻度对齐修复（2026-11-17）

### 现象

窗口缩小时，同一条 EQ 曲线相对 dB 网格显得更高；窗口放大时，刻度反而比曲线长得快。
曲线本身和网格在代码里是同一套 `layoutGeometry()` + `toY()`，所以错的不是映射，而是
**越界部分**的尺度。

### 根因

1. **雾化层的 reach 被固定成了屏幕像素。** 参考实现的 `drawEQOverflow()` 里
   `range = 224` 是**设计像素**（页面是固定 1040×772 画布），会随整个设计缩放。
   Qt 版写成了 `224 / fitScale`（[main.qml](file:///c:/Users/Eonun/Documents/SupersawOmega/Overteon-0.9.20/Source/ui/qt/qml/main.qml)）
   和 `224 / displayScale()`（`OvEqView::fogRange()`）。窗口减半时，同样的 224 个
   屏幕像素覆盖的 dB 翻倍；窗口放大时反之。截图里 PQ 的雾一路冲进 SIDECHAIN 面板
   就是它。
2. **五条单带曲线没有 clip。** `paintOverlay()` 只给总曲线设了绘图区 clip，单带曲线
   离开范围后直接画到绘图区外；参考实现的 `drawCurve()` 是先 `clip()` 再画所有曲线，
   越界部分只交给雾。
3. **雾的重建签名不含视图尺寸。** `OvFogSource::signature()` 只有雾表面自身的尺寸；
   自由改变高宽比时，雾可能沿用旧的描边斜率。

### 改动

- `main.qml`：`fogRange` 改回 **224 设计像素**，去掉 `/ fitScale`。
- `OvEqView::fogRange()`：返回 `224.0`；`displayScale()` 因此没有任何调用者，删除。
- `paintOverlay()`：五条单带曲线加绘图区 clip，和总曲线、参考实现一致。
- `OvFogSource::signature()`：混入两个视图的 `width()` / `height()`，视图几何一变
  必然重建雾。
- **毛玻璃的模糊半径按设计像素**（与 0.9.10 的 12 段一致）：band 0 在离边缘
  约 19 px 内 `filter: none`，band 11 在 224 px 处 `blur(6.45 px)`。
  现在 `sigma = 0`、`sigmaSlope = 6.45`，模糊随越界距离增长；整个设计缩放时，
  小窗口的越界像素更少、模糊更小（更清晰）。上一版把它除以 `fitScale` 做成
  固定屏幕宽度，方向反了。
- **雾的渐隐改为"先模糊、后渐隐"**，与 0.9.10 的 `drawEQOverflow()` 一致：
  旧版先 `c.filter = blur(...)` 再用 `globalAlpha` 乘 `(1-k/12)^1.15`；
  之前是先按距离淡出、再模糊，能量被摊薄，雾显得更暗更虚。现在描边保持原色，
  `blur.frag` 的 `fade` 在模糊结果上乘同一组 `(1-t)^1.15`，保留实线核心。
  线宽、颜色、reach 都没有改。

### 验证

- `Overteon_VST3` / `Overteon_Standalone` Release 构建通过；启动无 `qml error`。
- 默认 / 最小 / 放大三种窗口尺寸下的视觉比例由用户实测。

产物：`Build/dist-win/VST3/Overteon.vst3`

## 四十一、全局过采样滤波器的通带推到 Nyquist 附近（2026-11-17）

### 现象

Plugin Doctor 在 44.1 kHz 下测到 20 kHz 之后直接掉下去。原因不是 EQ 的响应，
而是整条链路里的三处防折叠滤波：主过采样 `osN`/`osD`、限制器的过采样 `os`、
以及一个 63 抽头的 guard FIR（0.47 fs），后者连干声也一起切了。

### 改动（只动后端）

- `osN` / `osD` / 限制器 `os` 全部改为**显式构建**的半带 FIR：
  `transition = 0.019`、`stopband = -100 dB`、线性相位、整数延迟。
  通带约 **0.49 × 采样率**：44.1k 下约 21.6k，48k 下约 23.5k，96k 下约 47k。
  原来 JUCE 默认短核从 0.45 × 采样率（44.1k 下 19.8k）就开始掉。
- **删除** 0.47 fs 的 63 抽头 guard FIR。它作用在 wet 和 dry 两条路径上，
  是干声在 20.7k 被切的直接原因；新的过采样滤波器已经有 -100 dB 的阻带，
  guard 没有存在意义。删掉后 `firGuardLat` 一并移除。
- biquad 的频率钳位从 `0.45 × fs` 提到 `0.48 × fs`：44.1k 下允许到约 21.2k，
  之前 20k 的 band 会被钳到 19.845k。
- 频点参数上限（20k）和 UI **未改**，按本轮要求先只动后端。

### 代价

- 过采样滤波器更长，**延迟增加**（宿主会通过 PDC 补偿）；CPU 增加很小，
  因为半带 FIR 只有一半抽头非零。
- 44.1k 下物理上限仍是 22.05k；要“大幅延展”需要在 48k/96k 下测试。

### 验证

- `Overteon_VST3` / `Overteon_Standalone` Release 构建通过；启动无 `qml error`。
- 实际频响由用户用 Plugin Doctor 复测。

产物：`Build/dist-win/VST3/Overteon.vst3`

## 四十二、搁架过冲：隐藏的 Q 值（2026-11-17）

### 现象

Plugin Doctor 里，一个高频搁架 + 一个贝尔切除，20k 以上出现一个先顶高再掉的尖。
原本以为是 RBJ 拓扑在 Nyquist 附近的翘曲，准备换成 Orfanidis 设计。

### 数值验证后定位到的真正原因

`configBand()` 对所有类型都把 band 的 Q 传给滤波器，包括搁架。但 UI 里
`OvEqView::typeHasQ()` 对搁架返回 false，**Q 旋钮是禁用的**；Post EQ 的
band 4 默认 Q = 0.9，用户把 band 4 改成搁架后，这个不可见的 0.9 就成了
搁架的 Q。RBJ 搁架在 Q > 0.707 时会过冲：

- +6 dB 搁架、Q=0.9：21k 处约 +6.2 dB，再回到 +6；
- +12 dB 时过冲更明显。

这不是拓扑问题，换 Orfanidis 也不会消除一个 Q > 0.707 的搁架过冲。真正错的是
“用户可以控制的 Q”和“滤波器实际使用的 Q”不一致。

### 改动

- `configBand()`：搁架（LowShelf / HighShelf）固定使用 **Q = 0.707**
  （Butterworth 斜率）；反向补偿的 `pInv` 同步使用 0.707。
- 贝尔、陷波、高低切仍然使用 band 的 Q（这些类型的 Q 旋钮是启用的）。
- 链路顺序、过采样、旋钮、触发、UI 全部未改。

### 验证

- 数值扫频：Q=0.707 的 +6 dB 搁架在 15k…22k 单调收敛，无过冲；
  Q=0.9 才会出现 21k 附近的凸起。
- `Overteon_VST3` / `Overteon_Standalone` 构建通过；启动无 `qml error`。

产物：`Build/dist-win/VST3/Overteon.vst3`

## 四十三、频段上限提到 30 kHz；撤回搁架 Q 修改（2026-11-17）

### 改动

- **撤回**第四十二节的搁架 Q 固定：`configBand()` 的 LowShelf / HighShelf
  重新使用 band 的 Q（正向与反向补偿同步）。实测对 20k 附近的形状没有帮助。
- **EQ 频段上限 20 kHz → 30 kHz**（SC EQ 与 Post EQ 同步）。这是参数范围，
  不是宿主采样率——插件不能改宿主的采样率。
- 44.1 / 48 kHz 下，biquad 仍按 `0.48 × fs` 钳位（约 21.2 / 23.1 kHz），
  所以 30 kHz 的设置在低采样率下只是“拐点贴近 Nyquist”，20k 以内看到的是
  它的裙边；真正的 30 kHz 拐点需要宿主跑到 60 kHz 以上（96 kHz 最稳妥）。
- UI 未动：图的 x 轴仍是原来的范围，30 kHz 的 band 会落在图的右边界之外；
  参数本身已经可以设到 30 kHz（旋钮的归一化范围随之变化）。

### 验证

- `Overteon_VST3` / `Overteon_Standalone` 构建通过；启动无 `qml error`。
- 30 kHz 频段的实际听感/曲线由用户在高采样率下复测。

产物：`Build/dist-win/VST3/Overteon.vst3`

## 四十四、EQ 曲线在 20k 之后的虚假上翘（仅显示，2026-11-17）

### 现象

搁架 + 贝尔在 Plugin Doctor 里到 20k 是平的，但插件自己的曲线在 20k 右侧又往上翘。

### 原因

UI 的频率轴是 5 Hz–24 kHz，曲线一直算到 24k。biquad 的幅度在 20k 以上
接近 Nyquist 时会向上卷；但实际输出在 20k 以上没有这个卷（Plugin Doctor 到
20k 是平的，过采样也会在 21.6k 以上衰减）。所以那是**显示模型**的伪影，不是 DSP。

### 改动

- **频率轴改为 5 Hz–20 kHz**（原 5 Hz–24 kHz）。之前 20–24 kHz 这一段画的是
  数字 biquad 在 Nyquist 附近的卷边，而 Plugin Doctor 到 20k 就结束，所以
  看起来像“用户听不到、图上却翘起来”。轴结束在 20k 后，曲线自然在 20k 收尾，
  既没有卷边，也没有硬拉平的横线。
- 20k 标签自动向左收回两个像素，避免贴到绘图区右边界被裁掉。
- 频谱的时间/频率锚点注释同步：现在只有 5–20 Hz 是“无测量”区。
- **DSP 没有任何改动**；频段上限仍是 30 kHz，20k 以上放节点仍需要把 UI 轴
  真正扩展到 20k 以上（后置）。

### 验证

- `Overteon_VST3` / `Overteon_Standalone` 构建通过；启动无 `qml error`。
- 曲线右端应在 20k 自然结束，与 Plugin Doctor 的 20k 平台一致。

产物：`Build/dist-win/VST3/Overteon.vst3`

## 四十五、EQ 频段上限回调到 20 kHz，与 UI 轴一致（2026-11-17）

- SC EQ 与 Post EQ 的频点参数上限从 30 kHz 回调到 **20 kHz**；图的 x 轴也是
  5 Hz–20 kHz，所以节点、旋钮、自动化都不会跑到看不见的区域。
- 过采样、滤波链路、窗口、其他参数一概未动。
- 本节取代第四十三节的“30 kHz 上限”；30 kHz 的实验结论保留在那一节里备查。

产物：`Build/dist-win/VST3/Overteon.vst3`

## 四十六、EQ 节点数值框实时刷新 + 拖动方向与光标（2026-11-17）

- **实时刷新**：`OvEqSection` 新增对 `OvEngine::paramRevisionChanged` 的监听；
  节点数值框可见时调用 `nodeEdit.refresh(false)`。之前只有字段自己的 scrub 会
  刷新，所以在图上拖动节点、或用滚轮改值，框里三个数字不跟着变。
- **拖动方向**：频率字段改为**横向拖动**（`winX`），增益和 Q 保持纵向；
  光标形状分别是 `SizeHorCursor` / `SizeVerCursor`。
- **Q 的光标**：Q 没有有意义的拖动方向，拖动时通过 `OvPointer.setCursorHidden()`
  隐藏系统光标并停在原位，松开/取消后恢复。`Component.onDestruction` 也做了
  恢复，避免弹窗消失时把光标留在隐藏状态。
- DSP、参数范围、过采样均未改动。

产物：`Build/dist-win/VST3/Overteon.vst3`

## 四十七、拖动节点时的迟滞：缓存 biquad 系数（2026-11-17）

### 原因

曲线每一帧要在约 1000 个 x 位置上求值，每个位置对 5 条带各设计一次 biquad
（`rbj()` 里的 cos/sin/pow/sqrt）。一次拖动帧就是 **5000 多次滤波器设计**，
全部发生在 UI 线程的绘制里，节点自然跟不上鼠标。

### 改动

- 新增 `cachedCoefs()`：按 `(type, fs, f, q, dB)` 直接映射缓存 16 组系数。
  同一帧内每条带只设计一次，其余 1000 次只是查表 + `magDb()` 求值。
- 所有曲线、雾、节点求值都经过 `bandMagDb()`，所以一处改动同时生效。
- 数学、视觉、DSP、参数、UI 布局均未改动；只是不再重复计算相同的系数。

### 验证

- `Overteon_VST3` / `Overteon_Standalone` 构建通过。
- 拖动节点的手感由用户实测；若仍有迟滞，下一处是 overlay 纹理上传
  （2.4 MB/帧），需要单独做拖拽期降分辨率。

产物：`Build/dist-win/VST3/Overteon.vst3`

## 四十八、去重与结构整理（2026-11-18）

### 1. 过采样配方收成一份

- 新增 `Source/src/Oversampling.h`：`omega::initHighQualityOversampling()` +
  `kOversamplingTransition` / `kOversamplingStopbandDb`。
- 饱和链（`osN`/`osD`）与限制器（`os[4]`）都调用它。**两个过采样器仍然是
  独立对象、独立状态、独立倍率**，只是“怎么构造”不再有两份复制。
- 行为不变：仍为 0.019 过渡带、-100 dB 阻带、整数延迟、线性相位。

### 2. 系数缓存改为每个视图独立

- `OvEqView` 新增 16 格的 `m_coefCache`，`bandMagDb()` 从 static 改为 const
  成员。SC 和 Post 两个视图各自持有，不再互相挤掉缓存。
- 同时发现并删除了一处**真正的重复**：`OvEqView.cpp` 自己带了一份
  `rbj()`/`magDb()`，而 `OvBiquad.h` 本来就是给 `OvEqView` 和 `OvJackSource`
  共用的实现（文件头注释也这么写）。现在视图直接使用 `ovbq::*`，曲线和
  Jack 预览的输出频谱不可能再各算一套。
- 公式、类型表、数值与之前逐字相同，没有改声音。

### 验证

- `Overteon_VST3` / `Overteon_Standalone` 构建通过；启动无 `qml error`。
- 过采样常量与原先字面值一致；EQ 曲线数学与 `OvBiquad.h` 原实现一致。

产物：`Build/dist-win/VST3/Overteon.vst3`

## 四十九、数值框在拖动时原地不动（2026-11-18）

### 现象

拖动频率/增益/Q 字段时，节点跟着数值跑，弹窗也跟着节点跑，于是弹窗从指针下面
滑走；拖得远一点，hover/leave 规则判定“离开”，弹窗在拖动中途关闭。

### 改动

- `OvEqSection::place()`：字段 scrub 期间直接返回，弹窗保持按下时的位置；
  松开后下一次 16 ms 定时器重新锚回节点。指针、数字、弹窗三者始终在一起。
- `OvEqView::mousePressEvent()`：开始拖节点时置 `m_editScrub = true`，
  `endDrag()` 清零。这样**拖节点**（不是字段）时，hover 结束规则也不会在
  拖动中途触发关闭；弹窗仍然跟着节点走，因为节点就在指针下。
- 只改交互，DSP、参数、绘制均未动。

产物：`Build/dist-win/VST3/Overteon.vst3`




## 五十、About 介绍重写（2026-11-18）

### 改动

- 开场改为：Overteon 受被动式均衡器启发，并进一步进化——让染色不以动态为代价；
  饱和产生的谐波与干信号分离，仅在输出端合并，由 Post EQ 调优。
- 三个模式各自独立成段，不再挤在同一段里：
  - HARMONIC：完整设计。侧链 EQ 塑形饱和器输入，只提取新产生的谐波，干声经过
    同款空闲过采样器使合并相位精确，最后由 Post EQ 调优。
  - SCHRANZ：侧链 EQ 即输出，猛烈塑形后直接进限制器；饱和器与 Post EQ 被绕过，
    冲击、压缩感与过载染色来自限制器本身。
  - CLASSIC：传统路径。完整信号被饱和，按 MIX 与干声混合，再由 Post EQ 调优。
- 制作人员、网站链接、CLOSE 等内容保持原样。
- Qt 与 WebView 两个前端的 About 文案同步。

只改文案，DSP、参数、交互均未改动。

## 五十一、数值拖动：隐藏光标、弹窗跟随、松手回到数字（2026-11-18）

### 行为

- 频率 / 增益 / Q 字段越过 3 px 拖动阈值后，隐藏系统光标。
- 拖动期间弹窗实时跟随频点，和直接拖节点完全一致；不再冻结在按下时的位置。
- 松开鼠标：频率与增益先把弹窗锚回节点，再把光标放到当前数值文字的中心；
  Q 不移动节点，光标在原位恢复，行为不变。
- 取消拖动、弹窗关闭、组件销毁都会成对恢复光标，避免 BlankCursor 残留。

### 改动

- `OvEqSection::place()`：删除 `if (scrubbing) return`。`scrubbing` 只用于抑制
  hover/leave 关闭，并在 `close()` 时复位，避免弹窗中途关闭后永久抑制自动关闭。
- `OvNeField`：拖动开始时统一 `hideCursor()`；`onReleased` 对 f/g 调用
  `host.place()` 与 `OvPointer.warpTo()`，随后恢复光标；Q 保持原位恢复。
- `OvPointer`：新增 `warpTo(item, x, y)`（item 本地坐标映射到全局后
  `QCursor::setPos`）；`setCursorHidden` 保留成对 push/pop。

只改交互，DSP、参数和绘制未动。

## 五十二、Shift 微调：中途按下从当前位置开始（2026-11-18）

### 现象

拖动数值时按下 Shift（微调）会跳变：`startNorm` / `startX` 仍指向按下鼠标时的
位置，切换系数后整段行程按新速率重新计算。

### 改动

- 拖动中检测 Shift 状态变化，变化时把 `startX`、`startY`、`startNorm` 重新锚定
  到当前指针与当前参数值；微调从当前位置开始，松开 Shift 同样不跳。
- `delta` 在重锚之后重新计算，避免把旧行程带进新速率。
- 频率的微调系数从 0.0012 改为 0.0005（10 倍细于常规 0.005）；频率是对数轴，
  同一归一化步长在高频端代表的 Hz 大得多，因此单独收紧。增益 / Q 的微调保持
  0.0012。

只改数值字段的拖动手感，DSP、参数和绘制未动。

## 五十三、EQ 网格淡出与中部微高光（2026-11-18）

### 改动

- 两个 EQ 视图的网格不再是从头到尾等亮度的直线：
  - 竖向频率线自上而下淡出，中部最亮；
  - 横向 dB 线自左而右淡出，中部最亮；0 dB 线仍比普通 dB 线亮一档。
- 绘图区背景增加一个极低透明度的径向提亮（中心 7/255，55% 处 3/255，
  边缘 0），只在中间区域形成非常轻微的高光，不是一条硬边亮带。
- 实现全部在 `OvEqView::buildBackground()` 内。该图按几何与参数缓存
  （`m_bgKey`），渐变只在重绘背景时构造，正常播放/拖动帧不会重新计算，
  运行时开销为零。

只改背景绘制，曲线、节点、频谱和 DSP 未动。

## 五十四、网格提亮、中央光晕去色带、光标跟随微亮（2026-11-18）

### 网格亮度

`kMinor` / `kMajor` / `kHLine` / `kZero` 整体上调一档，两端淡出的部分不再完全
消失。频率集合与 dB 步长提取为文件级共享常量（`kGridMinors`、`kGridMajors`、
`gridDbStep`），背景和光标层用同一份数据。

### 中央光晕去色带

原实现是一整幅低透明度 `QRadialGradient`，8 位色深在暗背景上被量化成一圈圈
可见的色带。现在改为：

- 160 px 宽的低分辨率图，逐像素 `smoothstep` 衰减，加约 1 LSB 的确定性抖动；
- 用平滑插值放大到绘图区，插值把剩余台阶抹平；
- 叠加方式改为 `CompositionMode_Plus`，颜色取冷色
  （R:G:B = 0.62:0.80:1.0，峰值 12/255），中心同时提升亮度和饱和度，不发灰；
- 仍然只随背景缓存重建，正常帧零成本。

### 光标跟随微亮

- 以指针为中心、半径 105 px 的小图层，只重绘网格线：径向 brush 峰值
  alpha 24/255，50% 处 9/255，边缘 0，肉眼可见但很轻。
- 指针位置按 2 px 网格缓存，只在跨格时重建；hover 重绘按 16 ms 节流；
  指针离开绘图区立即隐藏，静止时零开销。
- 场景图路径新增一个始终挂载的 `QSGImageNode`，位于背景与频谱之间，
  自带 1×1 透明纹理；纹理替换沿用一帧延迟释放。CPU 绘制路径在背景之后
  绘制同一图层。

只改背景与网格绘制，曲线、节点、频谱和 DSP 未动。

## 五十五、去掉中央光晕；光标高亮取分区主题色；网格再提亮（2026-11-18）

### 改动

- 删除背景中央光晕：`buildBackground()` 中整块低分辨率径向提亮及其
  Plus 合成全部移除，背景回到纯色，不再有任何中央明度变化。
- 光标跟随高亮改用各视图自己的 `m_accent`：上方 EQ（红）掺红，下方 EQ（绿）
  掺绿。alpha 仍为峰值 24/255、50% 处 9/255、边缘 0；2 px 缓存、16 ms 节流
  与离开即隐藏的逻辑不变。
- `kMinor` / `kMajor` / `kZero` / `kHLine` 再上调一档：
  `#252a34` / `#313844` / `#3c4553` / `#282e38`。
- 同时删除 `OvEqView.h` 中随光晕一起失效的 `m_lightRect` 成员。

本轮修改文件：`Source/ui/qt/OvEqView.cpp`、`Source/ui/qt/OvEqView.h`。

只改绘制，曲线、节点、频谱和 DSP 未动。

## 五十六、光标静止继续提亮；Shift 微调解除屏幕边界限制（2026-11-18）

### 光标静止提亮

- 实测 Pro-Q 4：鼠标静止后，高亮在 0.2 秒内继续增长。这里实现为：
  静止 80 ms 后，用 200 ms 线性把网格高亮从 alpha 24/255 提到 36/255
  （50% 处 9 → 15）；鼠标一移动立即回落到基础值。
- 复用已有的 16 ms plot 定时器做这点算术，没有新增定时器；只有数值真正
  变化时才置 `m_dirty`，静止后不再重绘。
- 光标小图的缓存 key 增加 boost 档位（0..12），每档重建一次小纹理。

### Shift 微调被“限制住”

- 根因：旧实现用“当前指针 − 按下点”的绝对位移。频率的微调系数是
  0.0005 norm/px，走完 5 Hz–20 kHz 需要约 16,600 px，屏幕宽度远远不够，
  指针到边缘后位移不再增加，所以看起来被卡住。
- 修复：拖动期间把系统指针钉回按下时的全局位置（光标已隐藏，不可见），
  每次鼠标事件只累加手部增量，因此可以无限向两侧拖。
- 任意时刻按 / 松 Shift 都以当前值重新锚定，不会跳，也不会退回按下点；
  先拖一段再按 Shift 同样成立。
- Q 走同一套机制，可见行为不变（不移动节点，松开仍在原位恢复）。
- `OvPointer` 新增 `cursorPos()` / `warpGlobal()`；`OvNeField` 新增
  `lastX/lastY`、`accumX/accumY`、`anchorX/anchorY` 状态。

修改文件：`Source/ui/qt/OvEqView.cpp`、`OvEqView.h`、`OvPointer.cpp`、
`OvPointer.h`、`Source/ui/qt/qml/OvNeField.qml`。

只改高亮与数值拖动手感，DSP、参数和曲线未动。

## 五十七、重写数值拖动：去掉逐事件指针钉死（2026-11-18）

### 根因

上一版在每次鼠标事件后都把系统指针 `setPos` 回按下点。`setPos` 与鼠标事件
不是同步的：向右拖时事件可能仍停留在旧位置，而代码已经把基准重置到锚点，
同一次移动就被当成反向增量，于是数值来回抖；向左拖时尤其明显。这不是参数
映射的问题，是拖动状态机的基准点错了。

### 改动

- 删除逐事件钉死及相关状态（`warpPending`、`warpAt`、每事件重置基准）。
- 数值只由**增量累加**驱动：`accumX/accumY` 加上每次事件相对上一次真实位置
  的位移；向左向右、屏幕任何位置都对称。
- 指针只在离按下点超过 **220 px** 时重新居中一次，不再每帧干扰鼠标。
- 加跳变保护：单次事件位移大于 **120 px** 时视为指针归中（或其它不连续），
  只重新同步 `rawX/rawY`，**不改变数值**。归中动作永远不会被算成数值步进。
- Shift 中途按下/松开仍以当前值重锚；频率微调 0.0005、增益/Q 0.0012 不变。
- Q 继续使用同一套按钮逻辑，可见行为不变。

修改文件：`Source/ui/qt/qml/OvNeField.qml`。未动 DSP、参数和绘制。

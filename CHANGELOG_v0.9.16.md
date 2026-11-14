# Overteon v0.9.16 — 改动日志

记录时间：2026-11-11
上一版本：**`~/SupersawOmega.rar` 里的原始工程**（Overteon，WebView/HTML+CSS+JS 编辑器）
当前版本：**Overteon v0.9.16**（Qt6/QML 编辑器，RHI 渲染）
工作目录：`/home/x_x/build/Onyx-z/OnyxTEON`
基线提交：`59f03d7 Initial import: Overteon JUCE plugin (LV2/VST3/Standalone) with CMake+Ninja build`
当前提交：`5802aec Node editor: the box follows its node while a field is scrubbed`

---

## 一句话

**DSP 一个字节没动，界面从网页换成了 Qt6/QML，而且是在 GPU 上自己画的。**

58 个参数、`.hinj` 预设格式、均衡算法、侧链/PQ 结构、干湿与增益量程，全部与
原始工程逐位一致 —— 所以 `.hinj` 双向互通，旧预设直接能用。变的是编辑器：
它不再是 WebView 里跑的一张网页，而是一个 QtQuick 场景图，
均衡曲线、雾化、毛玻璃、辉光、电平表全部走 RHI 的
`updatePaintNode` + `QSGImageNode` / `QSGGeometryNode`，
没有 `QPainter`、没有 `QQuickFramebufferObject`、没有 X11/HWND/NSView。

规模：`59f03d7..5802aec`，81 个提交，1823 个文件，+141,798 / −35 行。

---

## 一、版本与产品身份

产品身份保持**原版的**那一套 —— 名字、URI、bundle id、插件代码全部沿用，
所以用旧版存过的工程打开后不需要重映射，`.hinj` 预设双向通用。

| 项目 | 值 |
|------|-----|
| 产品名 | `Overteon` |
| LV2 URI | `https://eonun.com/plugins/Overteon` |
| bundle id | `com.eonun.overteon` |
| 插件代码 | `Ovtn` |
| bundle 名 | `Overteon.lv2` / `Overteon.vst3` |
| 预设目录 | `~/Eonun/Overteon Presets` |
| QML import 命名空间 | `Overteon 1.0` |
| wordmark | `OVER\|TEON` |
| 参数数量 | 58（与原版完全相同） |
| 编辑器技术 | Qt6/QML + RHI 场景图 |
| 许可证 | BSD（仅动态链接 LGPL 的 Qt 模块） |

> 因为身份就是原版的那一套，这个构建和原版 WebView 构建**不能并排安装** ——
> 二选一。开发期为了让两者共存曾用过独立的 `ONYXTEON` 身份，现已取消。

---

## 二、UI 架构：WebView → Qt6/QML

- **`812bbf4`** 独立产品身份，Qt6/QML 编辑器与 HTML 里的真实 dual-Kawase 模糊并存
- **`bb05285`** 编辑器整体重写为 QtQuick
- **`c2a33eb`** 真实 dual-Kawase 玻璃、可用的 EQ 编辑、宿主给尺寸的窗口
- WebView 那条路径彻底移除：不再需要 QtWebEngine，不再需要往 JS 里塞桥

### 渲染必须走 RHI（硬约束）

Qt 6 **没有** RHI 版的 QPainter 引擎 —— 只有 `raster`（CPU）和
`QOpenGL2PaintEngineEx`（GL）。所以"用 QPainter 画到 GPU 上"永远等于强制 OpenGL，
而 `QQuickFramebufferObject` 也是 OpenGL-only。目标用户是 Win/Mac，
因此所有自绘内容改用场景图的两种节点：

- `QSGImageNode` 承接缓存图层（文字、渐变底、辉光贴图），只在关键参数变化时重绘
- `QSGGeometryNode` 承接动态几何，渐变变成逐顶点颜色

- **`1b74821`** EQ 曲线与雾化改为场景图几何，去掉 QPainter 与 OpenGL
- **`db77026`** 均衡图搬到 GPU，`--solo` 下显示每个控件各自的帧率

期间踩到并记录的三个坑：`QSGSimpleTextureNode` 在
`QSGOpaqueTextureMaterial::compare` 里首帧崩；零顶点的 `QSGGeometryNode` 同样崩；
`QSGImageNode` 不拥有纹理，必须晚一帧 release。

---

## 三、毛玻璃与辉光

网页里有 `backdrop-filter`，**插件宿主根本不认**，所以这两层玻璃是自己算的。

- **`aa663f9`** 严格复刻原网页 FrostEngine：半分辨率、7 抽头双高斯
- **`b625c36`** 接缝玻璃真正画出来；假信号换成了真实音频
- **`272a4e0`** 玻璃不再反射曲线；辉光改成 CSS 的椭圆
- **`2a6522c`** 表头 pod 上的真实高斯光晕；整棵树共用一份模糊实现
- **`ee960db`** pod 光晕按 CSS 的 sigma 走；热区是 sigma 4，不是 2.5
- **`bca9755`** 修好"玻璃缩到没有"
- **`e80c77a`** 帧成本从单核 91 % 降到 37 %
- **`115c27b`** GPU 玻璃可用；记录了让它看起来是坏的四个原因
- **`e265ece`** CPU 与 GPU 模糊在同一张照片上误差 2/255
- **`b9bd9bf`** 对照用的模糊 demo（左原图 / 右模糊）
- **`b5db3af`** 可用的 GPU 双高斯接进编辑器玻璃
- **`983c758`** 玻璃就地采样、按设备分辨率、按网页的绘制顺序
- **`c51c29b`** 两个控制区各加一条辉光横条
- **`8ad9999`** 删掉 FrostEngine，从此以原网页为准做对比
- **`940fec1`** 雾化距离符号修正，真的会模糊和淡出
- **`5a791bf`** 雾化两半共用一张表面，去掉无谓重绘
- **`7e0c1b9`** 溢出雾化按屏幕尺寸而非设计尺寸
- **`90f5022`** 溢出雾化改成真正的双高斯
- **`d9bb7b0`** 雾化按帧率重绘，不再画原始描边
- **`f70f71e` / `841b4f8` / `dc1cb1e`** 颜色真正生效、抗锯齿边缘、预乘正确的梯度
- **`0558293`** 抗锯齿描边、先淡出再模糊、纹理不泄漏
- **`7e59762`** 接缝玻璃走 RHI 路径；`--solo` 支持逐控件截图

---

## 四、EQ 编辑

- **`28c3ace`** 修好节点抓取（`layer.enabled` 吃掉了事件）；辉光跟随电平已验证
- **`dfad340`** 修好节点拖动卡死
- **`dcdf567`** 上下条辉光按网页的方式画
- 命中半径统一 `kNodeHitRadius = 12.0`
- 滚轮调 Q 有 1.2 s 抓取锁定（`m_wheelLockBand` / `m_wheelLockUntil`），
  避免扫过一个点就把邻居的 Q 改掉
- 频率轴映射是 `timeRange(5, 20000, 800)`（`setSkewForCentre 800`，skew ≈ 0.214936），
  **不是纯对数** —— 这一条被单独核对过，因为按对数实现时低频段手感完全不同

---

## 五、电平表与频谱：这一轮最较真的部分

原先的表头是"漂亮但编出来的"。这一轮把它换成了有依据的算法。

- **`93b4cb8`** 所有表走同一条曲线：时间用 smootherstep，轴用 Catmull-Rom
- **`f3fe07e`** 真正的 RMS 检波器，以及它蕴含的回落速度（11.8 dB/s，不是 57）
- **`aa7b620`** 分析器：在功率域平均，半倍频程宽，16.6 dB/s
- **`4d8cc80`** 按网页速度走：两个时间常数，都不是我自己发明的
- **`aca4808`** 回退上一提交（理由记录在提交信息里）
- **`37b8334`** 所有显示速率整体快 25 %：每个时间常数 ÷ 1.25

### 频谱两端

用户两次指出"频谱没了"和"20 Hz 左侧有一个硬的切面"，这里有两个真 bug：

- **`5ab884d`** 最低的真实 bin 就保持不变，不要凭空补一条到地面线的斜坡
- **`4ec2252`** 填充在轴的两端都锚到绘图区底部

原因是 20 Hz 附近没有真实 bin 时，代码用了一条插值斜坡去"补"，那条斜坡在
20 Hz 处形成一个硬折角；左侧则是填充从零开始，看起来像图断了。两处都改成
锚在绘图区底边，视觉上连续，且没有编造数据。

### 一个读数陷阱

`jack_cpu_load` 输出的是**百分比**，`1.3` 就是 1.3 %，不是 131 %。
我按 131 % 读过一次，浪费了好几轮"优化"。

---

## 六、预览程序开始跑真实 DSP

- **`0759d70`** 预览宿主真实 DSP：两路频谱取样都是**测出来的**，不是建模的

在此之前，预览里的频谱和电平表是照着网页行为模拟的 —— 于是"界面看起来对，
插件里不对"这类问题永远查不出来。现在预览与插件共用同一份 DSP 代码，
频谱/电平/增益约简全部来自真实处理链路。

- **`030135b`** 预览能像插件一样加载页面里的 `.hinj` 预设
- **`a46bd55`** 预览服务器保留页面给定的参数
- **`704add2`** LV2 打包 + 用 jalv 播真实音频的基准测量

---

## 七、`ovctl`：可重复的测试通道

- **`efe6184`** 雾化重新跟随实时拖动；`ovctl` 通过 MIT SHM 驱动参数
- **`1259985`** `ovctl` 改为**附着**而非创建，命令之间不再互相擦除
- **`307f8d1`** `OVERTEON_SOLO_OPACITY`，单独看某个控件时能透视到下层

```sh
./build/overteon-qml-preview --size 1040x772 --preset /tmp/flat.hinj --ctl ovctl
./build/ovctl --name ovctl set sc1Gain 18 --tau 0.4
./build/ovctl --name ovctl sweep pq1Gain -12 18 --time 3 --step 0.03
```

预览按 8 ms 一步、指数逼近目标值（`v += (target-v)*(1-exp(-dt/tau))`），
所以界面看到的是拖动而不是跳变 —— 这样测出来的帧率和手感才有意义。

---

## 八、编辑器尺寸、DPI 与宿主嵌入

这一节是"在宿主里根本用不了"到"在 Ardour 里正常"的全过程。

- **`76359a5`** 编辑器跟随桌面缩放，不再自己藏起来
- **`7f92c02`** 按设备分辨率嵌入；每次测量只采样一次分析器
- **`0258f3b`** 设计尺寸要**乘上**桌面缩放，子控件与之一致
- **`0454b85`** 用日志证明尺寸，并把两个像素空间分开
- **`8d2abd8`** 日志说的才是真的 —— 读它，按它修
- **`ab791df`** 界面填满窗口，而不是被裁到设计尺寸
- **`92d04e1`** 按 Qt 官方文章的方式嵌入：一个 JUCE Component
- **`4d06b7f`** 能在 Ardour 里加载了，并填满宿主给的缩放后框架

`4d06b7f` 的根因值得一提：Ardour 的 LV2 宿主通过 `suil` 走 glib，
而我们的替身 shell 少了 15 个 glib 符号（`ui/qt/OvGlibShim.cpp`），
补上之后窗口才真正出现。另外宿主要缩放后的尺寸，内部 Qt 窗口仍是小尺寸，
修法是 `localToGlobal` 之后按比例铺满。

- **`665e0fa`** HiDPI 上均衡图被缩放两次
- **`b92c2ab`** 工具提示不再让每个拖动事件中断；窗口适配屏幕

---

## 九、数值编辑窗（双击参数弹出的那个小窗）

网页版双击频率点会弹出一个数值窗，可以拖、可以输入。这一轮 1:1 复刻了它。

- **`f65d547`** 复刻 WebUI 的双击数值弹窗
- **`f783ff5`** 让三个输入框真正能用
- **`92b98c7`** 数值文字在**任何**显示缩放下都能放进框里
- **`86cade2`** dB 字段的拖动是个反馈环 —— 拖动时写值触发了重绘，重绘又改回值
- **`5802aec`** 拖字段时框跟着节点走

实测灵敏度与原网页一致：dB 轴 **0.1199 dB/px**（网页 0.12），
频率轴 16 px → Δnorm 0.0800。

原先的问题：`TypeError: Cannot assign to read-only property "text"` —— QML 里
把 C++ 侧的只读属性当可写用，三个框既拖不动也不显示值。

---

## 十、与原版 WebUI 的逐项审计

用户要求"看看有没有和原网页不一致的地方"（只谈不一致，不谈性能改进）。
两轮审计各自定位并修正了具体条目：

- **`122b94a`** 五处视觉漂移
- **`2f62977`** 四处交互与原网页不同

一个必须先解决的前提：**原网页只嵌了 EDIX 一种字体，`Inter` 靠系统安装**。
测试机上没装 Inter 时，整页 **9.43 %** 的像素不同 —— 也就是说在装 Inter 之前，
所有"文字不一样"的对比结论都是 Inter vs DejaVu 的字体差异，不是移植错误。
装上 Inter 之后重新对比，剩下的才是真的。

---

## 十一、构建系统与开关

- **`b7fedce`** 支持 VST2：内置 VST2 接口头并接进 CMake
- **`121953c`** `bench.py` 的 `-Gj` / `-Gc`，并铺满宿主给的窗口
- **`4140c07`** 调试开关**按值**解析，所以 `NAME=0` 真的是关掉
- **新增** `OVERTEON_RESIZABLE_WINDOW`（默认 **OFF**，见下）

### `OVERTEON_RESIZABLE_WINDOW`

```sh
cmake -DOVERTEON_RESIZABLE_WINDOW=ON ..   # 允许宿主拉伸编辑器
```

**默认关闭。** 界面是按 1040×772 这个设计尺寸画的，并且会等比铺满宿主给的
任意框架 —— 所以它技术上能活在任意尺寸里。但设计尺寸就是这个产品的样子，
被拖到 400×300 的窗口是"看不清"而不是"灵活"。所以它打包成固定尺寸：
插件按设计尺寸向宿主申请，并声明窗口不可缩放。

这是个**编译期**决定，不是运行时环境变量 —— 固定构建不可能被任何东西拉开，
这正是把它做成编译开关的意义。

关与开在构建产物里的差别是可查的：

| 构建 | 生成的 `Overteon.lv2/ui.ttl` 里 |
|------|--------------------------------|
| 默认（OFF） | `lv2:optionalFeature ui:noUserResize` |
| `-DOVERTEON_RESIZABLE_WINDOW=ON` | `lv2:optionalFeature ui:resize` |

（JUCE 的 LV2 侧按 `editorInstance->isResizable()` 三选一写进清单；
VST3 侧对应 `IPlugView::canResize()`。）

注意：`jalv.gtk3` **不读**这个声明，它的框架在两种构建下都能拖 ——
所以拿 jalv 的 `WM_NORMAL_HINTS` 验证这个开关是验不出来的（我试过，两态一样）。
判据要看 `ui.ttl`，或者用真正会询问 `canResize()` 的宿主。

### 同一个开关也管预览程序

`overteon-qml-preview` 是独立的 `QQuickView`，JUCE 的 `setResizable` 到不了它那里，
所以它原先能拖成任意形状。

| 构建 | 预览窗口 |
|------|----------|
| 默认（OFF） | 固定在设计比例上（`min == max`，没有可拖的边） |
| `-DOVERTEON_RESIZABLE_WINDOW=ON` | 可以拖，但比例锁死（`AspectLock` 事件过滤器） |

比例必须锁住：`main.qml` 是把整个设计**等比**塞进给定框架的，窗口换成别的形状
只会得到一圈空白，而不是更大的界面。

实测（嵌套显示 + openbox，因为裸 X 会忽略尺寸提示）：

- 固定构建：请求 1400×500 → 被夹回 1040×772
- 可调构建：1400×500 → 674×500；800×800 → 1078×800（比例 1.347 = 1040/772）
- 两种构建都守住 520×386 下限

---

## 十二、Windows / wine

- **`a782805`** 用 clang-cl + xwin 交叉编译 Qt UI 到 Windows，在 wine 里跑起来
- **`a16d214`** 部署 Qt 运行时，去掉死掉的 Shapes import
- **`c33194c`** 修冻住的界面：变更即重绘；给 wine 一个兼容窗口
- **`c8f0b29`** wine 下选 OpenGL RHI，走 `QQuickWidget` 渲染
- **`692d653`** `demo.sh` 加一个 PID 管理的 wine 目标

---

## 十三、工具

- **`bench.py`**：`-Gs` 场景图、`-Gj` jalv、`-Gc` 命令行。测量必须带 `-Gs`
- **`demo.sh`**：所有目标按 PID 管理，起停不靠 `pkill`
- **`c8d1571`** HUD：一个直接把命中测试结果读出来的小窗
- **`3dbd4b8`** 统一内部帧率 120 Hz；一次拖动 0.8 ms（原先 3.2 ms）

---

## 十四、兼容性

| 项目 | 状态 |
|------|------|
| `.hinj` 预设 | 双向互通（参数 ID 未变） |
| 参数数量 | 58，完全相同 |
| DSP | 未改动 |
| LV2 / VST3 / VST2 / Standalone | 均已构建并安装 |
| 宿主 | Ardour 已验证可加载；jalv 全流程可跑 |
| 显示 | 高 DPI 与普通 DPI 均按设备像素渲染 |
| 平台 | Linux（已验证）、Windows（wine 中已验证）、macOS（未测） |

---

## 十五、已知限制 / 未闭环

1. **中央表头 `OvMeterView` 仍是 `QQuickPaintedItem`。** 光晕在 `paint()` 里构建，
   完整 RHI 迁移试过一次已回退。这是唯一还留在 CPU 光栅路径上的自绘控件。

2. **`OVERTEON_RESIZABLE_WINDOW=ON` 的实际拉伸行为未在真实宿主里跑过。**
   已验证的是：开关翻转构建产物、`ui.ttl` 按预期改变、两种构建都能加载并出界面。
   没验证的是"宿主把窗口拉大之后布局是否如预期" —— `jalv.gtk3` 不看这个声明，
   需要换一个会询问 `canResize()` 的宿主。

3. **`console.log` / `console.warn` 在这些 release 构建里不输出**，不能当证据。
   可靠手段只有：`[hit]` 追踪、C++ 侧 `fprintf`、把状态渲染进控件再截图。

4. **macOS 完全未测试。**

5. **AUTORCC 偶发不重建。** 改了 QML 之后 ninja 有时认为无需重跑 rcc，
   二进制里还是旧界面。判断同步不能只看时间戳，要比哈希或者 `touch` 一下 QML。

---

## 十六、构建与运行

```sh
# 配置（首次或改了 CMakeLists.txt）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 编译
ninja -C build

# 只有 Qt 编辑器（不编 JUCE，交叉编译也用这个）
cmake -S . -B build -G Ninja -DOVERTEON_PREVIEW_ONLY=ON

# 允许宿主拉伸编辑器（默认关）
cmake -S . -B build -G Ninja -DOVERTEON_RESIZABLE_WINDOW=ON

# 跑预览
./build/overteon-qml-preview --size 1040x772 --preset /tmp/flat.hinj --ctl ovctl
```

产物：

```
build/HarmonicInjector_artefacts/Release/LV2/Overteon.lv2/libOverteon.so
build/HarmonicInjector_artefacts/Release/VST3/Overteon.vst3/Contents/x86_64-linux/Overteon.so
build/HarmonicInjector_artefacts/Release/VST/libOverteon.so
```

安装（VST2 的文件名必须是 `<Name>.so`，不能是 `libOverteon.so`）：

```sh
cp build/HarmonicInjector_artefacts/Release/LV2/Overteon.lv2/*    ~/.lv2/Overteon.lv2/
cp build/HarmonicInjector_artefacts/Release/VST3/Overteon.vst3/Contents/x86_64-linux/Overteon.so \
   ~/.vst3/Overteon.vst3/Contents/x86_64-linux/
cp build/HarmonicInjector_artefacts/Release/VST/libOverteon.so ~/.vst/Overteon.so
```

### 运行环境的几条硬性规定

- JACK 测试**必须**带 `JACK_DEFAULT_SERVER=ovtest`（默认服务器是用户的声卡）
- 测 GUI **必须**用 `env -i DISPLAY=:N PATH=... /bin/sh -c '...'`；不要 `vglrun` 开 GUI
- 起停进程**只能按 PID**，不要 `pkill` / `killall`

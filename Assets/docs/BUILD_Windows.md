# Overteon 0.9.20 — Windows x64 VST3 完整构建与导出指南

记录日期：2026-11-16

适用源码：`Overteon-0.9.20`

目标产物：Windows x64 VST3，Qt/QML 静态链接，不附带 Qt DLL 或 VC++ 运行库 DLL

本文面向没有插件开发经验的操作人员。按顺序完成环境检查、静态 Qt 编译、
Overteon 编译、产物检查和宿主验证即可。命令默认在 **PowerShell** 中执行。

> 本文是对已经成功流程的技术记录。本次补写文档没有重新构建插件，也没有改变
> 当前 VST3、系统安装目录或任何预设。不要把文档中的示例哈希当作每次重新编译后
> 都必须逐字节一致的要求。

---

## 1. 最终方案与边界

Overteon 使用两层技术：

- **JUCE 8.0.13**：DSP、参数、VST3 接口和宿主交互。
- **Qt 6.9.3 / QML**：插件编辑器。

最终 Windows 方案不是“加载壳 + 核心 DLL”，也不是 `windeployqt` 收集的一批
Qt DLL，而是：

1. 用 `-static -static-runtime` 编译 Qt。
2. 将 Qt、QML 插件、Windows 平台插件和 MSVC CRT 静态链接进 VST3。
3. 将 QML、字体和 shader 通过 qrc 资源编入二进制。
4. 保留 JUCE 生成的标准 VST3 输出；需要独立文件时，交付其中的
   `Contents/x86_64-win/Overteon.vst3`。

最终 VST3 不需要：

- `OverteonCore.dll`；
- `Qt6Core.dll`、`Qt6Gui.dll`、`Qt6Qml.dll`、`Qt6Quick.dll` 等；
- `platforms/`、外部 QML import 目录；
- `MSVCP*.dll`、`VCRUNTIME*.dll`；
- 用户另外安装 Qt；
- `QT_PLUGIN_PATH`、`QML2_IMPORT_PATH` 或修改系统 `PATH`。

### 1.1 “单文件”和“目录包”不要混淆

Windows 上 VST3 有两种常见交付形态：

- **标准目录包**：`Overteon.vst3/` 是文件夹，内部有 `Contents/`。
- **独立二进制**：`Overteon.vst3` 本身就是一个 PE/DLL 文件。

本项目 JUCE 的原始构建输出是标准目录包。原有预设地址
`Overteon.vst3/Contents/presets` 属于这种目录包；如果现有包内已有该目录，
交付和升级时必须整体保留，不能删除层级，也不要用 `/MIR` 清空目标目录。
独立二进制方案则只复制内部的 VST3 可执行模块，不应借此删除其他已有预设备份。

本文只说明两者关系，不要求改变当前已经验证成功的产物。

---

## 2. 源码目录与构建目录

以下结构以项目根目录 `Overteon-0.9.20/` 为基准。

### 2.1 必须保留的源码和数据

```text
Overteon-0.9.20/
├─ Config/
│  ├─ CMakeLists.txt                 # 主构建入口
│  ├─ CMakePresets.json
│  └─ cmake/                         # 其他工具链文件
├─ Source/
│  ├─ src/
│  │  └─ OverteonProcessor.cpp       # JUCE DSP、参数和插件入口
│  ├─ ui/qt/
│  │  ├─ OvQmlEditor.cpp/.h          # Qt 编辑器与 JUCE 的连接
│  │  ├─ OvQtComponent.cpp/.h        # Qt 内容嵌入宿主窗口
│  │  ├─ OvJuceSource.cpp/.h         # 参数、频谱和预设数据源
│  │  ├─ ov_ui.qrc                   # QML、字体等资源清单
│  │  ├─ qml/                        # QML 页面
│  │  └─ shaders/                    # qsb 的 GLSL 源文件
│  └─ third_party/
│     └─ JUCE/                       # vendored JUCE 8.0.13，离线构建所需
├─ Assets/
│  ├─ presets/
│  │  ├─ Factory/                    # 工厂 .hinj 预设
│  │  └─ User/                       # 用户预设位置
│  └─ docs/                          # 版本日志和构建文档
└─ Build/                            # 所有编译输出，不是源码输入
```

不要删除 `Source/third_party/JUCE`。默认配置
`OVERTEON_VENDORED_JUCE=ON`，存在该目录时 CMake 不需要联网下载 JUCE。

### 2.2 本次使用的构建输出目录

```text
Build/
├─ qt-static-build/                  # Qt 的编译中间文件
│  ├─ config.opt                     # 本次 Qt 配置参数记录
│  ├─ config.summary                 # Qt 功能与编译器摘要
│  └─ .ninja_log                     # Ninja 步骤记录
├─ qt-static/                        # Qt 静态安装前缀
│  ├─ bin/qsb.exe
│  ├─ include/
│  └─ lib/
│     ├─ Qt6Core.lib、Qt6Quick.lib 等
│     └─ cmake/Qt6/Qt6Config.cmake
├─ win-static/                       # Overteon 的构建目录
│  └─ Overteon_artefacts/Release/
│     ├─ VST3/Overteon.vst3/         # JUCE 标准目录包
│     └─ Standalone/Overteon.exe     # 可选的独立验证程序
└─ dist-win/VST3/
   └─ Overteon.vst3                  # 本次交付的独立二进制文件
```

JUCE 标准 VST3 输出为：

```text
Overteon.vst3/                       # 这里是目录
└─ Contents/
   ├─ Resources/
   │  └─ moduleinfo.json
   ├─ presets/                       # 若原包已有，原样保留
   │  ├─ Factory/
   │  └─ User/
   └─ x86_64-win/
      └─ Overteon.vst3               # 这里才是宿主加载的二进制
```

`Contents/x86_64-win/Overteon.vst3` 与外层目录同名是 JUCE/VST3 的正常结构，
不是重复打包错误。真正需要避免的是额外多套一层
`Overteon.vst3/Overteon.vst3/Contents/...`。

---

## 3. 准备构建环境

### 3.1 操作系统

- Windows 10/11 x64；本次实际使用 Windows 11。
- 为 Qt 源码、中间文件和静态库预留足够磁盘空间。
- 建议路径只使用普通 ASCII 字符，并尽量缩短路径。MSVC 在很深的 Qt qrc
  自动生成路径上可能触发路径长度问题。

### 3.2 安装 Visual Studio

本次成功环境：

- Visual Studio 2026 Community；
- MSVC `14.51.36231`；
- VS 自带 CMake 和 Ninja。

官方下载：<https://visualstudio.microsoft.com/downloads/>

在安装器中选择 **使用 C++ 的桌面开发**，并确认至少包含：

1. MSVC x64/x86 生成工具；
2. Windows 10 或 Windows 11 SDK；
3. 适用于 Windows 的 C++ CMake 工具。

其他 Visual Studio 版本也可以尝试，但必须保证 Qt 和 Overteon 都使用
**同一种 MSVC 架构、Debug/Release 模式和 CRT 方式**。不要混用 MinGW。

### 3.3 安装 Qt 6.9.3 源码

Qt 官方下载：<https://www.qt.io/download-qt-installer>

在 Qt 在线安装器中展开：

```text
Qt
└─ Qt 6.9.3
   └─ Sources
```

需要的是完整的 **Sources** 组件，不是 `Qt Insight Tracker` 源码。
本次安装后的源码位置为：

```text
C:\Qt\6.9.3\Src\
├─ configure.bat
├─ qtbase/
├─ qtshadertools/
├─ qtdeclarative/
└─ qtsvg/
```

静态编译 Overteon 时，运行库来自后面生成的 `Build/qt-static`，不依赖
`C:/Qt/6.9.3/msvc2022_64/bin` 中的动态 Qt DLL。

### 3.4 初始化 PowerShell 变量

打开 PowerShell，先设置路径。换机器时只需修改 `$Project`、`$QtSrc` 和
VS 安装路径，不要在 CMake 文件中写死这些本机路径。

```powershell
$Project  = "C:\Users\Eonun\Documents\SupersawOmega\Overteon-0.9.20"
$QtSrc    = "C:\Qt\6.9.3\Src"
$QtBuild  = Join-Path $Project "Build\qt-static-build"
$QtPrefix = Join-Path $Project "Build\qt-static"
$Build    = Join-Path $Project "Build\win-static"

$VsTools = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools"
$CMake   = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$NinjaBin = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
```

如果安装的是其他 VS 版本，通过资源管理器找到实际的
`Common7/Tools/Launch-VsDevShell.ps1` 和 `cmake.exe`，再替换上面的路径。

### 3.5 初始化 MSVC x64 环境

在同一个 PowerShell 窗口中执行：

```powershell
& (Join-Path $VsTools "Launch-VsDevShell.ps1") -Arch amd64 -HostArch amd64
$env:Path = "$NinjaBin;$env:Path"
```

检查输入文件和工具：

```powershell
Test-Path (Join-Path $Project "Config\CMakeLists.txt")
Test-Path (Join-Path $Project "Source\third_party\JUCE\CMakeLists.txt")
Test-Path (Join-Path $QtSrc "configure.bat")
Test-Path $CMake

Get-Command cl
Get-Command ninja
& $CMake --version
cl 2>&1 | Select-Object -First 2
```

前三个 `Test-Path` 必须都返回 `True`。`cl` 应显示 x64 的 Microsoft C/C++
编译器。若找不到 `cl`，不要继续编译，先重新执行 VS 环境初始化。

---

## 4. 第一步：编译静态 Qt 6.9.3

### 4.1 配置

本次 `config.opt` 中保存的实际参数如下，PowerShell 可直接执行：

```powershell
New-Item -ItemType Directory -Force -Path $QtBuild, $QtPrefix | Out-Null

Push-Location $QtBuild
& (Join-Path $QtSrc "configure.bat") `
  -prefix $QtPrefix `
  -static `
  -static-runtime `
  -release `
  -opensource `
  -confirm-license `
  -nomake examples `
  -nomake tests `
  -opengl dynamic `
  -no-openssl `
  -no-feature-quickcontrols2-basic `
  -no-feature-quickcontrols2-fluentwinui3 `
  -submodules qtbase,qtshadertools,qtdeclarative,qtsvg `
  -- `
  -DBUILD_qtbase=ON `
  -DBUILD_qtshadertools=ON `
  -DBUILD_qtdeclarative=ON `
  -DBUILD_qtsvg=ON
$ConfigureExit = $LASTEXITCODE
Pop-Location

if ($ConfigureExit -ne 0) { throw "Qt configure failed: $ConfigureExit" }
```

参数作用：

| 参数 | 作用 |
|------|------|
| `-static` | 生成 Qt 静态库，而不是 Qt DLL。 |
| `-static-runtime` | Qt 使用静态 MSVC CRT（Release 为 `/MT`）。 |
| `-release` | 只构建 Release，减少时间和输出。 |
| `-nomake examples -nomake tests` | 不编译示例和测试。 |
| `-opengl dynamic` | 保留 Windows 动态 OpenGL 选择能力，不代表 Qt 本身动态链接。 |
| `-no-openssl` | 不引入额外 OpenSSL 构建依赖。 |
| `-submodules ...` | 只构建本项目需要的模块及依赖。 |
| 两个 `-no-feature-quickcontrols2-*` | 避开本次 FluentWinUI3 qrc 长路径编译失败；项目 UI 不使用这些样式。 |

不要复用以前配置过动态 Qt 的构建目录。若需要彻底重做，应使用一个新的
空构建目录；不要在未确认路径的情况下删除现有目录。

### 4.2 编译和安装

```powershell
& $CMake --build $QtBuild --parallel 8
if ($LASTEXITCODE -ne 0) { throw "Qt build failed: $LASTEXITCODE" }

& $CMake --install $QtBuild
if ($LASTEXITCODE -ne 0) { throw "Qt install failed: $LASTEXITCODE" }
```

`--parallel 8` 是并行任务数，可按机器内存和 CPU 调整。内存不足时可改成 4。

另开一个 PowerShell 查看进度，不影响正在编译的窗口：

```powershell
Get-Content "C:\Users\Eonun\Documents\SupersawOmega\Overteon-0.9.20\Build\qt-static-build\.ninja_log" -Tail 15 -Wait
```

按 `Ctrl+C` 只退出日志查看，不会停止另一个窗口中的编译。
`.ninja_log` 同时包含编译、moc、rcc、链接等步骤，因此不能只用 obj 数量推算精确百分比。

### 4.3 检查静态 Qt

```powershell
Test-Path (Join-Path $QtPrefix "bin\qsb.exe")
Test-Path (Join-Path $QtPrefix "lib\Qt6Core.lib")
Test-Path (Join-Path $QtPrefix "lib\Qt6Quick.lib")
Test-Path (Join-Path $QtPrefix "lib\cmake\Qt6\Qt6Config.cmake")
```

四项均应返回 `True`。本次 `config.summary` 中关键结果为：

```text
Building shared libraries ........ no
Mode ............................. release
OpenSSL .......................... no
Qt Quick Controls 2 Styles ....... <none>
```

---

## 5. 第二步：编译 Overteon VST3

### 5.1 配置独立构建目录

继续使用已经初始化 MSVC 环境的 PowerShell：

```powershell
& $CMake `
  -S (Join-Path $Project "Config") `
  -B $Build `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=$QtPrefix `
  "-DOVERTEON_FORMATS=VST3;Standalone"

if ($LASTEXITCODE -ne 0) { throw "Overteon configure failed: $LASTEXITCODE" }
```

注意：

- `-S` 指向 `Config`，不是项目根目录。
- `CMAKE_PREFIX_PATH` 指向刚才安装的 **静态** Qt，不是动态 Qt 的 bin 目录。
- `VST3;Standalone` 在 PowerShell 中整体加引号，避免分号被解释成命令分隔符。
- 不要把动态 Qt 的 `Build/win` 缓存和静态 Qt 的 `Build/win-static` 混用。

### 5.2 构建 VST3

```powershell
& $CMake --build $Build --target Overteon_VST3 --parallel 8
if ($LASTEXITCODE -ne 0) { throw "VST3 build failed: $LASTEXITCODE" }
```

如需用于无宿主检查的 Standalone：

```powershell
& $CMake --build $Build --target Overteon_Standalone --parallel 8
if ($LASTEXITCODE -ne 0) { throw "Standalone build failed: $LASTEXITCODE" }
```

### 5.3 构建系统如何保证静态整合

`Config/CMakeLists.txt` 在静态 Qt 下执行以下关键逻辑：

- 检查 `QT6_IS_SHARED_LIBS_BUILD` 为假；
- 对 `Overteon` 共享代码目标及每个格式目标设置
  `MSVC_RUNTIME_LIBRARY=MultiThreaded...`，与 Qt 的 `/MT` 一致；
- 链接 `Qt6::QWindowsIntegrationPlugin`；
- 链接 `qtquick2plugin`、`qmlplugin`、`modelsplugin`、`workerscriptplugin`；
- 将 `ov_ui.qrc` 和生成的 shader qrc 加入每个格式目标；
- 使用 `Qt6::qsb` 预编译 shader；
- 不进入动态 Qt 专用的 `Overteon_VST3_Bootstrap` 分支。

`Source/ui/qt/OvQmlEditor.cpp` 还使用：

```cpp
Q_IMPORT_QML_PLUGIN(QtQuick2Plugin)
Q_IMPORT_QML_PLUGIN(QtQmlPlugin)
Q_IMPORT_QML_PLUGIN(QtQmlModelsPlugin)
Q_IMPORT_QML_PLUGIN(QtQmlWorkerScriptPlugin)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
```

这些导入语句不可省略。静态 Qt 不会像动态 Qt 一样在运行时从 DLL 自动发现插件。

---

## 6. 导出、保留目录和安装

### 6.1 检查 JUCE 原始产物

```powershell
$Bundle = Join-Path $Build "Overteon_artefacts\Release\VST3\Overteon.vst3"
$Binary = Join-Path $Bundle "Contents\x86_64-win\Overteon.vst3"

Get-Item -LiteralPath $Bundle
Get-Item -LiteralPath $Binary
Get-ChildItem -LiteralPath $Bundle -Recurse -File
```

预期至少存在：

```text
Contents/Resources/moduleinfo.json
Contents/x86_64-win/Overteon.vst3
```

如果原目录中还有 `Contents/presets/Factory` 和 `Contents/presets/User`，
它们属于现有配置体系，应原样保留。升级时先备份整个旧目录，再复制新二进制；
不要删除整个目录后用不完整的包替代，也不要使用会删除目标额外文件的镜像命令。

### 6.2 导出独立二进制

本次成功交付形式是单独一个文件。复制命令为：

```powershell
$Dist = Join-Path $Project "Build\dist-win\VST3"
New-Item -ItemType Directory -Force -Path $Dist | Out-Null
Copy-Item -LiteralPath $Binary -Destination (Join-Path $Dist "Overteon.vst3") -Force

Get-Item -LiteralPath (Join-Path $Dist "Overteon.vst3")
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $Dist "Overteon.vst3")
```

本次已验证文件：

```text
大小：35,494,912 字节
SHA256：DB05C22B25BA30DDB4BDEAD84FBFB9F5606D5600B9CC9249E4792E2D85138280
```

重新编译后大小和哈希可能变化，应以新构建的实际检查值为准。

### 6.3 安装到系统 VST3 目录

标准系统目录：

```text
C:\Program Files\Common Files\VST3\
```

操作前先关闭所有正在扫描或加载插件的宿主，并备份旧文件或旧目录。
`Program Files` 需要管理员权限，所以复制操作应在“以管理员身份运行”的
PowerShell 中完成。

- 当前采用独立二进制时，将新的 `Overteon.vst3` 文件复制到上述目录。
- 当前采用标准目录包时，复制完整的 `Overteon.vst3/` 文件夹，保留其中所有
  `Contents` 层级和已有预设。
- 如果旧形态是文件、新形态是目录，或反过来，不要直接强制覆盖；先备份并明确
  区分旧产物，再安装新产物，避免同名文件和目录冲突。

本次曾将独立文件安装为：

```text
C:\Program Files\Common Files\VST3\Overteon.vst3
```

安装文件与 `Build/dist-win/VST3/Overteon.vst3` 的 SHA256 已核对一致。
旧预设的当时备份位于 `Build/win-static/system-presets-backup`。

---

## 7. 验证是否真正可用

文件存在、扩展名正确或导出符号存在，都不能单独证明插件可用。至少完成以下检查。

### 7.1 检查是否残留额外 DLL

对标准目录包执行：

```powershell
Get-ChildItem -LiteralPath $Bundle -Recurse -File |
  Where-Object { $_.Extension -in ".dll", ".exe" }
```

最终 VST3 包中不应出现 Qt DLL、VC++ 运行库 DLL、`OverteonCore.dll` 或加载壳。
内部唯一的插件可执行模块使用 `.vst3` 扩展名。

### 7.2 检查 PE 依赖

在已初始化 MSVC 环境的 PowerShell 中执行：

```powershell
dumpbin /DEPENDENTS $Binary
```

不应出现：

```text
Qt6*.dll
MSVCP*.dll
VCRUNTIME*.dll
OverteonCore.dll
```

保留 `KERNEL32.dll`、`USER32.dll`、`GDI32.dll`、`OPENGL32.dll` 等 Windows
系统 DLL 是正常的。静态链接不等于连操作系统 API 也不能调用。

### 7.3 在干净 Qt 环境下检查 VST3 工厂

下面直接在 PowerShell 中执行，不创建额外脚本。它调用真实的 `InitDll` 和
`GetPluginFactory`，再读取 `IPluginFactory::countClasses`，不只是 `LoadLibrary`。

```powershell
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class Vst3Native
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr LoadLibraryW(string path);

    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
    public static extern IntPtr GetProcAddress(IntPtr module, string name);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    [return: MarshalAs(UnmanagedType.I1)]
    public delegate bool InitDll();

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    public delegate IntPtr GetPluginFactory();

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    public delegate int CountClasses(IntPtr self);
}
"@

$module = [Vst3Native]::LoadLibraryW($Binary)
if ($module -eq [IntPtr]::Zero) {
    throw [System.ComponentModel.Win32Exception]::new(
        [Runtime.InteropServices.Marshal]::GetLastWin32Error())
}

$initAddress = [Vst3Native]::GetProcAddress($module, "InitDll")
$factoryAddress = [Vst3Native]::GetProcAddress($module, "GetPluginFactory")
if ($initAddress -eq [IntPtr]::Zero -or $factoryAddress -eq [IntPtr]::Zero) {
    throw "VST3 entry points are missing"
}

$init = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer(
    $initAddress, [type][Vst3Native+InitDll])
$getFactory = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer(
    $factoryAddress, [type][Vst3Native+GetPluginFactory])

$initResult = $init.Invoke()
$factory = $getFactory.Invoke()
if ($factory -eq [IntPtr]::Zero) { throw "GetPluginFactory returned null" }

# IPluginFactory: FUnknown 的 3 个方法之后，索引 3 是 getFactoryInfo，
# 索引 4 才是 countClasses。调用错索引会导致测试进程访问冲突。
$vtable = [Runtime.InteropServices.Marshal]::ReadIntPtr($factory)
$countAddress = [Runtime.InteropServices.Marshal]::ReadIntPtr(
    $vtable, 4 * [IntPtr]::Size)
$countClasses = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer(
    $countAddress, [type][Vst3Native+CountClasses])

[pscustomobject]@{
    InitDll        = $initResult
    FactoryClasses = $countClasses.Invoke($factory)
    QtDllCount     = @(Get-Process -Id $PID).Modules |
                     Where-Object { $_.ModuleName -like "Qt6*.dll" } |
                     Measure-Object |
                     Select-Object -ExpandProperty Count
    LoadedFrom     = $Binary
}
```

本次实测结果为：

```text
InitDll        : True
FactoryClasses : 2
QtDllCount     : 0
```

需要严格排除系统 Qt 影响时，可在单独测试 PowerShell 中先清除 Qt 环境变量，
并将该进程的 `PATH` 限制为 `C:\Windows\System32`，再执行测试。此操作只影响
当前测试窗口，不要把它写成插件运行所必需的配置。

### 7.4 界面自检（导出前必做，工厂自检不能代替）

工厂自检只验证 VST3 能加载，**不会实例化 QML 界面**。QML 有错时工厂自检照样通过，但宿主里打开编辑器只会显示一行 "the Qt6/QML UI could not be started"。因此每次导出前必须再跑 Standalone 冒烟测试：

```powershell
$exe = "<PROJECT>\Build\win-static\Overteon_artefacts\Release\Standalone\Overteon.exe"
$p = Start-Process -FilePath $exe -RedirectStandardOutput "$env:TEMP\ov-out.txt" -RedirectStandardError "$env:TEMP\ov-err.txt" -PassThru
Start-Sleep -Seconds 6
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Get-Content "$env:TEMP\ov-err.txt"
```

通过标准：进程存活，且 stderr 中**没有** `[ov] qml error`。任何 QML 错误都会在这里逐条列出文件、行号和原因，必须先修复再导出。若尚未构建 Standalone，先执行 `cmake --build Build\win-static --target Overteon_Standalone --parallel 8`。

注意：QML 的 `font.pixelSize` 只接受整数，写 `9.5` 会报 "Invalid property assignment: int expected" 并导致整个界面无法创建。

### 7.5 最终必须由宿主确认

1. 关闭宿主。
2. 安装产物并确认旧扫描缓存没有指向旧路径。
3. 重新扫描 VST3。
4. 创建插件实例并实际打开编辑器。
5. 检查工厂预设、用户预设、参数调整和音频处理。
6. 关闭编辑器再重新打开，确认没有崩溃。
7. 检查编辑器是否填满宿主客户区，右侧和下方没有异常空白。

工厂检查不能代替第 4 步的 QML 编辑器验证，Standalone 也不能代替 VST3 宿主验证。

---

## 8. 本次 Windows 适配解决的问题

### 8.1 `qsb` 写死 Linux 路径

**现象**：Windows 配置或构建时寻找 `/usr/lib/qt6/bin/qsb`，该文件不存在。

**原因**：shader 编译命令写死了 Linux 安装位置。

**解决**：在 `Config/CMakeLists.txt` 中改用 CMake 导入工具目标：

```cmake
COMMAND Qt6::qsb --glsl ... -o <输出> <输入>
```

这样无论 Qt 安装在哪里，都由 `CMAKE_PREFIX_PATH` 找到同一个静态 Qt 的工具。

### 8.2 Linux GLib shim 被 MSVC 编译

**现象**：MSVC 遇到 GCC `__attribute__`、原子内建等语法时报错。

**原因**：`OvGlibShim.cpp` 只用于解决 Linux 宿主的 glib 符号问题，却被加入 Windows 构建。

**解决**：只在 `UNIX AND NOT APPLE` 下添加该源文件。Windows 不需要也不应编译它。

### 8.3 动态 Qt 目录包含大量 DLL

**现象**：`windeployqt` 能收集运行依赖，但包内出现大量 DLL、QML import 和插件，
扫描和交付都不符合单二进制目标。

**结论**：这是动态 Qt 分发方式的自然结果，不是继续调整宿主扫描路径就能根除的问题。

**解决**：最终改用 Qt 静态库。动态加载壳代码仅保留给动态构建兼容，最终静态 VST3
不链接、不需要 `OverteonCore.dll`。

### 8.4 `/DELAYLOAD` 加了却没有进入最终链接

**现象**：设置 `/DELAYLOAD` 后，`dumpbin /DEPENDENTS` 仍显示 Qt 普通依赖。

**原因**：链接选项加在静态共享代码目标 `Overteon` 上，而最终 DLL 由
`Overteon_VST3` 格式目标链接，选项不会自动传过去。

**排查**：检查实际链接命令，而不是只看 CMake 源码。

**处理**：即使移到格式目标，仍会遇到下面的 `LNK1194`，所以该路线最终没有采用。

### 8.5 `LNK1194`：Qt DLL 不能按此方式延迟加载

**现象**：MSVC 报 `LNK1194`，涉及 `QQuickItem::staticMetaObject` 等导入数据符号。

**原因**：Qt DLL 不只导出函数，还导出数据。MSVC 的 `/DELAYLOAD` 无法处理这种
导入数据符号。

**解决**：停止把 delay-load 当成单文件方案，改为真正静态链接 Qt。

### 8.6 Qt 源码组件安装错误

**现象**：`Src` 中只有 `qtinsighttracker`，找不到 `qtbase`、`qtdeclarative`。

**原因**：安装的是 `extensions.qtinsighttracker...src`，不是 Qt 完整 Sources。

**解决**：在 Qt 安装器中选择 `Qt 6.9.3 -> Sources`，确认四个所需模块目录存在。

### 8.7 `C1083`：FluentWinUI3 自动生成文件路径失败

**现象**：

```text
fatal error C1083: 无法打开编译器生成的文件: “”: Invalid argument
```

失败位置在 FluentWinUI3 的 qrc 资源初始化源文件。

**原因**：Qt 自动生成文件嵌套路径过深，触发 MSVC/Windows 路径长度问题。

**解决**：项目不使用这些 Quick Controls 样式，重新配置时关闭：

```text
-no-feature-quickcontrols2-basic
-no-feature-quickcontrols2-fluentwinui3
```

Qt 自动关闭依赖这些样式的其他样式后，静态 Qt 编译和安装成功。
不要通过删除随机 Qt 源文件来绕过错误。

### 8.8 静态插件没有自动注册

**原因**：动态 Qt 可以从插件 DLL 自动发现平台和 QML 插件；静态 Qt 没有这一步，
而 JUCE 插件又是 MODULE 库，不会得到 Qt 可执行程序的全部自动收尾逻辑。

**解决**：CMake 显式链接所需静态插件，C++ 使用 `Q_IMPORT_QML_PLUGIN` 和
`Q_IMPORT_PLUGIN`。缺少 Windows 平台插件时 QGuiApplication 无法创建窗口；
缺少 QML 插件时会出现模块不可导入或 QML 类型不可用。

### 8.9 CRT 链接方式不一致

**原因**：Qt 使用 `-static-runtime`，Release 对应 `/MT`；Overteon 若仍用 `/MD`，
会引入 VC++ 运行库 DLL，并可能造成不同 CRT 之间对象和内存管理问题。

**解决**：共享代码目标和每个格式目标统一设置：

```cmake
MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
```

### 8.10 Windows 编辑器内容贴左上，右侧和下方空白

**原因有两层**：

1. Windows 的 JUCE 编辑器尺寸使用逻辑像素，原逻辑又乘一次 DPR，导致宿主框架过大。
2. Qt 嵌入窗口没有直接按照宿主原生客户区设置几何，内容只占窗口的一部分。

**解决**：

- `OvQmlEditor::targetSize()`：Windows 下设计尺寸不再重复乘 DPR；Linux 路径保持原样。
- `OvQtComponent.cpp`：用 `GetClientRect` 获取宿主客户区物理像素，再除以 Qt DPR
  转为 Qt 使用的 DIP，并设置 QWindow geometry。
- 没有用拉伸 QML 或写死宿主尺寸来掩盖问题。

用户已在实际宿主中确认本次结果成功。

### 8.11 引入 `windows.h` 后变量 `far` 冲突

**现象**：`OvQtComponent.cpp` 出现 C2513/C2059 等编译错误。

**原因**：`far` 是 Windows 头文件兼容宏，原变量名被宏替换。

**解决**：把该变量重命名为 `bottomRight`，重新构建通过。不是 Qt 或 DSP 问题。

### 8.12 工厂检查进程访问冲突

**现象**：PowerShell 测试进程退出码 `-1073741819`。

**原因**：测试代码把 vtable 索引 3 当成 `countClasses`。索引 3 实际是
`getFactoryInfo`，以错误函数签名调用导致测试进程崩溃，不是插件工厂本身崩溃。

**解决**：`IPluginFactory::countClasses` 使用索引 4；修正后返回 `FactoryClasses = 2`。

### 8.13 系统 VST3 目录“访问被拒绝”

**原因**：`C:\Program Files\Common Files\VST3` 需要管理员权限。

**解决**：关闭宿主，在管理员 PowerShell 中复制；先备份旧文件或目录。
不要把整个插件目录权限改成 Everyone 可写来绕过系统权限。

### 8.14 多套一层 VST3 目录

**现象**：出现 `Overteon.vst3/Overteon.vst3/Contents/...`。

**原因**：复制目录时把“源目录本身”和“源目录内容”混淆，目标中又已有同名目录。

**解决**：先确认 `Get-ChildItem` 的实际层级，再选择复制整个包或只复制内部二进制。
不要盲目递归强制覆盖，也不要用 `/MIR` 处理含用户预设的目录。

---

## 9. 从零复现检查表

按顺序勾选：

- [ ] `Config/CMakeLists.txt`、完整 `Source/`、`Assets/` 和 vendored JUCE 均存在。
- [ ] VS 中包含 C++ 桌面工作负荷、MSVC x64、Windows SDK、CMake。
- [ ] Qt 安装的是 `Qt 6.9.3 -> Sources`，四个所需模块目录存在。
- [ ] PowerShell 已初始化 x64 MSVC，`cl`、`ninja`、`cmake` 可用。
- [ ] Qt 在独立目录中以 `-static -static-runtime -release` 配置。
- [ ] Qt 编译、安装成功，`qsb.exe`、Qt `.lib` 和 `Qt6Config.cmake` 存在。
- [ ] Overteon 使用新的 `Build/win-static` 和 `Build/qt-static` 配置。
- [ ] `Overteon_VST3` Release 构建成功。
- [ ] 标准输出包含 `Contents/x86_64-win/Overteon.vst3` 和 `moduleinfo.json`。
- [ ] 已有 `Contents/presets` 及其 `Factory`/`User` 未被删除或迁移。
- [ ] 交付包没有额外 Qt DLL、核心 DLL、加载壳或 VC++ 运行库 DLL。
- [ ] `dumpbin /DEPENDENTS` 没有 Qt/MSVCP/VCRUNTIME 依赖。
- [ ] 清空 Qt 环境后，`InitDll=True`、工厂可返回、Qt DLL 数量为 0。
- [ ] 在真实 VST3 宿主中重新扫描、创建实例并打开编辑器。
- [ ] 预设、参数、音频和编辑器尺寸均由最终使用环境确认。

---

## 10. 本次修改文件索引

- `Config/CMakeLists.txt`：Windows 工具调用、平台源文件过滤、静态 Qt/CRT/插件链接。
- `Source/ui/qt/OvQmlEditor.cpp`：静态插件导入、Windows DPR 请求尺寸修正。
- `Source/ui/qt/OvQmlEditor.h`：尺寸单位注释修正。
- `Source/ui/qt/OvQtComponent.cpp`：Windows 原生客户区与 Qt DIP 几何修正。
- `Source/src/OvVst3Bootstrap.cpp`：仅动态 Qt 构建使用的旧加载壳，最终静态版不需要。
- `Assets/docs/CHANGELOG_v0.9.20.md`：本次修改与验证结果日志。
- `Assets/docs/BUILD_Windows.md`：本复现文档。

---

## 11. 诊断日志构建（Overteon Diag）

正式版永远不写日志；需要抓宿主里的卡顿时，用同一个源码树再配置一个诊断构建。
开关是构建期的，不是运行期设置：`OVERTEON_DIAG_LOG=ON` 才编译日志代码。

```powershell
& $CMake `
  -S (Join-Path $Project "Config") `
  -B (Join-Path $Project "Build\win-static-diag") `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=$QtPrefix `
  "-DOVERTEON_FORMATS=VST3;Standalone" `
  -DOVERTEON_DIAG_LOG=ON

& $CMake --build (Join-Path $Project "Build\win-static-diag") `
  --target Overteon_VST3 --parallel 8
```

诊断构建使用独立的产品名与插件 ID（`Overteon Diag` / `OvtD`），因此可以和正式版
同时被宿主扫描，不会占用同一个 VST3 类 ID。代价是宿主工程里保存的是另一个插件
实例；只用于测试，不要用它保存正式工程。

导出仍是单文件：

```powershell
$Binary = Join-Path $Project "Build\win-static-diag\Overteon_artefacts\Release\VST3\Overteon Diag.vst3\Contents\x86_64-win\Overteon Diag.vst3"
$Dist   = Join-Path $Project "Build\dist-win\Overteon Diag"
New-Item -ItemType Directory -Force -Path $Dist | Out-Null
Copy-Item -LiteralPath $Binary -Destination (Join-Path $Dist "Overteon Diag.vst3") -Force
```

宿主加载后，日志优先写在 **VST3 同目录**：

```text
Overteon-diag-YYYYMMDD-HHMMSS-mmm.log
```

如果该目录不可写（例如 `Program Files\Common Files\VST3`），回退到：

```text
%APPDATA%\Overteon\logs\Overteon-diag-YYYYMMDD-HHMMSS-mmm.log
```

日志内容：

- 头部：构建时间、版本、插件模块路径、实际日志路径、系统、CPU、宿主名；
- `[audio]`：每 500 ms 一条，块数、平均/最大处理耗时、缓冲区预算、超预算次数；
- `[ui]`：每个 EQ 视图每秒一条，绘制帧率、平均绘制耗时、背景/曲线/光标层重建次数、
  鼠标移动到画面更新的平均延迟；
- `[qt]`：QML/Qt 的警告与错误（含文件与行号）；
- `[hb]`：每 2 秒一条心跳。日志最后一行的时间就是卡死/崩溃前的位置。

日志写入在独立锁内完成；音频线程只写原子变量，不做字符串和文件 IO。
正式构建中 `OVERTEON_DIAG_LOG` 为 OFF，以上代码全部编译为空，不产生任何文件。

# Overteon v0.9.0 — macOS 构建技术文档

本文档面向“把整个工程文件夹拷到 Mac 后，从零生成可用 VST3”的场景。
工程已内置 Apple 构建分支，不需要改代码，也不需要 WebView2 SDK。

- 产品名：**Overteon**
- 厂商：**Eonun**
- 工程版本：**0.9.0**
- 构建目标：**VST3**（同时可产出 AU 与 Standalone）
- 最低系统：**macOS 11.0**
- JUCE：**8.0.13**（工程内 `JUCE/` 已自带）

---

## 1. 拷贝清单

### 必须保留

```text
CMakeLists.txt
src/
  DSP.h
  FxProcessor.h
  FxProcessor.cpp
  FxWebEditor.h
  FxWebEditor.cpp
web_fx/
  index.html
  eonun_logo.png
  edix.otf
JUCE/                  ← 保留后可完全离线构建；缺失时会联网拉取 8.0.13
```

`web_fx/` 的三个文件缺一不可：界面 HTML、About 页 logo、标题字体。
它们会在编译期被 `juce_add_binary_data()` 转成 C++ 字节数组。

### 不需要带到 Mac

```text
build/                 ← Windows 的 CMake 缓存，Mac 不能复用
dist/                  ← Windows 产物
build.zip
juce.zip
webview2/              ← Windows 专用 WebView2 SDK
webview2.nupkg
_backup_v1.9.0/        ← 可选，仅回滚用
```

到 Mac 后新建 `build-mac/`，不要试图在旧的 `build/` 上继续构建。

---

## 2. 环境准备

### 2.1 Xcode 命令行工具

```bash
xcode-select --install
```

如果已安装完整 Xcode，也可以执行：

```bash
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
```

### 2.2 CMake 与 Ninja

```bash
brew install cmake ninja
```

没有 Homebrew 时，安装 CMake 官方 macOS 包即可；Ninja 可省略，改用 Xcode 生成器。

### 2.3 版本检查

```bash
clang --version
cmake --version        # 必须 >= 3.22
ninja --version
xcodebuild -version
```

---

## 3. 构建

### 3.1 最短可用流程

```bash
cd /path/to/SupersawOmega

cmake -S . -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-mac --target HarmonicInjector_VST3 --parallel 8
```

生成：

```text
build-mac/HarmonicInjector_artefacts/Release/VST3/Overteon.vst3
```

### 3.2 关于本地 JUCE

[CMakeLists.txt](CMakeLists.txt) 中已加入 Apple 分支：

```cmake
if(APPLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_JUCE "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
endif()
```

因此只要 `JUCE/` 存在，配置阶段不会访问 GitHub。
若删除 `JUCE/`，CMake 会按 `GIT_TAG 8.0.13` 联网克隆，此时需要 `git` 和网络。

### 3.3 构建全部格式

```bash
cmake --build build-mac --parallel 8
```

会生成 VST3、AU、Standalone 三个目标。

### 3.4 只构建某个目标

```bash
cmake --build build-mac --target HarmonicInjector_VST3 --parallel 8
cmake --build build-mac --target HarmonicInjector_AU --parallel 8
cmake --build build-mac --target HarmonicInjector_Standalone --parallel 8
```

内部 target 名保留历史前缀 `HarmonicInjector`，实际产物名由 `PRODUCT_NAME "Overteon"` 决定。

### 3.5 使用 Xcode 生成器（Ninja 不可用时）

```bash
cmake -S . -B build-mac-xcode -G Xcode
cmake --build build-mac-xcode --config Release --target HarmonicInjector_VST3
```

Xcode 工程的产物同样位于：

```text
build-mac-xcode/HarmonicInjector_artefacts/Release/VST3/Overteon.vst3
```

### 3.6 架构选择

默认由 CMakeLists 设置为 Universal：

```text
arm64;x86_64
```

首次构建或只想快速迭代时，可以只编当前架构：

```bash
cmake -S . -B build-mac -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES=arm64
```

Intel Mac 用：

```bash
-DCMAKE_OSX_ARCHITECTURES=x86_64
```

交付给其他人时建议恢复默认双架构。
另外可覆盖部署目标：

```bash
-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
```

---

## 4. 产物结构

### 4.1 VST3

```text
build-mac/HarmonicInjector_artefacts/Release/VST3/Overteon.vst3/
├── Contents/
│   ├── Info.plist
│   ├── PkgInfo
│   ├── MacOS/
│   │   └── Overteon                    ← 主二进制
│   └── Resources/
│       └── moduleinfo.json             ← Name / Vendor / Version
```

### 4.2 其它格式

```text
build-mac/HarmonicInjector_artefacts/Release/AU/Overteon.component
build-mac/HarmonicInjector_artefacts/Release/Standalone/Overteon.app
```

Standalone 不需要宿主，适合先验证界面与 WebView 是否正常。

---

## 5. 安装

### 5.1 当前用户（推荐，不需要 sudo）

```bash
mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3"

rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/Overteon.vst3"
cp -R build-mac/HarmonicInjector_artefacts/Release/VST3/Overteon.vst3 \
      "$HOME/Library/Audio/Plug-Ins/VST3/"
```

### 5.2 全系统

```bash
sudo mkdir -p /Library/Audio/Plug-Ins/VST3
sudo rm -rf /Library/Audio/Plug-Ins/VST3/Overteon.vst3
sudo cp -R build-mac/HarmonicInjector_artefacts/Release/VST3/Overteon.vst3 \
           /Library/Audio/Plug-Ins/VST3/
```

### 5.3 AU

```bash
mkdir -p "$HOME/Library/Audio/Plug-Ins/Components"
rm -rf "$HOME/Library/Audio/Plug-Ins/Components/Overteon.component"
cp -R build-mac/HarmonicInjector_artefacts/Release/AU/Overteon.component \
      "$HOME/Library/Audio/Plug-Ins/Components/"
```

安装后重启 DAW，或让宿主重新扫描插件目录。

---

## 6. 验证

### 6.1 架构

```bash
lipo -info "$HOME/Library/Audio/Plug-Ins/VST3/Overteon.vst3/Contents/MacOS/Overteon"
```

Universal 构建应输出包含 `x86_64 arm64`。

### 6.2 签名状态

```bash
codesign -dv --verbose=4 "$HOME/Library/Audio/Plug-Ins/VST3/Overteon.vst3"
```

本地构建通常是 ad-hoc 签名（`Signature=adhoc`），Apple Silicon 可正常加载。

### 6.3 元数据

```bash
grep -E '"Name"|"Vendor"|"Version"' \
  "$HOME/Library/Audio/Plug-Ins/VST3/Overteon.vst3/Contents/Resources/moduleinfo.json"
```

应包含：

```text
"Name": "Overteon"
"Vendor": "Eonun"
"Version": "0.9.0"
```

### 6.4 Standalone 冒烟测试

```bash
open build-mac/HarmonicInjector_artefacts/Release/Standalone/Overteon.app
```

界面应显示上下两块连续 EQ、中央综合表与底部八个旋钮。
这是排查 WebView 问题最快的入口。

### 6.5 AU 校验（可选）

```bash
auval -v aufx Ovtn Eonu
```

- 类型：`aufx`（效果器）
- 子类型：`Ovtn`（`PLUGIN_CODE`）
- 厂商：`Eonu`（`PLUGIN_MANUFACTURER_CODE`）

---

## 7. 打包与分发

### 7.1 本机自用

直接把 `.vst3` 文件夹复制到插件目录即可，不需要开发者账号。

### 7.2 打压缩包

macOS bundle 内含符号链接和扩展属性，请用 `ditto`，不要用普通 `zip`：

```bash
ditto -c -k --sequesterRsrc --keepParent \
  build-mac/HarmonicInjector_artefacts/Release/VST3/Overteon.vst3 \
  "Overteon_v0.9.0_macOS_$(date +%Y%m%d_%H%M).zip"
```

### 7.3 分发给他人（签名 + 公证）

需要 `Developer ID Application` 证书。工程已开启 hardened runtime。

```bash
ZIP="Overteon_v0.9.0_macOS_$(date +%Y%m%d_%H%M).zip"

codesign --force --deep --options runtime --timestamp \
  --sign "Developer ID Application: <你的名字> (<TEAMID>)" \
  build-mac/HarmonicInjector_artefacts/Release/VST3/Overteon.vst3

ditto -c -k --sequesterRsrc --keepParent \
  build-mac/HarmonicInjector_artefacts/Release/VST3/Overteon.vst3 "$ZIP"

xcrun notarytool submit "$ZIP" \
  --keychain-profile "notary" --wait
```

说明：

- 公证通常提交 zip；`.vst3` 本身不是 `.app`，不要依赖 `stapler` 直接装订。
- 若要求 Gatekeeper 完全离线通过，建议再用 `productbuild` 做成签名并公证的 `.pkg` 安装包。
- 对方下载后被拦截时，可执行：

```bash
xattr -dr com.apple.quarantine /path/to/Overteon.vst3
```

---

## 8. 运行时文件位置

macOS 下 `File::userApplicationDataDirectory` 对应 `~/Library/Application Support`：

```text
~/Library/Application Support/Eonun/OverteonEditor.settings      ← 窗口位置与尺寸
~/Library/Application Support/Eonun/HarmonicInjector.settings    ← 全局过采样偏好
```

文件名保留历史名称 `HarmonicInjector` 是正常的，不影响产品名。
`.hinj` 用户预设由 `SAVE` / `LOAD` 选择的位置决定，不在这两个文件里。

---

## 9. 可复制全流程

```bash
#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

# 1. 配置
cmake -S . -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release

# 2. 编译 VST3
cmake --build build-mac --target HarmonicInjector_VST3 --parallel 8

# 3. 安装到当前用户目录
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$VST3"
rm -rf "$VST3/Overteon.vst3"
cp -R build-mac/HarmonicInjector_artefacts/Release/VST3/Overteon.vst3 "$VST3/"

# 4. 验证
lipo -info "$VST3/Overteon.vst3/Contents/MacOS/Overteon"
codesign -dv --verbose=4 "$VST3/Overteon.vst3" 2>&1 | head -5
grep -E '"Name"|"Vendor"|"Version"' \
  "$VST3/Overteon.vst3/Contents/Resources/moduleinfo.json"

# 5. 打包（可选）
ditto -c -k --sequesterRsrc --keepParent \
  "$VST3/Overteon.vst3" \
  "Overteon_v0.9.0_macOS_$(date +%Y%m%d_%H%M).zip"

echo "完成：$VST3/Overteon.vst3"
```

---

## 10. 常见问题

### 10.1 CMake 报生成器或缓存错误

同一个构建目录不能混用 Ninja 与 Xcode。删除后重建：

```bash
rm -rf build-mac
```

### 10.2 配置阶段卡在下载 JUCE

确认工程根目录的 `JUCE/CMakeLists.txt` 存在。该文件存在时 Apple 构建不会联网。
若确实缺失，可手动放置 JUCE 8.0.13，或保留网络让 FetchContent 克隆。

### 10.3 DAW 列表里没有 Overteon

1. 确认安装到了 `~/Library/Audio/Plug-Ins/VST3/`，而不是直接放在 `Contents/MacOS`。
2. 重启 DAW 或手动重新扫描。
3. 确认宿主架构与二进制匹配；Universal 包可同时服务 Apple Silicon 与 Intel。
4. 若系统级和用户级同时存在同名插件，先移除旧的那份。

### 10.4 插件能加载但界面空白

1. 先打开 Standalone。Standalone 也空白，说明是 WebView 资源问题。
2. Standalone 正常、DAW 空白，检查宿主是否在沙箱或特殊渲染模式下运行。
3. 用“控制台 / Console.app”过滤 `WebKit` 与 `Overteon`，查看具体报错。
4. 确认构建的是最新版：`web_fx/index.html` 是编译期嵌入的，改完必须重新 build。

### 10.5 改了界面但 DAW 里没变化

重编 VST3，并重新复制整个 bundle：

```bash
cmake --build build-mac --target HarmonicInjector_VST3
rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/Overteon.vst3"
cp -R build-mac/HarmonicInjector_artefacts/Release/VST3/Overteon.vst3 \
      "$HOME/Library/Audio/Plug-Ins/VST3/"
```

仍然不更新时，可清理资源目标后重编：

```bash
rm -rf build-mac/juce_binarydata_FxWebAssets
cmake --build build-mac --target HarmonicInjector_VST3
```

### 10.6 提示无法验证开发者

本地构建通常不会触发；若从网络下载后被隔离：

```bash
xattr -dr com.apple.quarantine /path/to/Overteon.vst3
```

或对 bundle 重新 ad-hoc 签名：

```bash
codesign --force --deep --sign - /path/to/Overteon.vst3
```

### 10.7 构建速度慢

- 默认 Universal 会编译 `arm64` 与 `x86_64` 两份。
- 迭代阶段使用 `-DCMAKE_OSX_ARCHITECTURES=arm64`。
- 首次构建需要编译 JUCE 模块，属于正常现象。

### 10.8 AU 校验失败

先确认：

```bash
defaults read "$HOME/Library/Audio/Plug-Ins/Components/Overteon.component/Contents/Info.plist"
```

重点检查 `CFBundleIdentifier`、版本号与 `AudioComponents` 条目。
AU 与 VST3 共用同一份 DSP，若 VST3 正常而 AU 异常，通常是安装路径或缓存问题。

---

## 11. 参数速查

| 项目 | 值 |
|---|---|
| CMake 最低版本 | 3.22 |
| 默认生成器 | Ninja（文档示例） |
| 构建类型 | Release |
| 默认架构 | `arm64;x86_64` |
| 部署目标 | macOS 11.0 |
| 格式 | VST3 / AU / Standalone |
| VST3 target | `HarmonicInjector_VST3` |
| AU target | `HarmonicInjector_AU` |
| Standalone target | `HarmonicInjector_Standalone` |
| 产物根目录 | `build-mac/HarmonicInjector_artefacts/Release/` |
| 产品名 | Overteon |
| 厂商 | Eonun |
| 版本 | 0.9.0 |

---

## 12. 变更记录

- 2026-11-04：首版。覆盖从拷贝、环境、构建、安装、验证、打包、签名到故障排查的完整 macOS 流程。
- 同日：CMakeLists 增加 Apple 本地 JUCE 优先逻辑，避免 Mac 构建依赖 GitHub 网络。

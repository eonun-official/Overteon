# Cross-compiling the Qt UI for Windows

The Qt front end has no JUCE dependency, so it cross-compiles cleanly.  The
plug-in itself does not: JUCE refuses MinGW outright
(`juce_TargetPlatform.h` has `#error "MinGW is not supported"`), and making it
work would mean patching JUCE's core for `pointer_sized_uint`, missing
`<cstring>` includes and the WebView2 headers.

## What works

```
cargo install --locked xwin
xwin --accept-license splat --output ~/.xwin-cache

# Qt for Windows (MSVC) + a native Qt of the same version for the host tools
aqt install-qt windows desktop 6.9.3 win64_msvc2022_64 -O ~/Qt-msvc \
    --archives qtbase qtdeclarative
aqt install-qt linux desktop 6.9.3 linux_gcc_64 -O ~/Qt-host --archives qtbase
aqt install-qt linux desktop 6.9.3 linux_gcc_64 -O ~/Qt-host --archives icu

# host rcc/moc need Qt-host's own ICU (73)
for t in rcc moc uic; do
  printf '#!/bin/sh\nLD_LIBRARY_PATH=%s/Qt-host/6.9.3/gcc_64/lib exec %s/Qt-host/6.9.3/gcc_64/libexec/%s "$@"\n' \
      "$HOME" "$HOME" "$t" > ~/hostqt-bin/$t
  chmod +x ~/hostqt-bin/$t
done

cmake -S . -B build-win -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-clangcl-xwin.cmake \
      -DOVERTEON_PREVIEW_ONLY=ON -DCMAKE_BUILD_TYPE=Release \
      -DOVERTEON_HOST_RCC=$HOME/hostqt-bin/rcc
ninja -C build-win
```

Produces `build-win/overteon-qml-preview.exe` — a real PE32+ binary linking
the MSVC CRT (through clang-cl + lld-link, not MinGW).

## Running it in wine

```
mkdir -p dist-win && cp build-win/overteon-qml-preview.exe dist-win/
cp ~/Qt-msvc/6.9.3/msvc2022_64/bin/Qt6{C,Gu,Qml,Quick,QmlModels,
    QmlWorkerScript,QmlMeta,Network,OpenGL}.dll dist-win/
WINEPREFIX=~/.wine-onyx DISPLAY=:3 wine dist-win/overteon-qml-preview.exe
```

Verified: the window opens, renders the full UI (design background
#16181d, deck, bars) and is capturable with `import -window`.

## Wine support is automatic now

Two separate wine problems, both handled by the application itself — no
environment variables needed:

1. **Qt 6 picks the D3D11 RHI on Windows, and wine's DXGI cannot create an
   output for the window**:
   ```
   err:dxgi:dxgi_device_init Failed to get output from window ...
   ```
   The result is a uniformly black window.  The app now detects wine
   (`ntdll!wine_get_version`) before constructing QApplication and selects
   `QSG_RHI_BACKEND=opengl`.

2. **Qt Quick's own window does not present.** A plain QWidget repaints
   fine, and the scene graph runs (grabWindow() at different times differs),
   so only the present is broken.  Under wine the app now renders through
   `QQuickWidget`, which goes via `QQuickRenderControl` into an FBO and
   blits through QWidget's paint path — no window present involved.

   (Grabbing a hidden QQuickView into a QWidget does *not* work: a window
   that was never presented has no surface, so grabWindow() returns an empty
   image and the widget stays blank.)

Verified in wine 9.21, `wine explorer /desktop=onyxteon,1100x830 ...` on a
nested display: 40000-50000 px change per frame, full UI, deck border at the
expected scaled row.

Override with `--compat` / `--no-compat`.

## Notes

* `OVERTEON_PREVIEW_ONLY=ON` skips JUCE entirely.
* The xwin splat ships the release CRT only, so the build type must be
  Release (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL` is pinned in the
  toolchain file).
* CMake runs moc/rcc on the build host, so `OVERTEON_HOST_RCC` and the
  `QT_HOST_PATH` in the toolchain must point at a native Qt of the same
  version as the Windows one.

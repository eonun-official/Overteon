# Overteon Debug build

Windows x64 diagnostic build (`OVERTEON_FILE_LOG=ON`, report token compiled in).

## Contents

* `Standalone/Overteon.exe` — standalone application, Qt 6.9.3 runtime included
* `VST3/Overteon.vst3` — VST3 plug-in bundle, runtime included
* `VST/Overteon.dll` — VST2 plug-in, runtime included

## Behaviour

* Every run writes `%USERPROFILE%\Overteon\<date>.<time>.log`.
* When the editor opens, the log is uploaded to
  `github.com/path86/bugreportREP` under `Ot/<version>/`, with home directories
  redacted to `~`. The report token is embedded in these binaries.
* This build is a diagnostic channel, not a stable release.

## Known issues in this build

* Windows was built and smoke-tested on Linux/Wine only: the VST3 loads in a
  JUCE host and the Qt editor initialises (QML scene loads, editor 1040x772).
* Under Wine the Qt 6.9.3 runtime reports a QML binding recursion
  (`OvGpuGlass.qml`, "Maximum call stack size exceeded") and the automatic
  upload countdown stalls behind it. Verify on real Windows before shipping.

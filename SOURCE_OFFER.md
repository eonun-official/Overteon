# Corresponding source for Overteon 0.9.20

Overteon 0.9.20 is distributed as a VST3 plug-in built from this repository.
The complete corresponding source for the project is this repository, at the
commit tagged `v0.9.20` (or the exact commit named in the release notes).

## Project

- Source: https://github.com/eonun-official/Overteon
- Tag: `v0.9.20`
- Notices tag: `v0.9.20-licences` (same code, completed third-party notices)
- Build scripts: `Config/CMakeLists.txt`, `Config/CMakePresets.json`
- Windows build notes: `Assets/docs/BUILD_Windows.md`

## Version scope

`v0.9.20` is the commit the released binary was built from.  `v0.9.20-licences`
is a later, documentation-only tag that adds the completed third-party notices;
the code is unchanged.  Older development tags and snapshots are retained as
history and are superseded by the current `NOTICE` and `LICENSES/`.

## Qt 6.9.3

The Windows binary statically links Qt 6.9.3, built from the official source
with the configuration documented in `Assets/docs/BUILD_Windows.md`.

- Exact source: https://download.qt.io/official_releases/qt/6.9/6.9.3/single/qt-everywhere-src-6.9.3.tar.xz
- Upstream source browser: https://code.qt.io/cgit/qt/
- Licence: GPLv3 open-source option (see `LICENSES/GPL-3.0.txt` and
  `LICENSES/Qt-6.9.3/`)

If the upstream archive is unavailable, the project author will provide the
exact Qt 6.9.3 source used for the build on request, for at least three years
from the release date, through the repository's issue tracker:
https://github.com/eonun-official/Overteon/issues

## JUCE 8.0.13

- Source: https://github.com/juce-framework/JUCE/tree/8.0.13
- Licence: AGPLv3 option (see `LICENSES/JUCE-8-LICENSE.md`)
- JUCE's bundled dependency notices: `LICENSES/JUCE-8.0.13/`

The build fetches the pinned JUCE 8.0.13 source; it is not vendored in this
repository.  A local, untracked checkout may exist at `Source/third_party/JUCE`
and the build uses it when present, but the upstream tag above is the
authoritative corresponding source.  The exact source is available at the
upstream tag above.

## VST3 SDK

The VST3 SDK is bundled with JUCE 8.0.13 and is MIT-licensed; the verbatim
licence is `LICENSES/JUCE-8.0.13/VST3-SDK-LICENSE.txt`.

## Distribution scope

The release artifact is the single-file VST3 plug-in.  No VST2 or LV2 binaries
are shipped.  The VST2 clean-room headers remain in the source tree for build
compatibility; the build uses the project-authored headers at
`Source/third_party/vst2sdk`, and the Xaymar SDK at
`Source/third_party/vst2sdk-xaymar` is a BSD-3-Clause reference/alternative
(`LICENSES/VST2-SDK-BSD-3-Clause.txt`).  They are not part of any distributed
binary.  `Build/dist-win` is a development runtime, not a release artifact.

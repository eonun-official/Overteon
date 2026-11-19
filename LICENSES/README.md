# Licence texts

The licence texts are verbatim copies of the upstream licences, except
`EDIX.txt` (an authored attribution note), `Qt-6.9.3/README.md` (an authored
index) and this README.
`Qt-6.9.3/BSD-3-Clause.txt` is the upstream Qt template text; the copyright
holder for each Qt component is named in that component's own notice.
Nothing in this directory adds to, removes from, or replaces those licences.

| path | component | applies to |
|---|---|---|
| `AGPL-3.0.txt` | Overteon | the whole project (also installed as the root `LICENSE`) |
| `JUCE-8-LICENSE.md` | JUCE 8.0.13 | pinned upstream JUCE, the compiled plug-in and standalone |
| `JUCE-8.0.13/` | JUCE bundled dependencies | FLAC, Ogg Vorbis, libjpeg, CHOC/QuickJS, VST3 SDK, pnglib, zlib, HarfBuzz, SheenBidi, LV2, ASIO, AAX, AudioUnitSDK, Oboe, Box2D, pslextensions and related notices |
| `JUCE-8.0.13/LV2-Support-Libs-ISC.txt` | lilv, serd, sord, sratom | JUCE headless LV2 support libraries (source-only; LV2 builds) |
| `jucexqt-BSD-3-Clause.txt` | jucexqt | `Source/ui/qt/OvQtComponent.*` |
| `GPL-3.0.txt` | Qt 6.9.3 | open-source Qt option used for this distribution |
| `LGPL-3.0.txt` | Qt 6.9.3 | the other open-source Qt option; supplied because Qt's licence terms reference it |
| `Qt-6.9.3/` | Qt 6.9.3 bundled third-party code | verbatim notices for FreeType, HarfBuzz, libjpeg-turbo, libpng, PCRE2, zlib, glslang, SPIRV-Cross, double-conversion, md4c, tinycbor and the other Qt 3rdparty components; includes `Qt-GPL-exception-1.0.txt` |
| `VST2-SDK-BSD-3-Clause.txt` | Xaymar clean-room VST2 SDK | reference/alternative headers; the build uses the project-authored `Source/third_party/vst2sdk` headers; no VST2 binary is distributed |
| `OFL-1.1-Inter.txt` | Inter | `Source/ui/qt/res/Inter-*.ttf` |
| `EDIX.txt` | EDIX font | UI display face; author's published terms and attribution |

## How the combination works

- **JUCE 8.0.13** is offered under AGPLv3 or a commercial JUCE licence.  This
  project takes the AGPLv3 option, so the project as a whole is AGPL-3.0.
- **Qt 6.9.3** is used under its GPLv3 open-source option.  The released
  single-file VST3 statically links Qt; `Build/dist-win` is a development
  runtime with shared Qt DLLs.  AGPLv3 section 13 explicitly permits combining
  an AGPLv3 work with GPLv3 works, so the combined work is conveyed
  consistently.
- **jucexqt** is used under BSD-3-Clause; the notice is
  `jucexqt-BSD-3-Clause.txt`.
- Because the complete corresponding source — code, CMake build scripts and the
  exact Qt/JUCE versions — is published in this repository or pinned to the
  exact upstream tag, and tagged per release, the source and relinking
  obligations that come with static linking are satisfied without shipping
  separate object files.  See `SOURCE_OFFER.md` for the exact upstream source
  locations and the written source offer.
- Qt runtime files under `Build/dist-win/` belong to the development runtime,
  not a release artifact, and are covered by the Qt licences above.
- Distribution scope: the release artifact is the single-file **VST3**.  No
  VST2 or LV2 binaries are shipped.  The VST2 clean-room headers remain in the
  source tree for build compatibility; the build uses the project-authored
  headers at `Source/third_party/vst2sdk`, and the Xaymar SDK is a
  BSD-3-Clause reference/alternative.

The full third-party declaration, including bundled JUCE and Qt dependencies,
is in the root `NOTICE`.

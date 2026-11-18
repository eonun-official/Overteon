# Licence texts

Every licence file here is a verbatim copy of an upstream licence, except
`EDIX.txt`, which records the font author's published terms and attribution.
Nothing in this directory adds to, removes from, or replaces those licences.

| path | component | applies to |
|---|---|---|
| `AGPL-3.0.txt` | Overteon | the whole project (also installed as the root `LICENSE`) |
| `JUCE-8-LICENSE.md` | JUCE 8.0.13 | `Source/third_party/JUCE`, the compiled plug-in and standalone |
| `JUCE-8.0.13/` | JUCE bundled dependencies | FLAC, Ogg Vorbis, libjpeg, CHOC/QuickJS, VST3 SDK, pnglib, zlib, HarfBuzz, SheenBidi, LV2, ASIO/AAX/AU SDK notices |
| `GPL-3.0.txt` | Qt 6.9.3 | open-source Qt option used for this distribution |
| `LGPL-3.0.txt` | Qt 6.9.3 | the other open-source Qt option; supplied because Qt's licence terms reference it |
| `Qt-6.9.3/` | Qt 6.9.3 bundled third-party code | verbatim notices for FreeType, HarfBuzz, libjpeg-turbo, libpng, PCRE2, zlib, glslang, SPIRV-Cross, double-conversion, md4c, tinycbor and the other Qt 3rdparty components; includes `Qt-GPL-exception-1.0.txt` |
| `VST2-SDK-BSD-3-Clause.txt` | Xaymar clean-room VST2 SDK | source tree only; no VST2 binary is distributed |
| `OFL-1.1-Inter.txt` | Inter | `Source/ui/qt/res/Inter-*.ttf` |
| `EDIX.txt` | EDIX font | UI display face; author's published terms and attribution |

## How the combination works

- **JUCE 8.0.13** is offered under AGPLv3 or a commercial JUCE licence.  This
  project takes the AGPLv3 option, so the project as a whole is AGPL-3.0.
- **Qt 6.9.3** is used under its GPLv3 open-source option, and is statically
  linked into the binaries.  AGPLv3 section 13 explicitly permits combining an
  AGPLv3 work with GPLv3 works, so the combined work is conveyed consistently.
- Because the complete corresponding source — code, CMake build scripts and the
  exact Qt/JUCE versions — is published in this repository and tagged per
  release, the source and relinking obligations that come with static linking
  are satisfied without shipping separate object files.  See `SOURCE_OFFER.md`
  for the exact upstream source locations and the written source offer.
- Qt runtime files under `Build/dist-win/` are part of the distribution and are
  covered by the Qt licences above.
- Distribution scope: **VST3 only**.  No VST2 or LV2 binaries are shipped.
  The VST2 clean-room headers remain in the source tree for build
  compatibility and are covered by `VST2-SDK-BSD-3-Clause.txt`.

The full third-party declaration, including bundled JUCE and Qt dependencies,
is in the root `NOTICE`.

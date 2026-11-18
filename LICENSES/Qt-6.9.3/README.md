# Qt 6.9.3 — bundled third-party licence notices

The released single-file VST3 statically links Qt 6.9.3 (Core, Gui, Qml,
Quick, OpenGL and Network, plus the QML modules used by the editor).  The
development preview/build tools also use Widgets, QuickWidgets and
ShaderTools.  Qt's own licence is GPLv3 (the open-source option); the project
as a whole is AGPL-3.0, which AGPLv3 section 13 permits to be combined with
GPLv3 works.

`Build/dist-win` is a development runtime with shared Qt DLLs and additional
Qt modules/plugins; it is not a release artifact, but the same Qt licences
apply to it.

Qt itself bundles third-party code.  Because the released VST3 statically links
Qt, that code is part of the shipped binary and its notices must travel with
the binary.
The verbatim licence files are in `3rdparty/`, grouped by Qt module and
component.  The directories were copied from the Qt 6.9.3 source tree used for
the build (`qtbase`, `qtdeclarative`, `qtshadertools`, `qtsvg`).

The components that are built as separate static libraries in this
configuration are:

| component | Qt library | typical licence |
|---|---|---|
| FreeType | `Qt6BundledFreetype` | FreeType Project Licence / GPLv2 |
| HarfBuzz | `Qt6BundledHarfbuzz` | Old MIT |
| libjpeg-turbo | `Qt6BundledLibjpeg` (and 12/16-bit variants) | BSD-3-Clause + IJG |
| libpng | `Qt6BundledLibpng` | libpng licence |
| PCRE2 | `Qt6BundledPcre2` | BSD-3-Clause |
| zlib | `Qt6BundledZLIB` | zlib licence |
| glslang | `Qt6BundledGlslang_*` | BSD-3-Clause / Khronos |
| SPIRV-Cross | `Qt6BundledSpirv_Cross` | Apache-2.0 / Khronos |

Other third-party code compiled into Qt Core/Gui/Qml/Quick includes
double-conversion, md4c, tinycbor, blake2, siphash, SHA/MD implementations,
emoji-segmenter, pixman, libpsl, D3D12MemoryAllocator,
VulkanMemoryAllocator and platform-specific components present in the Qt
source.  Where a component ships a licence file, it is under `3rdparty/`;
public-domain and generated components may not have one.  Inclusion of a
platform-specific component does not mean it is linked on Windows.

`Qt-GPL-exception-1.0.txt` is the exception text shipped with the Qt source's
GPLv3 option.  `BSD-3-Clause.txt` is the upstream Qt template text; the
copyright holder for each Qt component is named in that component's own
notice.

This directory is a notice archive, not a replacement for the upstream
licences.  The authoritative texts are the files themselves and the Qt 6.9.3
source distribution.

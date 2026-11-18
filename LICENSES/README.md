# Licence texts

Every file here is a verbatim copy of an upstream licence.  Nothing in this
directory adds to, removes from, or replaces those licences.

| file | component | applies to |
|---|---|---|
| `AGPL-3.0.txt` | Overteon | the whole project (also installed as the root `LICENSE`) |
| `JUCE-8-LICENSE.md` | JUCE 8.0.13 | `Source/third_party/JUCE`, the compiled plug-in and standalone |
| `GPL-3.0.txt` | Qt 6.9.3 | open-source Qt option used for this distribution |
| `LGPL-3.0.txt` | Qt 6.9.3 | the other open-source Qt option; supplied because Qt's licence terms reference it |
| `OFL-1.1-Inter.txt` | Inter | `Source/ui/qt/res/Inter-*.ttf` |

## How the combination works

- **JUCE 8.0.13** is offered under AGPLv3 or a commercial JUCE licence.  This
  project takes the AGPLv3 option, so the project as a whole is AGPL-3.0.
- **Qt 6.9.3** is used under its GPLv3 open-source option, and is statically
  linked into the binaries.  AGPLv3 section 13 explicitly permits combining an
  AGPLv3 work with GPLv3 works, so the combined work is conveyed consistently.
- Because the complete corresponding source — code, CMake build scripts and the
  exact Qt/JUCE versions — is published in this repository and tagged per
  release, the source and relinking obligations that come with static linking
  are satisfied without shipping separate object files.
- Qt runtime files under `Build/dist-win/` are part of the distribution and are
  covered by the Qt licences above.

The full third-party declaration, including bundled JUCE dependencies, is in
the root `NOTICE`.

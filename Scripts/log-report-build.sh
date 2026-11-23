#!/usr/bin/env bash
# Build the Qt plug-in with the runtime log and the automation bug-report token
# compiled in.
#
#   OVERTEON_REPORT_TOKEN=github_pat_xxx Scripts/log-report-build.sh [build-dir]
#
# The token reaches CMake through the environment, never through the command
# line, and CMake writes it to a generated header in the build tree -- so it
# does not appear in compile_commands.json, build.ninja or a CI log.  It is
# still readable inside the produced binaries: that is the point of an
# automation-report build.  Use a fine-grained token limited to
# "Issues: write" on eonun-official/Overteon only, rotate it if an artifact
# escapes, and do not publish a token build to an untrusted channel.
#
# The token can be overridden at run time with OVERTEON_GITHUB_TOKEN, which
# wins over the compiled-in one (see Source/ui/qt/OvIssueReport.cpp).
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-"$root/Build/log-report"}

if [[ -z "${OVERTEON_REPORT_TOKEN:-}" ]]; then
    echo "OVERTEON_REPORT_TOKEN is not set; refusing to build without it." >&2
    echo "usage: OVERTEON_REPORT_TOKEN=... $0 [build-dir]" >&2
    exit 2
fi

slang=${OVERTEON_SLANGC:-"$root/Build/tools/slang/bin/slangc"}
if [[ ! -x "$slang" ]]; then
    echo "slangc not found at $slang; set OVERTEON_SLANGC" >&2
    exit 2
fi

cmake -S "$root" -B "$build" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DOVERTEON_FILE_LOG=ON \
      -DOVERTEON_SLANGC="$slang"

cmake --build "$build" --parallel --target standalone lv2 vst3

cat <<EOF

Built with the automation-report token embedded in:
  $build/products

The token is extractable from those binaries.  Rotate it if the build is
published anywhere you do not control.  A run-time override is:
  OVERTEON_GITHUB_TOKEN=github_pat_yyy
EOF

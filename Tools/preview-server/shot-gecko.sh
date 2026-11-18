#!/bin/sh
# ===========================================================================
#  Render the editor UI with Gecko (Firefox) and save a screenshot.
#
#  Starts the vibe.d preview server if it is not already listening on :8888,
#  then captures http://127.0.0.1:8888 with Firefox in headless mode.
#
#    ./shot-gecko.sh [out.png] [WxH] [url]
#
#  Defaults: gecko-1040x772.png, 1040x772, http://127.0.0.1:8888/
# ===========================================================================
set -e

OUT=$(realpath -m "${1:-gecko-1040x772.png}")
SIZE=${2:-1040x772}
URL=${3:-http://127.0.0.1:8888/current}

HERE=$(cd "$(dirname "$0")" && pwd)

# ---- 1. server -------------------------------------------------------------
if curl -sf -m 2 http://127.0.0.1:8888/healthz > /dev/null 2>&1; then
    echo "using the preview server already running on :8888"
else
    echo "starting the preview server on :8888"
    ( cd "$HERE" && setsid ./overteon-preview-server > /tmp/overteon-preview.log 2>&1 & )
    for _ in $(seq 1 40); do
        curl -sf -m 1 http://127.0.0.1:8888/healthz > /dev/null 2>&1 && break
        sleep 0.25
    done
    curl -sf -m 2 http://127.0.0.1:8888/healthz > /dev/null || {
        echo "server did not come up; see /tmp/overteon-preview.log" >&2
        exit 1
    }
fi

# ---- 2. render with Gecko --------------------------------------------------
# a throw-away profile lets Firefox start headless while another instance runs
PROFILE=$(mktemp -d)
trap 'rm -rf "$PROFILE"' EXIT

W=${SIZE%x*}
H=${SIZE#*x}

MOZ_HEADLESS=1 firefox \
    --headless --no-remote --profile "$PROFILE" \
    --window-size="$W,$H" \
    --screenshot "$OUT" \
    "$URL" 2>&1 | grep -v "You are running in headless mode" || true

if [ -s "$OUT" ]; then
    echo "wrote $OUT"
else
    echo "screenshot failed" >&2
    exit 1
fi

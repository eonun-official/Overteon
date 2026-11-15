#!/bin/sh
# ===========================================================================
#  demo.sh — start/stop the demo processes by PID.
#
#  Never uses pkill/pgrep-style pattern killing: every process is recorded in
#  its own pidfile under $RUN_DIR and stopped with `kill -- -PGID`, so only the
#  process group we started is affected (a stray `pkill jalv` would otherwise
#  take down other hosts the user is running).
#
#    ./demo.sh jalv start|stop|status [lv2-path]
#    ./demo.sh preview start|stop|status
#    ./demo.sh jack start|stop|status
#    ./demo.sh wine start|stop|status [extra args]
#    ./demo.sh all start|stop|status
#
#  RUN_DIR defaults to /tmp/overteon-demo
# ===========================================================================
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
RUN_DIR=${RUN_DIR:-/tmp/overteon-demo}
mkdir -p "$RUN_DIR"

LV2_DEFAULT="$ROOT/Build/host/Overteon_artefacts/Release/LV2"
URI_Overteon="https://eonun.com/plugins/Overteon"

# ---- helpers --------------------------------------------------------------
pid_of()      { [ -f "$RUN_DIR/$1.pid" ] && cat "$RUN_DIR/$1.pid" || echo ""; }
is_running()  { p=$(pid_of "$1"); [ -n "$p" ] && kill -0 "$p" 2>/dev/null; }

# Start a command detached, in its own session, recording its PID.
# $1 = name, rest = command
start_bg() {
    name=$1; shift
    if is_running "$name"; then
        echo "$name already running (pid $(pid_of "$name"))"
        return 0
    fi
    setsid "$@" >> "$RUN_DIR/$name.log" 2>&1 < /dev/null &
    echo $! > "$RUN_DIR/$name.pid"
    sleep 0.3
    if is_running "$name"; then
        echo "$name started (pid $(pid_of "$name")), log $RUN_DIR/$name.log"
    else
        echo "$name failed to start, see $RUN_DIR/$name.log" >&2
        rm -f "$RUN_DIR/$name.pid"
        return 1
    fi
}

# Stop by PID: the negative PID targets the process group created by setsid,
# so children (jalv's helper threads, X clients) go with it.
stop_name() {
    name=$1
    p=$(pid_of "$name")
    if [ -z "$p" ]; then
        echo "$name not running (no pidfile)"
        return 0
    fi
    if kill -0 "$p" 2>/dev/null; then
        kill -TERM -- "-$p" 2>/dev/null || kill -TERM "$p" 2>/dev/null
        for _ in $(seq 1 40); do
            kill -0 "$p" 2>/dev/null || break
            sleep 0.1
        done
        if kill -0 "$p" 2>/dev/null; then
            echo "$name ignored SIGTERM, sending SIGKILL" >&2
            kill -KILL -- "-$p" 2>/dev/null || kill -KILL "$p" 2>/dev/null
        fi
    fi
    rm -f "$RUN_DIR/$name.pid"
    echo "$name stopped (was pid $p)"
}

status_name() {
    name=$1
    if is_running "$name"; then
        echo "$name: running (pid $(pid_of "$name"))"
    else
        echo "$name: stopped"
    fi
}

# ---- the three demos ------------------------------------------------------
do_jalv() {
    lv2=${2:-$LV2_DEFAULT}
    case "$1" in
        start)
            start_bg jalv env \
                DISPLAY=:3 \
                JACK_DEFAULT_SERVER=ovtest \
                LV2_PATH="$lv2:$HOME/.lv2:/usr/lib/lv2" \
                jalv.gtk3 -s "$URI_Overteon" ;;
        stop)   stop_name jalv ;;
        status) status_name jalv ;;
    esac
}

do_preview() {
    case "$1" in
        start)  start_bg preview env DISPLAY=:3 "$ROOT/Build/host/overteon-qml-preview" --size 1040x772 ;;
        stop)   stop_name preview ;;
        status) status_name preview ;;
    esac
}

do_jack() {
    case "$1" in
        start)  start_bg jack jackd -n ovtest -d dummy -r 48000 -p 1024 ;;
        stop)   stop_name jack ;;
        status) status_name jack ;;
    esac
}

# the Windows build under wine: one process group, recorded by PID
do_wine() {
    EXE="$ROOT/Build/dist-win/overteon-qml-preview.exe"
    case "$1" in
        start)
            [ -f "$EXE" ] || { echo "no $EXE - run Scripts/deploy-win.sh" >&2; return 1; }
            start_bg wine env \
                WINEPREFIX="$HOME/.wine-onyx" \
                DISPLAY="${WINE_DISPLAY:-:3}" \
                WINEDEBUG=-all \
                ${WINE_FPS:+OVERTEON_FPS=1} \
                wine "$EXE" --size "${WINE_SIZE:-1040x772}" "${@:2}" ;;
        stop)   stop_name wine ;;
        status) status_name wine ;;
    esac
}

do_all() {
    for n in jack preview jalv; do
        case "$1" in
            start)  $0 "$n" start ;;
            stop)   $0 "$n" stop ;;
            status) $0 "$n" status ;;
        esac
    done
}

case "${1:-}" in
    wine)    do_wine "${2:-status}" "${@:3}" ;;
    jalv)    do_jalv "${2:-status}" "${3:-}" ;;
    preview) do_preview "${2:-status}" ;;
    jack)    do_jack "${2:-status}" ;;
    all)     do_all "${2:-status}" ;;
    *)
        echo "usage: $0 {jalv|preview|jack|all} {start|stop|status}" >&2
        exit 2 ;;
esac

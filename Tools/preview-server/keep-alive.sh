#!/bin/sh
# ===========================================================================
#  Keeps the Overteon preview server alive on 0.0.0.0:8888.
#
#  Runs the server in a loop: if it exits for any reason (crash, port taken,
#  manual kill) it is restarted after a second.  Started detached, so it
#  survives the shell that launched it.
#
#    ./keep-alive.sh start     start the keeper (idempotent)
#    ./keep-alive.sh stop      stop the keeper and the server
#    ./keep-alive.sh status    is it up, and what does it serve
#
#  Log: /tmp/overteon-preview.log
# ===========================================================================
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SERVER="$HERE/overteon-preview-server"
PIDFILE=/tmp/overteon-preview-keeper.pid
LOG=/tmp/overteon-preview.log

start() {
    if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
        echo "keeper already running (pid $(cat "$PIDFILE"))"
        return 0
    fi
    setsid nohup "$HERE/keep-alive.sh" loop >> "$LOG" 2>&1 < /dev/null &
    echo $! > "$PIDFILE"
    sleep 1
    echo "keeper started (pid $(cat "$PIDFILE")), log $LOG"
}

stop() {
    # the keeper runs its server in the same process group (setsid), so killing
    # the group takes both down; no pattern matching involved.
    if [ -f "$PIDFILE" ]; then
        kp=$(cat "$PIDFILE")
        kill -TERM -- "-$kp" 2>/dev/null || kill -TERM "$kp" 2>/dev/null || true
        for _ in $(seq 1 30); do
            kill -0 "$kp" 2>/dev/null || break
            sleep 0.1
        done
        kill -KILL -- "-$kp" 2>/dev/null || kill -KILL "$kp" 2>/dev/null || true
        rm -f "$PIDFILE"
    fi
    # a server started outside the keeper (older build) is stopped via its own
    # pidfile if it has one
    if [ -f /tmp/overteon-preview-server.pid ]; then
        sp=$(cat /tmp/overteon-preview-server.pid)
        kill -TERM "$sp" 2>/dev/null || true
        rm -f /tmp/overteon-preview-server.pid
    fi
    echo "stopped"
}

status() {
    if curl -sf -m 2 http://127.0.0.1:8888/healthz > /dev/null 2>&1; then
        echo "up: $(curl -s -m 2 http://127.0.0.1:8888/healthz)"
        echo "lan: http://$(ip -4 route get 1.1.1.1 2>/dev/null |
                          awk '{for(i=1;i<=NF;i++) if($i=="src") print $(i+1); exit}'):8888/"
    else
        echo "down"
        return 1
    fi
}

loop() {
    while :; do
        # `|| true`: a crash must restart, not terminate the keeper
        "$SERVER" >> "$LOG" 2>&1 &
        echo $! > /tmp/overteon-preview-server.pid
        wait $! || true
        echo "[keep-alive] server exited, restarting in 1s" >> "$LOG"
        sleep 1
    done
}

case "${1:-status}" in
    start)  start  ;;
    stop)   stop   ;;
    status) status ;;
    loop)   loop   ;;
    *) echo "usage: $0 {start|stop|status}" >&2; exit 2 ;;
esac

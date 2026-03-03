#!/bin/sh
### BEGIN INIT INFO
# Provides:          roomwizard-app
# Required-Start:    $local_fs $remote_fs $all
# Required-Stop:     $local_fs $remote_fs
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: RoomWizard App Launcher
# Description:       Starts the configured application from /opt/roomwizard/default-app
### END INIT INFO

# Generic init script for RoomWizard.  Reads /opt/roomwizard/default-app to
# determine which executable to launch (e.g. /opt/games/game_selector,
# /opt/vnc_client/vnc_client).
#
# Set the default app with:
#   echo /opt/games/game_selector > /opt/roomwizard/default-app
#
# The system /usr/sbin/watchdog daemon (started by /etc/init.d/watchdog
# via /etc/default/watchdog) feeds /dev/watchdog.  No separate feeder needed.

PATH=/sbin:/usr/sbin:/bin:/usr/bin
DESC="RoomWizard App Launcher"
NAME=roomwizard-app
CONFIG=/opt/roomwizard/default-app
DISABLE_SCRIPT=/opt/roomwizard/disable-steelcase.sh
PIDFILE=/var/run/roomwizard-app.pid
RESPAWN_SCRIPT=/opt/roomwizard/respawn.sh

[ -f /etc/init.d/functions ] && . /etc/init.d/functions

read_config() {
    if [ -f "$CONFIG" ]; then
        APP_EXEC=$(head -1 "$CONFIG" | tr -d '[:space:]')
    else
        APP_EXEC=""
    fi
}

do_start() {
    echo "Starting $DESC..."

    # Disable Steelcase bloatware (idempotent — safe on every boot)
    if [ -x "$DISABLE_SCRIPT" ]; then
        "$DISABLE_SCRIPT"
    else
        echo "WARNING: $DISABLE_SCRIPT not found, skipping cleanup"
        # Minimal safety net: at least create the watchdog bypass
        touch /var/watchdog_test
    fi

    # Read configured app
    read_config

    if [ -z "$APP_EXEC" ]; then
        echo "WARNING: No default app configured"
        echo "  Set one with: echo /path/to/app > $CONFIG"
        return 0
    fi

    if [ ! -x "$APP_EXEC" ]; then
        echo "ERROR: Configured app not found or not executable: $APP_EXEC"
        return 1
    fi

    # Write respawn wrapper (re-reads config on each restart cycle, so
    # changing /opt/roomwizard/default-app takes effect after the current
    # app exits)
    cat > "$RESPAWN_SCRIPT" << 'RESPAWN_EOF'
#!/bin/sh
CONFIG=/opt/roomwizard/default-app
LOGDIR=/var/log/roomwizard
LOGFILE=$LOGDIR/respawn.log
CHILD_PID=

# Ensure log directory exists
mkdir -p "$LOGDIR"

# Rotate log if over 256 KB
rotate_log() {
    if [ -f "$LOGFILE" ]; then
        size=$(wc -c < "$LOGFILE" 2>/dev/null || echo 0)
        if [ "$size" -gt 262144 ]; then
            mv -f "$LOGFILE" "$LOGFILE.1"
        fi
    fi
}

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') [respawn] $*" >> "$LOGFILE"
}

cleanup() {
    log "Received TERM/INT signal, stopping child PID=$CHILD_PID"
    if [ -n "$CHILD_PID" ]; then
        kill "$CHILD_PID" 2>/dev/null
        # Give the child up to 5 s to exit, then force-kill
        for i in 1 2 3 4 5; do
            kill -0 "$CHILD_PID" 2>/dev/null || break
            sleep 1
        done
        kill -9 "$CHILD_PID" 2>/dev/null
    fi
    log "Respawn wrapper exiting"
    exit 0
}
trap cleanup TERM INT

log "=== Respawn wrapper started (pid $$) ==="

while true; do
    rotate_log
    APP=$(head -1 "$CONFIG" 2>/dev/null | tr -d '[:space:]')
    if [ -x "$APP" ]; then
        log "Starting $APP"
        "$APP" >> "$LOGDIR/app_stdout.log" 2>&1 &
        CHILD_PID=$!
        log "$APP running as PID $CHILD_PID"
        # Robust wait: keep waiting until the child truly exits.
        # BusyBox ash 'wait' may return prematurely on some signals;
        # the kill -0 guard prevents respawning while the app is alive.
        while kill -0 "$CHILD_PID" 2>/dev/null; do
            wait "$CHILD_PID" 2>/dev/null
        done
        wait "$CHILD_PID" 2>/dev/null
        EXIT_CODE=$?
        log "$APP (PID $CHILD_PID) exited (code $EXIT_CODE), restarting in 2s..."
        # Do NOT clear CHILD_PID until after sleep so cleanup() can
        # still kill the child if TERM arrives during the delay.
        sleep 2
        CHILD_PID=
    else
        log "No valid app configured ($APP), retrying in 10s..."
        sleep 10
    fi
done
RESPAWN_EOF
    chmod +x "$RESPAWN_SCRIPT"

    echo "  Launching: $APP_EXEC (with respawn)"
    start-stop-daemon --start --background --make-pidfile \
        --pidfile "$PIDFILE" --exec "$RESPAWN_SCRIPT" || {
        # Fallback if start-stop-daemon fails
        "$RESPAWN_SCRIPT" &
        echo $! > "$PIDFILE"
    }
    echo "$DESC started ($APP_EXEC)"
    return 0
}

do_stop() {
    echo "Stopping $DESC..."
    if [ -f "$PIDFILE" ]; then
        PID=$(cat "$PIDFILE")
        # Send TERM to respawn wrapper (its trap forwards to child app)
        start-stop-daemon --stop --pidfile "$PIDFILE" --retry 5 2>/dev/null || \
            kill "$PID" 2>/dev/null || true
        rm -f "$PIDFILE"
    fi

    # Kill ALL respawn.sh instances (catches orphans from previous restarts)
    if pidof -x respawn.sh >/dev/null 2>&1; then
        echo "  Killing remaining respawn.sh processes..."
        killall respawn.sh 2>/dev/null || true
        sleep 1
        killall -9 respawn.sh 2>/dev/null || true
    fi
    rm -f "$RESPAWN_SCRIPT"

    # Safety net: kill any orphaned child app that survived the wrapper.
    # Read the configured binary name and killall by basename.
    read_config
    if [ -n "$APP_EXEC" ]; then
        APP_BASE=$(basename "$APP_EXEC")
        if pidof "$APP_BASE" >/dev/null 2>&1; then
            echo "  Killing orphaned $APP_BASE..."
            killall "$APP_BASE" 2>/dev/null || true
            sleep 1
            killall -9 "$APP_BASE" 2>/dev/null || true
        fi
    fi

    echo "$DESC stopped"
    return 0
}

do_status() {
    read_config
    echo "Configured app: ${APP_EXEC:-"(none)"}"
    if [ -f "$PIDFILE" ]; then
        PID=$(cat "$PIDFILE")
        if kill -0 "$PID" 2>/dev/null; then
            echo "Status: running (PID $PID)"
        else
            echo "Status: not running (stale PID)"
        fi
    else
        echo "Status: not running"
    fi
}

case "$1" in
    start)                do_start ;;
    stop)                 do_stop ;;
    restart|force-reload) do_stop; sleep 2; do_start ;;
    status)               do_status ;;
    *)  echo "Usage: $0 {start|stop|restart|status}"; exit 1 ;;
esac

exit 0

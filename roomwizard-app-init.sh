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

    # Neither "no app configured" nor "app not executable" is fatal any more:
    # the wrapper counts the failures and falls back to the launcher, which is
    # the only on-device recovery path.  Refusing to start here is what used to
    # leave a bad default-app on a black screen with no way back.
    if [ -z "$APP_EXEC" ]; then
        echo "WARNING: No default app configured"
        echo "  Set one with: echo /path/to/app > $CONFIG"
        echo "  Starting the respawn wrapper anyway — it will fall back to the launcher"
    elif [ ! -x "$APP_EXEC" ]; then
        echo "WARNING: Configured app not found or not executable: $APP_EXEC"
        echo "  Starting the respawn wrapper anyway — it will fall back to the launcher"
    fi

    # Never launch a second wrapper.  Checked BEFORE the heredoc below, because
    # truncating and rewriting a script file that a running /bin/sh still has
    # open can make that sh misparse the remainder of the file.
    if pidof -x respawn.sh >/dev/null 2>&1; then
        echo "  Already running — use 'restart' to pick up a new default-app"
        return 0
    fi

    # Write respawn wrapper (re-reads config on each restart cycle, so
    # changing /opt/roomwizard/default-app takes effect after the current
    # app exits)
    cat > "$RESPAWN_SCRIPT" << 'RESPAWN_EOF'
#!/bin/sh
CONFIG=/opt/roomwizard/default-app
LOGDIR=/var/log/roomwizard
LOGFILE=$LOGDIR/respawn.log
APPLOG=$LOGDIR/app_stdout.log
CHILD_PID=

# Recovery policy: after MAX_FAILURES consecutive failures of the configured
# app — either "not executable" or "exited in under FAST_EXIT_SECS" — switch to
# FALLBACK so the device always comes back to something usable.  FALLBACK never
# falls back to itself; a broken launcher settles into the capped retry below.
FALLBACK=/opt/roomwizard/app_launcher
MAX_FAILURES=3
FAST_EXIT_SECS=5
MAX_BACKOFF_SECS=30
LOG_MAX_BYTES=262144

FAIL_COUNT=0
USE_FALLBACK=0
PREV_CONFIGURED=

# Ensure log directory exists
mkdir -p "$LOGDIR"

# Rotate a log if over 256 KB.  Both logs are rotated at the top of the respawn
# loop, which is the right boundary: the child's '>>' redirection is reopened on
# every launch, so a rotation between launches actually frees the old inode.
rotate_one() {
    _rf=$1
    [ -f "$_rf" ] || return 0
    _rsz=$(wc -c < "$_rf" 2>/dev/null || echo 0)
    [ -z "$_rsz" ] && _rsz=0
    if [ "$_rsz" -gt "$LOG_MAX_BYTES" ]; then
        mv -f "$_rf" "$_rf.1"
    fi
}

rotate_logs() {
    rotate_one "$LOGFILE"
    rotate_one "$APPLOG"
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

# Switch to FALLBACK if we are not already on it.  Sets USE_FALLBACK even in the
# "nothing to fall back to" case, so the decision is not re-logged every cycle;
# the capped backoff then keeps a broken launcher from spinning.
try_fallback() {
    [ "$USE_FALLBACK" = 1 ] && return 1
    if [ ! -x "$FALLBACK" ]; then
        log "ERROR: fallback $FALLBACK is not executable — cannot recover automatically"
        USE_FALLBACK=1
        return 1
    fi
    if [ "$FALLBACK" = "$CONFIGURED" ]; then
        log "ERROR: the configured app IS the fallback ($FALLBACK) — nothing to fall back to"
        USE_FALLBACK=1
        return 1
    fi
    log "*** FALLING BACK to $FALLBACK after $FAIL_COUNT consecutive failures ***"
    USE_FALLBACK=1
    FAIL_COUNT=0
    return 0
}

while true; do
    rotate_logs

    CONFIGURED=$(head -1 "$CONFIG" 2>/dev/null | tr -d '[:space:]')

    # A newly configured default-app deserves a clean slate.
    if [ "$CONFIGURED" != "$PREV_CONFIGURED" ]; then
        if [ -n "$PREV_CONFIGURED" ]; then
            log "default-app changed ('$PREV_CONFIGURED' -> '$CONFIGURED'), clearing failure state"
        fi
        PREV_CONFIGURED=$CONFIGURED
        FAIL_COUNT=0
        USE_FALLBACK=0
    fi

    if [ "$USE_FALLBACK" = 1 ]; then
        APP=$FALLBACK
    else
        APP=$CONFIGURED
    fi

    if [ ! -x "$APP" ]; then
        FAIL_COUNT=$((FAIL_COUNT + 1))
        log "No valid app configured ($APP) [failure $FAIL_COUNT/$MAX_FAILURES]"
        if [ "$FAIL_COUNT" -ge "$MAX_FAILURES" ] && try_fallback; then
            continue        # retry the fallback immediately
        fi
        sleep 10
        continue
    fi

    log "Starting $APP"
    START_TS=$(date +%s)
    "$APP" >> "$APPLOG" 2>&1 &
    CHILD_PID=$!
    log "$APP running as PID $CHILD_PID"

    # Robust wait that also keeps the child's real exit status.  BusyBox ash
    # 'wait' can return early on a signal, so the kill -0 guard decides whether
    # the status is real; the sleep 1 stops that guard from busy-spinning.
    EXIT_CODE=0
    while true; do
        wait "$CHILD_PID"; EXIT_CODE=$?
        kill -0 "$CHILD_PID" 2>/dev/null || break
        sleep 1
    done

    RUNTIME=$(( $(date +%s) - START_TS ))
    [ "$RUNTIME" -lt 0 ] && RUNTIME=0
    # 'exited (code 132) after 0s' is the Cortex-A8 SIGILL signature in one line.
    log "$APP (PID $CHILD_PID) exited (code $EXIT_CODE) after ${RUNTIME}s"

    DELAY=2
    if [ "$RUNTIME" -lt "$FAST_EXIT_SECS" ]; then
        FAIL_COUNT=$((FAIL_COUNT + 1))
        DELAY=$((2 * FAIL_COUNT))
        [ "$DELAY" -gt "$MAX_BACKOFF_SECS" ] && DELAY=$MAX_BACKOFF_SECS
        log "  exited within ${FAST_EXIT_SECS}s — treating as a crash [failure $FAIL_COUNT/$MAX_FAILURES]"
        if [ "$FAIL_COUNT" -ge "$MAX_FAILURES" ]; then
            try_fallback && DELAY=2
        fi
    else
        FAIL_COUNT=0        # a clean long run clears the crash history
    fi

    log "  restarting in ${DELAY}s..."
    # Do NOT clear CHILD_PID until after sleep so cleanup() can
    # still kill the child if TERM arrives during the delay.
    sleep "$DELAY"
    CHILD_PID=
done
RESPAWN_EOF
    chmod +x "$RESPAWN_SCRIPT"

    echo "  Launching: ${APP_EXEC:-(none — will fall back)} (with respawn)"
    start-stop-daemon --start --background --make-pidfile \
        --pidfile "$PIDFILE" --exec "$RESPAWN_SCRIPT"
    SSD_RC=$?
    if [ "$SSD_RC" -ne 0 ]; then
        # rc=1 also means "already running", and the old unconditional fallback
        # fired exactly then — launching a second wrapper, so two apps fought
        # over /dev/fb0.  Only exec directly if nothing is actually up.
        if pidof -x respawn.sh >/dev/null 2>&1; then
            echo "  start-stop-daemon rc=$SSD_RC but the wrapper is running — not starting a second one"
        else
            echo "  start-stop-daemon failed (rc=$SSD_RC) — starting the wrapper directly"
            "$RESPAWN_SCRIPT" &
            echo $! > "$PIDFILE"
        fi
    fi
    echo "$DESC started (${APP_EXEC:-none})"
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

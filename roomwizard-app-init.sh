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

    echo "  Launching: $APP_EXEC"
    start-stop-daemon --start --background --make-pidfile \
        --pidfile "$PIDFILE" --exec "$APP_EXEC" || {
        # Fallback if start-stop-daemon fails
        "$APP_EXEC" &
        echo $! > "$PIDFILE"
    }
    echo "$DESC started ($APP_EXEC)"
    return 0
}

do_stop() {
    echo "Stopping $DESC..."
    if [ -f "$PIDFILE" ]; then
        start-stop-daemon --stop --pidfile "$PIDFILE" --retry 5 || \
            kill "$(cat "$PIDFILE")" 2>/dev/null || true
        rm -f "$PIDFILE"
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

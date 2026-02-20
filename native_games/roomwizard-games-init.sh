#!/bin/sh
### BEGIN INIT INFO
# Provides:          roomwizard-games
# Required-Start:    $local_fs $remote_fs
# Required-Stop:     $local_fs $remote_fs
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: RoomWizard Game System
# Description:       Boots directly to game selector, replaces browser/X11
### END INIT INFO

# Note: the system /usr/sbin/watchdog daemon (started by /etc/init.d/watchdog
# via /etc/default/watchdog) feeds /dev/watchdog.  No separate watchdog_feeder
# is needed here.

PATH=/sbin:/usr/sbin:/bin:/usr/bin:/opt/games
DESC="RoomWizard Game System"
NAME=roomwizard-games
GAME_SELECTOR=/opt/games/game_selector
PIDFILE_SELECTOR=/var/run/game_selector.pid

[ -f /etc/init.d/functions ] && . /etc/init.d/functions

do_start() {
    echo "Starting $DESC..."

    # Stop browser/X11 stack if still running
    killall Xorg 2>/dev/null || true
    killall browser 2>/dev/null || true
    killall epiphany 2>/dev/null || true
    killall webkit 2>/dev/null || true
    killall java 2>/dev/null || true
    sleep 2

    # Start game selector
    if [ -x "$GAME_SELECTOR" ]; then
        start-stop-daemon --start --background --make-pidfile \
            --pidfile $PIDFILE_SELECTOR --exec $GAME_SELECTOR || {
            $GAME_SELECTOR &
            echo $! > $PIDFILE_SELECTOR
        }
        echo "$DESC started"
    else
        echo "ERROR: game_selector not found at $GAME_SELECTOR"
        return 1
    fi
    return 0
}

do_stop() {
    echo "Stopping $DESC..."
    if [ -f $PIDFILE_SELECTOR ]; then
        start-stop-daemon --stop --pidfile $PIDFILE_SELECTOR --retry 5 || \
            kill $(cat $PIDFILE_SELECTOR) 2>/dev/null || true
        rm -f $PIDFILE_SELECTOR
    fi
    echo "$DESC stopped"
    return 0
}

do_status() {
    if [ -f $PIDFILE_SELECTOR ]; then
        PID=$(cat $PIDFILE_SELECTOR)
        if kill -0 $PID 2>/dev/null; then
            echo "game_selector: running (PID $PID)"
        else
            echo "game_selector: not running (stale PID)"
        fi
    else
        echo "game_selector: not running"
    fi
}

case "$1" in
    start)          do_start ;;
    stop)           do_stop ;;
    restart|force-reload) do_stop; sleep 2; do_start ;;
    status)         do_status ;;
    *) echo "Usage: $0 {start|stop|restart|status}"; exit 1 ;;
esac

exit 0

        ;;
    restart|force-reload)
        do_stop
        sleep 2
        do_start
        ;;
    status)
        do_status
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|status}"
        exit 1
        ;;
esac

exit 0

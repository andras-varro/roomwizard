#!/bin/sh
### BEGIN INIT INFO
# Provides:          roomwizard-games
# Required-Start:    $local_fs $remote_fs $all
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

# Stop conflicting services properly using their init scripts
stop_conflicting_services() {
    echo "Stopping conflicting services..."
    
    # Stop services in reverse order of their startup priority
    # browser (S35) -> webserver (S32) -> x11 (S30)
    for svc in browser webserver x11 ntpd; do
        if [ -x "/etc/init.d/$svc" ]; then
            echo "  Stopping $svc..."
            /etc/init.d/$svc stop 2>/dev/null || true
        fi
    done
    
    # Give services time to stop gracefully
    sleep 2
    
    # Force kill any remaining processes
    killall -9 browser 2>/dev/null || true
    killall -9 epiphany 2>/dev/null || true
    killall -9 webkit 2>/dev/null || true
    killall -9 Xorg 2>/dev/null || true
    killall -9 java 2>/dev/null || true
    killall -9 ntpd 2>/dev/null || true
    
    # Kill psplash if running (frees ~6MB RAM)
    killall -9 psplash 2>/dev/null || true
    
    sleep 1
    echo "Conflicting services stopped."
}

# Disable conflicting services from starting on boot
disable_conflicting_services() {
    echo "Disabling conflicting services from boot..."
    
    # Remove symlinks from all runlevels
    for svc in browser webserver x11 jetty hsqldb ntpd; do
        # Remove from rc5.d (main runlevel)
        rm -f /etc/rc5.d/S*${svc} 2>/dev/null || true
        rm -f /etc/rc5.d/K*${svc} 2>/dev/null || true
        # Also try update-rc.d if available
        update-rc.d -f $svc remove 2>/dev/null || true
    done
    
    echo "Conflicting services disabled."
}

do_start() {
    echo "Starting $DESC..."

    # First, properly stop conflicting services using their init scripts
    stop_conflicting_services
    
    # Disable them from starting on next boot
    disable_conflicting_services

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

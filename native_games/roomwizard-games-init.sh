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

PATH=/sbin:/usr/sbin:/bin:/usr/bin:/opt/games
DESC="RoomWizard Game System"
NAME=roomwizard-games
GAME_SELECTOR=/opt/games/game_selector
WATCHDOG_FEEDER=/opt/games/watchdog_feeder
PIDFILE_SELECTOR=/var/run/game_selector.pid
PIDFILE_WATCHDOG=/var/run/watchdog_feeder.pid

# Load init functions if available
[ -f /etc/init.d/functions ] && . /etc/init.d/functions

do_start() {
    echo "Starting $DESC..."
    
    # Make sure old services are stopped
    echo "Ensuring browser/X11 are stopped..."
    killall Xorg 2>/dev/null || true
    killall browser 2>/dev/null || true
    killall epiphany 2>/dev/null || true
    killall webkit 2>/dev/null || true
    killall java 2>/dev/null || true
    
    # Wait for processes to die
    sleep 2
    
    # Start watchdog feeder first (critical!)
    echo "Starting watchdog feeder..."
    if [ -x "$WATCHDOG_FEEDER" ]; then
        start-stop-daemon --start --background --make-pidfile \
            --pidfile $PIDFILE_WATCHDOG --exec $WATCHDOG_FEEDER || {
            # Fallback if start-stop-daemon not available
            $WATCHDOG_FEEDER &
            echo $! > $PIDFILE_WATCHDOG
        }
        echo "Watchdog feeder started"
    else
        echo "WARNING: Watchdog feeder not found at $WATCHDOG_FEEDER"
        echo "System may reset after 60 seconds!"
    fi
    
    # Give watchdog feeder time to start
    sleep 1
    
    # Start game selector
    echo "Starting game selector..."
    if [ -x "$GAME_SELECTOR" ]; then
        start-stop-daemon --start --background --make-pidfile \
            --pidfile $PIDFILE_SELECTOR --exec $GAME_SELECTOR || {
            # Fallback if start-stop-daemon not available
            $GAME_SELECTOR &
            echo $! > $PIDFILE_SELECTOR
        }
        echo "Game selector started"
    else
        echo "ERROR: Game selector not found at $GAME_SELECTOR"
        return 1
    fi
    
    echo "$DESC started successfully"
    return 0
}

do_stop() {
    echo "Stopping $DESC..."
    
    # Stop game selector
    if [ -f $PIDFILE_SELECTOR ]; then
        echo "Stopping game selector..."
        start-stop-daemon --stop --pidfile $PIDFILE_SELECTOR --retry 5 || {
            # Fallback
            kill $(cat $PIDFILE_SELECTOR) 2>/dev/null || true
        }
        rm -f $PIDFILE_SELECTOR
    fi
    
    # Stop watchdog feeder
    if [ -f $PIDFILE_WATCHDOG ]; then
        echo "Stopping watchdog feeder..."
        start-stop-daemon --stop --pidfile $PIDFILE_WATCHDOG --retry 5 || {
            # Fallback
            kill $(cat $PIDFILE_WATCHDOG) 2>/dev/null || true
        }
        rm -f $PIDFILE_WATCHDOG
    fi
    
    echo "$DESC stopped"
    return 0
}

do_status() {
    echo "Status of $DESC:"
    
    # Check game selector
    if [ -f $PIDFILE_SELECTOR ]; then
        PID=$(cat $PIDFILE_SELECTOR)
        if kill -0 $PID 2>/dev/null; then
            echo "  Game selector: running (PID: $PID)"
        else
            echo "  Game selector: not running (stale PID file)"
        fi
    else
        echo "  Game selector: not running"
    fi
    
    # Check watchdog feeder
    if [ -f $PIDFILE_WATCHDOG ]; then
        PID=$(cat $PIDFILE_WATCHDOG)
        if kill -0 $PID 2>/dev/null; then
            echo "  Watchdog feeder: running (PID: $PID)"
        else
            echo "  Watchdog feeder: not running (stale PID file)"
        fi
    else
        echo "  Watchdog feeder: not running"
    fi
    
    return 0
}

case "$1" in
    start)
        do_start
        ;;
    stop)
        do_stop
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

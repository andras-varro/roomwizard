#!/bin/sh
### BEGIN INIT INFO
# Provides:          game-mode
# Required-Start:    $local_fs
# Required-Stop:     $local_fs
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: RoomWizard Game Mode
# Description:       Stops browser/X11 and starts game selector
### END INIT INFO

PATH=/sbin:/usr/sbin:/bin:/usr/bin:/opt/games
DESC="RoomWizard Game Mode"
NAME=game-mode
GAME_SELECTOR=/opt/games/game_selector
WATCHDOG_FEEDER=/opt/games/watchdog_feeder
PIDFILE_SELECTOR=/var/run/game_selector.pid
PIDFILE_WATCHDOG=/var/run/watchdog_feeder.pid

# Load init functions
. /etc/init.d/functions

do_start() {
    echo "Starting $DESC..."
    
    # Stop conflicting services
    echo "Stopping browser and X11..."
    /etc/init.d/browser stop 2>/dev/null || true
    /etc/init.d/x11 stop 2>/dev/null || true
    
    # Wait for services to stop
    sleep 2
    
    # Kill any remaining X11 or browser processes
    killall Xorg 2>/dev/null || true
    killall browser 2>/dev/null || true
    killall epiphany 2>/dev/null || true
    killall webkit 2>/dev/null || true
    
    # Wait a bit more
    sleep 1
    
    # Start watchdog feeder in background
    echo "Starting watchdog feeder..."
    start-stop-daemon --start --background --make-pidfile \
        --pidfile $PIDFILE_WATCHDOG --exec $WATCHDOG_FEEDER
    
    # Start game selector
    echo "Starting game selector..."
    start-stop-daemon --start --background --make-pidfile \
        --pidfile $PIDFILE_SELECTOR --exec $GAME_SELECTOR
    
    echo "$DESC started"
    return 0
}

do_stop() {
    echo "Stopping $DESC..."
    
    # Stop game selector
    start-stop-daemon --stop --pidfile $PIDFILE_SELECTOR --retry 5
    rm -f $PIDFILE_SELECTOR
    
    # Stop watchdog feeder
    start-stop-daemon --stop --pidfile $PIDFILE_WATCHDOG --retry 5
    rm -f $PIDFILE_WATCHDOG
    
    # Restart normal services
    echo "Restarting X11 and browser..."
    /etc/init.d/x11 start 2>/dev/null || true
    sleep 2
    /etc/init.d/browser start 2>/dev/null || true
    
    echo "$DESC stopped"
    return 0
}

do_status() {
    if [ -f $PIDFILE_SELECTOR ]; then
        PID=$(cat $PIDFILE_SELECTOR)
        if kill -0 $PID 2>/dev/null; then
            echo "$DESC is running (PID: $PID)"
            return 0
        else
            echo "$DESC is not running (stale PID file)"
            return 1
        fi
    else
        echo "$DESC is not running"
        return 3
    fi
}

case "$1" in
    start)
        do_start
        ;;
    stop)
        do_stop
        ;;
    restart)
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

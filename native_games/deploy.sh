#!/bin/bash
# Quick deployment script for RoomWizard games
# Usage: ./deploy.sh <roomwizard-ip> [test|permanent]

set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <roomwizard-ip> [test|permanent]"
    echo ""
    echo "Examples:"
    echo "  $0 192.168.50.73 test       # Deploy and test (browser still auto-starts)"
    echo "  $0 192.168.50.73 permanent  # Deploy and make permanent (replaces browser)"
    exit 1
fi

DEVICE_IP="$1"
MODE="${2:-test}"

echo "========================================"
echo "RoomWizard Game Deployment"
echo "========================================"
echo "Device IP: $DEVICE_IP"
echo "Mode: $MODE"
echo ""

# Check if build directory exists
if [ ! -d "build" ]; then
    echo "ERROR: build/ directory not found!"
    echo "Run ./compile_for_roomwizard.sh first"
    exit 1
fi

# Check if binaries exist
if [ ! -f "build/snake" ] || [ ! -f "build/tetris" ] || [ ! -f "build/pong" ]; then
    echo "ERROR: Game binaries not found in build/"
    echo "Run ./compile_for_roomwizard.sh first"
    exit 1
fi

echo "[1/5] Transferring game binaries..."
scp build/snake build/tetris build/pong build/game_selector root@$DEVICE_IP:/opt/games/
echo ""

echo "[2/5] Transferring utilities..."
scp build/watchdog_feeder build/touch_test build/touch_debug build/hardware_test root@$DEVICE_IP:/opt/games/
echo ""

echo "[3/5] Transferring init script..."
scp roomwizard-games-init.sh root@$DEVICE_IP:/etc/init.d/roomwizard-games
echo ""

echo "[4/5] Setting permissions..."
ssh root@$DEVICE_IP 'chmod +x /opt/games/* /etc/init.d/roomwizard-games'
echo ""

if [ "$MODE" = "permanent" ]; then
    echo "[5/5] Making game mode permanent..."
    echo "  - Disabling browser and X11 services"
    ssh root@$DEVICE_IP 'update-rc.d -f browser remove && update-rc.d -f x11 remove'
    echo "  - Enabling game mode service"
    ssh root@$DEVICE_IP 'update-rc.d roomwizard-games defaults'
    echo ""
    echo "========================================"
    echo "Deployment Complete!"
    echo "========================================"
    echo ""
    echo "Game mode is now PERMANENT."
    echo "The device will boot into game mode on restart."
    echo ""
    echo "To reboot now:"
    echo "  ssh root@$DEVICE_IP reboot"
    echo ""
    echo "To rollback to browser mode:"
    echo "  ssh root@$DEVICE_IP 'update-rc.d -f roomwizard-games remove'"
    echo "  ssh root@$DEVICE_IP 'update-rc.d browser defaults && update-rc.d x11 defaults'"
    echo "  ssh root@$DEVICE_IP reboot"
else
    echo "[5/5] Starting game mode (test)..."
    ssh root@$DEVICE_IP '/etc/init.d/roomwizard-games start'
    echo ""
    echo "========================================"
    echo "Deployment Complete!"
    echo "========================================"
    echo ""
    echo "Game mode is running in TEST mode."
    echo "Browser will still auto-start on reboot."
    echo ""
    echo "To stop game mode:"
    echo "  ssh root@$DEVICE_IP '/etc/init.d/roomwizard-games stop'"
    echo ""
    echo "To make permanent:"
    echo "  ./deploy.sh $DEVICE_IP permanent"
fi

echo ""

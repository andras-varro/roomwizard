#!/bin/bash
# Quick compilation script for RoomWizard games in WSL
# Run this in WSL: ./compile_for_roomwizard.sh

set -e

echo "========================================"
echo "RoomWizard Game Mode Compilation"
echo "========================================"
echo ""

# Check for cross-compiler
if ! command -v arm-linux-gnueabihf-gcc &> /dev/null; then
    echo "ERROR: ARM cross-compiler not found!"
    echo "Install with: sudo apt-get install gcc-arm-linux-gnueabihf"
    exit 1
fi

# Set compiler
export CC=arm-linux-gnueabihf-gcc
echo "Using compiler: $CC"
$CC --version | head -1
echo ""

# Create build directory
mkdir -p build
echo "Build directory: build/"
echo ""

# Compile common libraries
echo "[1/11] Compiling framebuffer library..."
$CC -O2 -static -c common/framebuffer.c -o build/framebuffer.o

echo "[2/11] Compiling touch input library..."
$CC -O2 -static -c common/touch_input.c -o build/touch_input.o

echo "[3/11] Compiling hardware control library..."
$CC -O2 -static -c common/hardware.c -o build/hardware.o

echo "[4/11] Compiling game common library..."
$CC -O2 -static -c common/game_common.c -o build/game_common.o

# Compile games
echo "[5/11] Compiling Snake..."
$CC -O2 -static snake/snake.c build/framebuffer.o build/touch_input.o build/game_common.o build/hardware.o -o build/snake -lm

echo "[6/11] Compiling Tetris..."
$CC -O2 -static tetris/tetris.c build/framebuffer.o build/touch_input.o build/game_common.o build/hardware.o -o build/tetris -lm

echo "[7/11] Compiling Pong..."
$CC -O2 -static pong/pong.c build/framebuffer.o build/touch_input.o build/game_common.o build/hardware.o -o build/pong -lm

# Compile utilities
echo "[8/11] Compiling Game Selector..."
$CC -O2 -static game_selector.c build/framebuffer.o build/touch_input.o build/game_common.o build/hardware.o -o build/game_selector -lm

echo "[9/11] Compiling Watchdog Feeder..."
$CC -O2 -static watchdog_feeder.c -o build/watchdog_feeder

echo "[10/11] Compiling Touch Test..."
$CC -O2 -static touch_test.c build/framebuffer.o build/touch_input.o build/hardware.o -o build/touch_test -lm

echo "[11/11] Compiling Hardware Test..."
$CC -O2 -static hardware_test.c build/hardware.o -o build/hardware_test

echo ""
echo "========================================"
echo "Build Complete!"
echo "========================================"
echo ""

# Verify binaries
echo "Verifying ARM binaries:"
file build/snake build/tetris build/pong build/game_selector build/watchdog_feeder build/touch_test build/hardware_test | sed 's/^/  /'

echo ""
echo "File sizes:"
ls -lh build/snake build/tetris build/pong build/game_selector build/watchdog_feeder build/touch_test build/hardware_test | awk '{print "  " $9 ": " $5}'
echo ""
echo "========================================"
echo "Next Steps:"
echo "========================================"
echo ""
echo "1. Transfer to RoomWizard:"
echo "   scp build/* root@<roomwizard-ip>:/opt/games/"
echo "   scp roomwizard-games-init.sh root@<roomwizard-ip>:/etc/init.d/roomwizard-games"
echo ""
echo "2. Set permissions on device:"
echo "   ssh root@<roomwizard-ip> 'chmod +x /opt/games/* /etc/init.d/roomwizard-games'"
echo ""
echo "3. For permanent game mode (replaces browser):"
echo "   ssh root@<roomwizard-ip> 'update-rc.d browser remove && update-rc.d x11 remove'"
echo "   ssh root@<roomwizard-ip> 'update-rc.d roomwizard-games defaults && reboot'"
echo ""
echo "   OR for temporary testing:"
echo "   ssh root@<roomwizard-ip> '/etc/init.d/roomwizard-games start'"
echo ""
echo "See PERMANENT_GAME_MODE.md for complete instructions."
echo ""

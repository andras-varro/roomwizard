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
echo "[1/12] Compiling framebuffer library..."
$CC -O2 -static -c common/framebuffer.c -o build/framebuffer.o

echo "[2/12] Compiling touch input library..."
$CC -O2 -static -c common/touch_input.c -o build/touch_input.o

echo "[3/12] Compiling hardware control library..."
$CC -O2 -static -c common/hardware.c -o build/hardware.o

echo "[4/12] Compiling unified common library..."
$CC -O2 -static -c common/common.c -o build/common.o

echo "[5/12] Compiling UI layout library..."
$CC -O2 -static -c common/ui_layout.c -o build/ui_layout.o

# Compile games
echo "[6/12] Compiling Snake..."
$CC -O2 -static snake/snake.c build/framebuffer.o build/touch_input.o build/common.o build/hardware.o -o build/snake -lm

echo "[7/12] Compiling Tetris..."
$CC -O2 -static tetris/tetris.c build/framebuffer.o build/touch_input.o build/common.o build/hardware.o -o build/tetris -lm

echo "[8/12] Compiling Pong..."
$CC -O2 -static pong/pong.c build/framebuffer.o build/touch_input.o build/common.o build/hardware.o -o build/pong -lm

# Compile utilities
echo "[9/12] Compiling Game Selector..."
$CC -O2 -static game_selector/game_selector.c build/framebuffer.o build/touch_input.o build/common.o build/ui_layout.o build/hardware.o -o build/game_selector -lm

echo "[10/12] Compiling Watchdog Feeder..."
$CC -O2 -static watchdog_feeder/watchdog_feeder.c -o build/watchdog_feeder

echo "[11/12] Compiling Hardware Test (GUI)..."
$CC -O2 -static hardware_test/hardware_test_gui.c build/framebuffer.o build/touch_input.o build/common.o build/ui_layout.o build/hardware.o -o build/hardware_test -lm

echo "[12/14] Compiling Touch Inject (Test Utility)..."
$CC -O2 -static tests/touch_inject.c -o build/touch_inject

echo "[13/14] Compiling Touch Calibration Utility..."
$CC -O2 -static tests/touch_calibrate.c build/framebuffer.o build/touch_input.o build/hardware.o -o build/touch_calibrate -lm

echo "[14/14] Compiling Unified Calibration Utility..."
$CC -O2 -static tests/unified_calibrate.c build/framebuffer.o build/touch_input.o build/hardware.o -o build/unified_calibrate -lm

echo ""
echo "========================================"
echo "Build Complete!"
echo "========================================"
echo ""

# Verify binaries
echo "Verifying ARM binaries:"
file build/snake build/tetris build/pong build/game_selector build/watchdog_feeder build/hardware_test build/touch_calibrate | sed 's/^/  /'

echo ""
echo "File sizes:"
ls -lh build/snake build/tetris build/pong build/game_selector build/watchdog_feeder build/hardware_test build/touch_calibrate | awk '{print "  " $9 ": " $5}'
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

#!/bin/bash
# Build script for native games on RoomWizard device
# This script compiles the games with optimizations for ARM

set -e

echo "================================"
echo "Native Games Build Script"
echo "================================"
echo ""

# Check if we're on ARM
ARCH=$(uname -m)
echo "Architecture: $ARCH"

# Compiler flags optimized for 300MHz ARM
if [[ "$ARCH" == arm* ]] || [[ "$ARCH" == aarch* ]]; then
    echo "ARM architecture detected - using optimized flags"
    export CFLAGS="-O2 -Wall -Wextra -march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=hard"
else
    echo "Non-ARM architecture - using generic flags"
    export CFLAGS="-O2 -Wall -Wextra"
fi

# Create build directory
mkdir -p build

echo ""
echo "Building common libraries..."
gcc $CFLAGS -c common/framebuffer.c -o build/framebuffer.o
gcc $CFLAGS -c common/touch_input.c -o build/touch_input.o

echo "Building Snake..."
gcc $CFLAGS snake/snake.c build/framebuffer.o build/touch_input.o -o build/snake -lm

echo "Building Tetris..."
gcc $CFLAGS tetris/tetris.c build/framebuffer.o build/touch_input.o -o build/tetris -lm

echo "Building Pong..."
gcc $CFLAGS pong/pong.c build/framebuffer.o build/touch_input.o -o build/pong -lm

echo ""
echo "================================"
echo "Build complete!"
echo "================================"
echo ""
echo "Executables created in build/ directory:"
ls -lh build/snake build/tetris build/pong

echo ""
echo "To run games:"
echo "  ./build/snake"
echo "  ./build/tetris"
echo "  ./build/pong"
echo ""
echo "To install system-wide (requires root):"
echo "  sudo make install"

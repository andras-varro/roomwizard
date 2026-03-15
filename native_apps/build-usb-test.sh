#!/bin/bash
# Build just usb_test with ARM cross-compiler
set -e
cd "$(dirname "$0")"
CC=arm-linux-gnueabihf-gcc
mkdir -p build

echo "=== Building common objects ==="
$CC -O2 -static -c common/framebuffer.c -o build/framebuffer.o
$CC -O2 -static -c common/touch_input.c -o build/touch_input.o
$CC -O2 -static -c common/hardware.c    -o build/hardware.o
$CC -O2 -static -c common/common.c      -o build/common.o
$CC -O2 -static -c common/highscore.c   -o build/highscore.o
$CC -O2 -static -c common/audio.c       -o build/audio.o
$CC -O2 -static -c common/config.c      -o build/config.o
$CC -O2 -static -c common/ppm.c         -o build/ppm.o
$CC -O2 -static -c common/logger.c      -o build/logger.o
$CC -O2 -static -c common/ui_layout.c   -o build/ui_layout.o

COMMON_OBJ="build/framebuffer.o build/touch_input.o build/hardware.o build/common.o build/highscore.o build/audio.o build/config.o"

echo "=== Building usb_test ==="
$CC -O2 -static -I. usb_test/usb_test.c $COMMON_OBJ build/ui_layout.o -o build/usb_test -lm

echo "=== BUILD SUCCESS ==="
file build/usb_test
ls -lh build/usb_test

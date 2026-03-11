# Makefile for Native Games
# Optimized for ARM embedded systems (300MHz)

CC = gcc
CFLAGS = -O2 -Wall -Wextra -march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=hard
LDFLAGS = -lm

# Directories
COMMON_DIR = common
SNAKE_DIR = snake
TETRIS_DIR = tetris
PONG_DIR = pong
BRICK_DIR = brick_breaker
BUILD_DIR = build

# Common source files
COMMON_SRC = $(COMMON_DIR)/framebuffer.c $(COMMON_DIR)/touch_input.c $(COMMON_DIR)/hardware.c $(COMMON_DIR)/common.c $(COMMON_DIR)/ppm.c $(COMMON_DIR)/logger.c $(COMMON_DIR)/highscore.c
COMMON_OBJ = $(BUILD_DIR)/framebuffer.o $(BUILD_DIR)/touch_input.o $(BUILD_DIR)/hardware.o $(BUILD_DIR)/common.o $(BUILD_DIR)/ppm.o $(BUILD_DIR)/logger.o $(BUILD_DIR)/highscore.o

# Additional common objects for device_tools
COMMON_AUDIO_OBJ = $(BUILD_DIR)/audio.o
COMMON_CONFIG_OBJ = $(BUILD_DIR)/config.o
COMMON_UILAYOUT_OBJ = $(BUILD_DIR)/ui_layout.o
DEVICE_TOOLS_DEPS = $(COMMON_OBJ) $(COMMON_AUDIO_OBJ) $(COMMON_CONFIG_OBJ) $(COMMON_UILAYOUT_OBJ)

# Targets
GAMES = $(BUILD_DIR)/snake $(BUILD_DIR)/tetris $(BUILD_DIR)/pong $(BUILD_DIR)/brick_breaker
UTILS = $(BUILD_DIR)/game_selector $(BUILD_DIR)/app_launcher $(BUILD_DIR)/watchdog_feeder $(BUILD_DIR)/device_tools
TESTS = $(BUILD_DIR)/touch_test $(BUILD_DIR)/touch_calibrate $(BUILD_DIR)/touch_inject $(BUILD_DIR)/unified_calibrate $(BUILD_DIR)/audio_touch_test

.PHONY: all clean install utils tests icons

all: $(BUILD_DIR) $(GAMES) $(UTILS) icons

utils: $(BUILD_DIR) $(UTILS)

tests: $(BUILD_DIR) $(TESTS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Common object files
$(BUILD_DIR)/framebuffer.o: $(COMMON_DIR)/framebuffer.c $(COMMON_DIR)/framebuffer.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/touch_input.o: $(COMMON_DIR)/touch_input.c $(COMMON_DIR)/touch_input.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/hardware.o: $(COMMON_DIR)/hardware.c $(COMMON_DIR)/hardware.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/common.o: $(COMMON_DIR)/common.c $(COMMON_DIR)/common.h $(COMMON_DIR)/highscore.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/highscore.o: $(COMMON_DIR)/highscore.c $(COMMON_DIR)/highscore.h $(COMMON_DIR)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ppm.o: $(COMMON_DIR)/ppm.c $(COMMON_DIR)/ppm.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/logger.o: $(COMMON_DIR)/logger.c $(COMMON_DIR)/logger.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/audio.o: $(COMMON_DIR)/audio.c $(COMMON_DIR)/audio.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/config.o: $(COMMON_DIR)/config.c $(COMMON_DIR)/config.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ui_layout.o: $(COMMON_DIR)/ui_layout.c $(COMMON_DIR)/ui_layout.h
	$(CC) $(CFLAGS) -c $< -o $@

# Snake game
$(BUILD_DIR)/snake: $(SNAKE_DIR)/snake.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: snake"

# Tetris game
$(BUILD_DIR)/tetris: $(TETRIS_DIR)/tetris.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: tetris"

# Pong game
$(BUILD_DIR)/pong: $(PONG_DIR)/pong.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: pong"

# Brick Breaker game
$(BUILD_DIR)/brick_breaker: $(BRICK_DIR)/brick_breaker.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: brick_breaker"

# Game selector
$(BUILD_DIR)/game_selector: game_selector.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: game_selector"

# App launcher
$(BUILD_DIR)/app_launcher: app_launcher/app_launcher.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) -I. $^ -o $@ $(LDFLAGS)
	@echo "Built: app_launcher"

# Watchdog feeder
$(BUILD_DIR)/watchdog_feeder: watchdog_feeder.c
	$(CC) $(CFLAGS) $< -o $@
	@echo "Built: watchdog_feeder"

# Touch test tool
$(BUILD_DIR)/touch_test: tests/touch_test.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: touch_test"

# Touch calibration tool
$(BUILD_DIR)/touch_calibrate: tests/touch_calibrate.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: touch_calibrate"

# Touch injection tool
$(BUILD_DIR)/touch_inject: tests/touch_inject.c
	$(CC) $(CFLAGS) $< -o $@
	@echo "Built: touch_inject"

# Unified calibration tool
$(BUILD_DIR)/unified_calibrate: tests/unified_calibrate.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: unified_calibrate"

# Audio touch test (Tap-a-Theremin)
$(BUILD_DIR)/audio_touch_test: tests/audio_touch_test.c $(COMMON_OBJ) $(COMMON_AUDIO_OBJ) $(COMMON_CONFIG_OBJ)
	$(CC) $(CFLAGS) -I. $^ -o $@ $(LDFLAGS)
	@echo "Built: audio_touch_test"

# Hardware test tool
$(BUILD_DIR)/hardware_test: hardware_test/hardware_test.c $(BUILD_DIR)/hardware.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: hardware_test"

# Device Tools (unified settings/diagnostics/tests/calibration)
$(BUILD_DIR)/device_tools: device_tools/device_tools.c $(DEVICE_TOOLS_DEPS)
	$(CC) $(CFLAGS) -I. $^ -o $@ $(LDFLAGS)
	@echo "Built: device_tools"

# Install to /opt/games (requires root)
install: all
	mkdir -p /opt/games
	mkdir -p /etc/init.d
	cp $(BUILD_DIR)/snake /opt/games/
	cp $(BUILD_DIR)/tetris /opt/games/
	cp $(BUILD_DIR)/pong /opt/games/
	cp $(BUILD_DIR)/game_selector /opt/games/
	cp $(BUILD_DIR)/watchdog_feeder /opt/games/
	cp $(BUILD_DIR)/device_tools /opt/games/
	cp game-mode-init.sh /etc/init.d/game-mode
	chmod +x /opt/games/*
	chmod +x /etc/init.d/game-mode
	@echo "Games and utilities installed to /opt/games"
	@echo "Init script installed to /etc/init.d/game-mode"
	@echo ""
	@echo "To enable game mode at boot:"
	@echo "  update-rc.d game-mode defaults"
	@echo ""
	@echo "To start game mode now:"
	@echo "  /etc/init.d/game-mode start"

# Collect PPM icons from source dirs
icons: $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/icons
	@for f in */*.ppm; do [ -f "$$f" ] && cp "$$f" $(BUILD_DIR)/icons/ && echo "  Icon: $$f"; done || true

clean:
	rm -rf $(BUILD_DIR)
	@echo "Cleaned build directory"

# Help target
help:
	@echo "Native Games Makefile"
	@echo "====================="
	@echo "Targets:"
	@echo "  all      - Build all games (default)"
	@echo "  snake    - Build snake game only"
	@echo "  tetris   - Build tetris game only"
	@echo "  pong     - Build pong game only"
	@echo "  install  - Install games to /opt/games (requires root)"
	@echo "  clean    - Remove build directory"
	@echo "  help     - Show this help message"
	@echo ""
	@echo "Usage:"
	@echo "  make              # Build all games"
	@echo "  make snake        # Build snake only"
	@echo "  make install      # Install to system"
	@echo "  make clean        # Clean build files"

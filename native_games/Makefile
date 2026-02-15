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
BUILD_DIR = build

# Common source files
COMMON_SRC = $(COMMON_DIR)/framebuffer.c $(COMMON_DIR)/touch_input.c $(COMMON_DIR)/hardware.c $(COMMON_DIR)/common.c
COMMON_OBJ = $(BUILD_DIR)/framebuffer.o $(BUILD_DIR)/touch_input.o $(BUILD_DIR)/hardware.o $(BUILD_DIR)/common.o

# Targets
GAMES = $(BUILD_DIR)/snake $(BUILD_DIR)/tetris $(BUILD_DIR)/pong
UTILS = $(BUILD_DIR)/game_selector $(BUILD_DIR)/watchdog_feeder

.PHONY: all clean install utils

all: $(BUILD_DIR) $(GAMES) $(UTILS)

utils: $(BUILD_DIR) $(UTILS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Common object files
$(BUILD_DIR)/framebuffer.o: $(COMMON_DIR)/framebuffer.c $(COMMON_DIR)/framebuffer.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/touch_input.o: $(COMMON_DIR)/touch_input.c $(COMMON_DIR)/touch_input.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/hardware.o: $(COMMON_DIR)/hardware.c $(COMMON_DIR)/hardware.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/common.o: $(COMMON_DIR)/common.c $(COMMON_DIR)/common.h
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

# Game selector
$(BUILD_DIR)/game_selector: game_selector.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: game_selector"

# Watchdog feeder
$(BUILD_DIR)/watchdog_feeder: watchdog_feeder.c
	$(CC) $(CFLAGS) $< -o $@
	@echo "Built: watchdog_feeder"

# Touch test tool
$(BUILD_DIR)/touch_test: touch_test.c $(COMMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: touch_test"

# Hardware test tool
$(BUILD_DIR)/hardware_test: hardware_test.c $(BUILD_DIR)/hardware.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: hardware_test"

# Install to /opt/games (requires root)
install: all
	mkdir -p /opt/games
	mkdir -p /etc/init.d
	cp $(BUILD_DIR)/snake /opt/games/
	cp $(BUILD_DIR)/tetris /opt/games/
	cp $(BUILD_DIR)/pong /opt/games/
	cp $(BUILD_DIR)/game_selector /opt/games/
	cp $(BUILD_DIR)/watchdog_feeder /opt/games/
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

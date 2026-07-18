# qemu-connect — QEMU TCG plugin + agent control plane
# SPDX-License-Identifier: GPL-2.0-or-later

PLUGIN_NAME    := libqemu-connect.so
CLI_NAME       := qemu-connect

BUILD_DIR      := build
PLUGIN_DIR     := plugin
CLI_DIR        := cli
INC_DIR        := include
TEST_DIR     := tests

CC             ?= gcc
CFLAGS         ?= -O2 -g -Wall -Wextra -Wpedantic
CFLAGS         += -fPIC -fvisibility=hidden -std=c11 -pthread
CPPFLAGS       += -I$(INC_DIR) -I$(PLUGIN_DIR)
CPPFLAGS       += $(shell pkg-config --cflags glib-2.0 2>/dev/null)
LDFLAGS_PLUGIN := -shared -pthread
LIBS_PLUGIN    := $(shell pkg-config --libs glib-2.0 2>/dev/null)

PLUGIN_SRCS    := \
	$(PLUGIN_DIR)/agent.c \
	$(PLUGIN_DIR)/vga.c \
	$(PLUGIN_DIR)/server.c \
	$(PLUGIN_DIR)/protocol.c \
	$(PLUGIN_DIR)/mem.c

PLUGIN_OBJS    := $(patsubst $(PLUGIN_DIR)/%.c,$(BUILD_DIR)/%.o,$(PLUGIN_SRCS))
CLI_OBJS       := $(BUILD_DIR)/cli_main.o
VGA_UNIT_OBJS  := $(BUILD_DIR)/vga.o $(BUILD_DIR)/test_vga_unit.o

.PHONY: all plugin cli clean dirs help test-load test-ping test-vga-unit \
	test-munux-console smoke

all: plugin cli

help:
	@echo "qemu-connect targets:"
	@echo "  all                 Build plugin + CLI (default)"
	@echo "  plugin / cli        Individual builds"
	@echo "  test-ping           PR1: socket thread + ping"
	@echo "  test-vga-unit       PR2: LE u16 cell → char (host only)"
	@echo "  test-munux-console  PR2: boot munux ISO, scrape panic text"
	@echo "  smoke               test-ping + test-vga-unit (+ munux if present)"
	@echo "  clean"

dirs:
	@mkdir -p $(BUILD_DIR)

plugin: dirs $(BUILD_DIR)/$(PLUGIN_NAME)

cli: dirs $(BUILD_DIR)/$(CLI_NAME)

$(BUILD_DIR)/$(PLUGIN_NAME): $(PLUGIN_OBJS)
	$(CC) $(LDFLAGS_PLUGIN) -o $@ $^ $(LIBS_PLUGIN)
	@echo "built $@"

$(BUILD_DIR)/$(CLI_NAME): $(CLI_OBJS)
	$(CC) -o $@ $^
	@echo "built $@"

$(BUILD_DIR)/%.o: $(PLUGIN_DIR)/%.c | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/cli_main.o: $(CLI_DIR)/main.c | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_vga_unit.o: $(TEST_DIR)/test_vga_unit.c | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_vga_unit: $(VGA_UNIT_OBJS)
	$(CC) -pthread -o $@ $^
	@echo "built $@"

clean:
	rm -rf $(BUILD_DIR)

test-load: plugin
	@command -v qemu-system-x86_64 >/dev/null || { echo "qemu-system-x86_64 not found"; exit 1; }
	qemu-system-x86_64 -display none -machine none -accel tcg \
		-plugin ./$(BUILD_DIR)/$(PLUGIN_NAME),socket=/tmp/qemu-connect-test.sock \
		-monitor stdio

test-ping: plugin cli
	@command -v qemu-system-x86_64 >/dev/null || { echo "qemu-system-x86_64 not found"; exit 1; }
	@bash scripts/test-ping.sh

test-vga-unit: $(BUILD_DIR)/test_vga_unit
	@$(BUILD_DIR)/test_vga_unit

test-munux-console: plugin cli
	@bash scripts/test-munux-console.sh

smoke: test-ping test-vga-unit
	@if [ -d test/munux ]; then $(MAKE) test-munux-console; else echo "SKIP munux (test/munux missing)"; fi

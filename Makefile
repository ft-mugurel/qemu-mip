# qemu-connect — QEMU TCG plugin + agent control plane
# SPDX-License-Identifier: GPL-2.0-or-later

PLUGIN_NAME    := libqemu-connect.so
CLI_NAME       := qemu-connect

BUILD_DIR      := build
PLUGIN_DIR     := plugin
CLI_DIR        := cli
INC_DIR        := include

CC             ?= gcc
CFLAGS         ?= -O2 -g -Wall -Wextra -Wpedantic
CFLAGS         += -fPIC -fvisibility=hidden -std=c11
CPPFLAGS       += -I$(INC_DIR) -I$(PLUGIN_DIR)
CPPFLAGS       += $(shell pkg-config --cflags glib-2.0 2>/dev/null)
LDFLAGS_PLUGIN := -shared
LIBS_PLUGIN    := $(shell pkg-config --libs glib-2.0 2>/dev/null)

PLUGIN_SRCS    := \
	$(PLUGIN_DIR)/agent.c \
	$(PLUGIN_DIR)/vga.c \
	$(PLUGIN_DIR)/server.c \
	$(PLUGIN_DIR)/protocol.c

PLUGIN_OBJS    := $(patsubst $(PLUGIN_DIR)/%.c,$(BUILD_DIR)/%.o,$(PLUGIN_SRCS))
CLI_SRCS       := $(CLI_DIR)/main.c
CLI_OBJS       := $(BUILD_DIR)/cli_main.o

.PHONY: all plugin cli clean dirs help test-load

all: plugin cli

help:
	@echo "qemu-connect targets:"
	@echo "  all        Build plugin + CLI (default)"
	@echo "  plugin     Build $(PLUGIN_NAME)"
	@echo "  cli        Build $(CLI_NAME)"
	@echo "  clean      Remove build artifacts"
	@echo "  test-load  Try loading the plugin into qemu-system-x86_64"

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

clean:
	rm -rf $(BUILD_DIR)

# Smoke: QEMU must accept the plugin without aborting on install.
test-load: plugin
	@command -v qemu-system-x86_64 >/dev/null || { echo "qemu-system-x86_64 not found"; exit 1; }
	@echo "Starting QEMU with plugin (Ctrl-C / quit to stop)..."
	qemu-system-x86_64 -display none -machine none \
		-plugin ./$(BUILD_DIR)/$(PLUGIN_NAME),socket=/tmp/qemu-connect-test.sock \
		-monitor stdio

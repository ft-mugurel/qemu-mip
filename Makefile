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
CPPFLAGS       += -I$(INC_DIR) -I$(PLUGIN_DIR) -I$(CLI_DIR)
CPPFLAGS       += $(shell pkg-config --cflags glib-2.0 2>/dev/null)
LDFLAGS_PLUGIN := -shared -pthread
LIBS_PLUGIN    := $(shell pkg-config --libs glib-2.0 2>/dev/null)

PLUGIN_SRCS    := \
	$(PLUGIN_DIR)/agent.c \
	$(PLUGIN_DIR)/vga.c \
	$(PLUGIN_DIR)/server.c \
	$(PLUGIN_DIR)/protocol.c \
	$(PLUGIN_DIR)/mem.c \
	$(PLUGIN_DIR)/queue.c \
	$(PLUGIN_DIR)/hypercall.c

PLUGIN_OBJS    := $(patsubst $(PLUGIN_DIR)/%.c,$(BUILD_DIR)/%.o,$(PLUGIN_SRCS))
CLI_OBJS       := $(BUILD_DIR)/cli_main.o $(BUILD_DIR)/cli_qmp.o \
	$(BUILD_DIR)/cli_run.o $(BUILD_DIR)/cli_session.o \
	$(BUILD_DIR)/cli_paths.o
VGA_UNIT_OBJS  := $(BUILD_DIR)/vga.o $(BUILD_DIR)/test_vga_unit.o

.PHONY: all plugin cli guest clean dirs help test-load test-ping test-vga-unit \
	test-munux-iso test-munux-console test-munux-shell test-refresh \
	test-qmp test-run test-mem-hypercall smoke

all: plugin cli

help:
	@echo "qemu-connect targets:"
	@echo "  all / plugin / cli"
	@echo "  guest [CMD=help]     one-shot munux boot/type/show"
	@echo "  session              multi-cmd without reboot (see AGENTS.md)"
	@echo "  test-ping test-vga-unit test-qmp test-run test-mem-hypercall"
	@echo "  test-munux-shell test-refresh smoke"

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

$(BUILD_DIR)/cli_qmp.o: $(CLI_DIR)/qmp.c | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/cli_run.o: $(CLI_DIR)/run.c | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/cli_session.o: $(CLI_DIR)/session.c | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/cli_paths.o: $(CLI_DIR)/paths.c | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_vga_unit.o: $(TEST_DIR)/test_vga_unit.c | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_vga_unit: $(VGA_UNIT_OBJS)
	$(CC) -pthread -o $@ $^

$(BUILD_DIR)/test_hypercall_unit.o: $(TEST_DIR)/test_hypercall_unit.c | dirs
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_hypercall_unit: $(BUILD_DIR)/hypercall.o $(BUILD_DIR)/test_hypercall_unit.o
	$(CC) -pthread -o $@ $^

clean:
	rm -rf $(BUILD_DIR)

test-ping: plugin cli
	@bash scripts/test-ping.sh

test-vga-unit: $(BUILD_DIR)/test_vga_unit
	@$(BUILD_DIR)/test_vga_unit

test-hypercall-unit: $(BUILD_DIR)/test_hypercall_unit
	@$(BUILD_DIR)/test_hypercall_unit

test-qmp: cli
	@bash scripts/test-qmp.sh

test-run: plugin cli
	@bash scripts/test-run.sh

test-mem-hypercall: plugin cli
	@bash scripts/test-mem-hypercall.sh

test-munux-iso:
	@if [ ! -d test/munux ]; then echo "SKIP munux"; exit 0; fi
	@$(MAKE) -C test/munux iso

test-munux-shell: plugin cli
	@bash scripts/test-munux-shell.sh

test-munux-console: test-munux-shell

test-refresh: plugin cli
	@bash scripts/test-refresh.sh

# Simple: make guest CMD='help'
guest: plugin cli
	@if [ ! -f test/munux/build/kernel.iso ] || [ ! -f test/munux/build/disk.img ]; then \
		$(MAKE) -C test/munux iso disk; fi
	@./$(BUILD_DIR)/$(CLI_NAME) guest $(CMD)

smoke: test-ping test-vga-unit test-qmp
	@if [ -d test/munux ]; then \
		$(MAKE) test-munux-shell && $(MAKE) test-refresh && $(MAKE) test-run && $(MAKE) test-mem-hypercall; \
	else \
		echo "SKIP munux"; \
	fi

# --------------------------------------------------------------------------- #
# Install (easy default: make install  → ~/.local)
# --------------------------------------------------------------------------- #

PREFIX        ?= $(HOME)/.local
BINDIR        := $(PREFIX)/bin
LIBDIR        := $(PREFIX)/lib/qemu-connect
SHAREDIR      := $(PREFIX)/share/qemu-connect
MCPDIR        := $(SHAREDIR)/mcp

.PHONY: install uninstall install-mcp install-all

install: plugin cli
	@mkdir -p "$(BINDIR)" "$(LIBDIR)" "$(SHAREDIR)"
	install -m 755 "$(BUILD_DIR)/$(CLI_NAME)" "$(BINDIR)/$(CLI_NAME)"
	install -m 755 "$(BUILD_DIR)/$(PLUGIN_NAME)" "$(LIBDIR)/$(PLUGIN_NAME)"
	install -m 644 README.md AGENTS.md LICENSE "$(SHAREDIR)/" 2>/dev/null || true
	@# small env helper for shells
	@printf '%s\n' \
		'# qemu-connect environment (optional: source this)' \
		'export QEMU_CONNECT_HOME="$(SHAREDIR)"' \
		'export QEMU_CONNECT_PLUGIN="$(LIBDIR)/$(PLUGIN_NAME)"' \
		'export QEMU_CONNECT_ROOT="$${QEMU_CONNECT_ROOT:-$(CURDIR)}"' \
		> "$(SHAREDIR)/env.sh"
	@chmod 644 "$(SHAREDIR)/env.sh"
	@echo ""
	@echo "Installed:"
	@echo "  CLI     $(BINDIR)/$(CLI_NAME)"
	@echo "  plugin  $(LIBDIR)/$(PLUGIN_NAME)"
	@echo "  share   $(SHAREDIR)/"
	@echo ""
	@echo "Ensure $(BINDIR) is on your PATH, then:"
	@echo "  export QEMU_CONNECT_ROOT=$(CURDIR)   # workspace with test/munux"
	@echo "  qemu-connect guest help"
	@echo ""
	@echo "Optional MCP:  make install-mcp PREFIX=$(PREFIX)"

install-mcp:
	@command -v npm >/dev/null || { echo "npm required for MCP install"; exit 1; }
	@command -v node >/dev/null || { echo "node required for MCP install"; exit 1; }
	@cd mcp && npm install && npm run build
	@mkdir -p "$(MCPDIR)" "$(BINDIR)"
	@cp -a mcp/package.json mcp/package-lock.json mcp/dist "$(MCPDIR)/"
	@rm -rf "$(MCPDIR)/node_modules"
	@cd "$(MCPDIR)" && npm install --omit=dev --silent
	@printf '%s\n' \
		'#!/usr/bin/env bash' \
		'set -euo pipefail' \
		'exec node "$(MCPDIR)/dist/index.js" "$$@"' \
		> "$(BINDIR)/qemu-connect-mcp"
	@chmod 755 "$(BINDIR)/qemu-connect-mcp"
	@bash scripts/gen-mcp-config.sh --prefix "$(PREFIX)" --root "$(CURDIR)" --out "$(SHAREDIR)/mcp.json"
	@echo ""
	@echo "MCP installed:"
	@echo "  $(BINDIR)/qemu-connect-mcp"
	@echo "  config: $(SHAREDIR)/mcp.json"
	@echo "Point Cursor/Claude MCP settings at that file (or merge its mcpServers entry)."

install-all: install install-mcp

uninstall:
	rm -f "$(BINDIR)/$(CLI_NAME)" "$(BINDIR)/qemu-connect-mcp"
	rm -rf "$(LIBDIR)" "$(SHAREDIR)"
	@echo "Uninstalled from $(PREFIX)"

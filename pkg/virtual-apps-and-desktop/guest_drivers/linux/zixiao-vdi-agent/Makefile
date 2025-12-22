# Zixiao VDI Agent for Linux - Makefile
#
# Copyright (c) 2025 Zixiao System
# SPDX-License-Identifier: Apache-2.0

# Project settings
PROJECT := zixiao-vdi-agent
VERSION := 1.0.0

# Compiler settings
CC := gcc
CFLAGS := -Wall -Wextra -Werror -std=c11 -D_GNU_SOURCE
CFLAGS += -DVERSION=\"$(VERSION)\"

# Debug/Release
ifdef DEBUG
    CFLAGS += -g -O0 -DDEBUG
else
    CFLAGS += -O2 -DNDEBUG
endif

# Directories
SRCDIR := .
OBJDIR := build
BINDIR := bin

# Source files
SRCS := src/main.c \
        src/common.c \
        src/agent.c \
        display/display_capture.c \
        audio/audio_capture.c \
        input/input_handler.c \
        clipboard/clipboard_manager.c \
        spice/spice_agent.c \
        webrtc/webrtc_agent.c

OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# Libraries
LIBS := -lpthread

# X11 support
CFLAGS += $(shell pkg-config --cflags x11 xext xrandr 2>/dev/null)
LIBS += $(shell pkg-config --libs x11 xext xrandr 2>/dev/null)

# PulseAudio support
CFLAGS += $(shell pkg-config --cflags libpulse-simple 2>/dev/null)
LIBS += $(shell pkg-config --libs libpulse-simple 2>/dev/null)

# Output
TARGET := $(BINDIR)/$(PROJECT)

# Default target
all: $(TARGET)

# Create directories
$(OBJDIR) $(BINDIR):
	@mkdir -p $@

# Create subdirectories for objects
$(OBJDIR)/src $(OBJDIR)/display $(OBJDIR)/audio $(OBJDIR)/input $(OBJDIR)/clipboard $(OBJDIR)/spice $(OBJDIR)/webrtc:
	@mkdir -p $@

# Link
$(TARGET): $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@echo "Built: $@"

# Compile
$(OBJDIR)/%.o: %.c | $(OBJDIR)/src $(OBJDIR)/display $(OBJDIR)/audio $(OBJDIR)/input $(OBJDIR)/clipboard $(OBJDIR)/spice $(OBJDIR)/webrtc
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# Include dependencies
-include $(DEPS)

# Clean
clean:
	rm -rf $(OBJDIR) $(BINDIR)

# Install
PREFIX ?= /usr/local
DESTDIR ?=

install: $(TARGET)
	install -D -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(PROJECT)
	install -D -m 644 $(PROJECT).service $(DESTDIR)/etc/systemd/system/$(PROJECT).service
	@echo "Installed to $(DESTDIR)$(PREFIX)/bin/$(PROJECT)"
	@echo "Systemd service installed to $(DESTDIR)/etc/systemd/system/$(PROJECT).service"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROJECT)
	rm -f $(DESTDIR)/etc/systemd/system/$(PROJECT).service

# Systemd
enable:
	systemctl daemon-reload
	systemctl enable $(PROJECT)
	systemctl start $(PROJECT)

disable:
	systemctl stop $(PROJECT)
	systemctl disable $(PROJECT)

# Development helpers
debug: clean
	$(MAKE) DEBUG=1

format:
	find . -name "*.c" -o -name "*.h" | xargs clang-format -i

check:
	cppcheck --enable=all --std=c11 src/ display/ audio/ input/ clipboard/ spice/ webrtc/

# Help
help:
	@echo "Zixiao VDI Agent for Linux"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build the agent (default)"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install to system (requires root)"
	@echo "  uninstall - Remove from system (requires root)"
	@echo "  enable    - Enable and start systemd service"
	@echo "  disable   - Stop and disable systemd service"
	@echo "  debug     - Build with debug symbols"
	@echo "  format    - Format source code"
	@echo "  check     - Run static analysis"
	@echo ""
	@echo "Variables:"
	@echo "  DEBUG=1   - Enable debug build"
	@echo "  PREFIX    - Installation prefix (default: /usr/local)"
	@echo "  DESTDIR   - Staging directory for packaging"

.PHONY: all clean install uninstall enable disable debug format check help

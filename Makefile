.POSIX:
SHELL = /bin/sh

# Compiler settings
CC      ?= cc
PKG_CONFIG  ?= pkg-config

# Installation paths
PREFIX    ?= /usr/local
DATADIR   ?= $(PREFIX)/share
MANDIR    ?= $(DATADIR)/man
WITH_BASH ?= 1

# OS detection
OS := $(shell uname -s)

# Default flags
CFLAGS   += -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual -pedantic
LDFLAGS  +=
LDLIBS   +=

# Find ncurses: pkg-config first (Linux, Nix, MacPorts, Homebrew formulae),
# then Homebrew's keg-only copy, then plain library names.
ifeq ($(shell $(PKG_CONFIG) --exists ncursesw panelw 2>/dev/null && echo 1),1)
    CPPFLAGS += $(shell $(PKG_CONFIG) --cflags ncursesw panelw)
    LDLIBS   += $(shell $(PKG_CONFIG) --libs panelw ncursesw)
else ifeq ($(OS),Darwin)
    BREW_NCURSES := $(shell brew --prefix ncurses 2>/dev/null)
    ifneq ($(BREW_NCURSES),)
        CPPFLAGS += -I$(BREW_NCURSES)/include
        # Use the full paths to the static libraries
        LDLIBS   += $(BREW_NCURSES)/lib/libpanel.a $(BREW_NCURSES)/lib/libncurses.a
    else
        $(warning ncurses not found: install it, or pkg-config, via Homebrew, MacPorts or Nix)
        LDLIBS   += -lpanel -lncurses
    endif
else
    LDLIBS  += -lpanelw -lncursesw -ltinfo
endif
LDLIBS += -lm

# Default target
.DEFAULT_GOAL := all

# Main targets
all: cbonsai

cbonsai: src/cbonsai.c src/msaw.c src/msaw.h
	@echo "Building cbonsai..."
	$(CC) $(CPPFLAGS) $(CFLAGS) src/cbonsai.c src/msaw.c -o $@ $(LDFLAGS) $(LDLIBS)

cbonsai.6: cbonsai.scd
ifeq ($(shell command -v scdoc 2>/dev/null),)
	$(warning Missing dependency: scdoc. The man page will not be generated.)
else
	scdoc <$< >$@
endif

# Function to check if a command exists
check_cmd = $(shell command -v $(1) >/dev/null 2>&1 && echo 1 || echo 0)

# Check for package manager
HAS_APT := $(call check_cmd,apt-get)
HAS_YUM := $(call check_cmd,yum)
HAS_BREW := $(call check_cmd,brew)
HAS_PACMAN := $(call check_cmd,pacman)

# Dependencies setup
deps:
ifeq ($(HAS_APT),1)
	@echo "Installing dependencies using apt..."
	@sudo apt-get update
	@sudo apt-get install -y libncurses-dev pkg-config
else ifeq ($(HAS_YUM),1)
	@echo "Installing dependencies using yum..."
	@sudo yum install -y ncurses-devel pkgconf-pkg-config
else ifeq ($(HAS_PACMAN),1)
	@echo "Installing dependencies using pacman..."
	@sudo pacman -S --needed ncurses pkg-config
else ifeq ($(HAS_BREW),1)
	@echo "Installing dependencies using Homebrew..."
	@brew install ncurses pkg-config
else
	@echo "No supported package manager found. Please install ncurses and pkg-config manually."
	@exit 1
endif

install: cbonsai cbonsai.6
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(MANDIR)/man6
	install -m 0755 cbonsai $(DESTDIR)$(PREFIX)/bin/cbonsai
	[ ! -f cbonsai.6 ] || install -m 0644 cbonsai.6 $(DESTDIR)$(MANDIR)/man6/cbonsai.6
ifeq ($(WITH_BASH),1)
	mkdir -p $(DESTDIR)$(DATADIR)/bash-completion/completions
	install -m 0644 completions/bash/cbonsai.bash $(DESTDIR)$(DATADIR)/bash-completion/completions/cbonsai
endif

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/cbonsai
	rm -f $(DESTDIR)$(MANDIR)/man6/cbonsai.6
	rm -f $(DESTDIR)$(DATADIR)/bash-completion/completions/cbonsai

clean:
	rm -f cbonsai cbonsai.6

# Help target
help:
	@echo "Available targets:"
	@echo "  all       - Build cbonsai (default target)"
	@echo "  deps      - Install required dependencies using system package manager"
	@echo "  install   - Install cbonsai and man pages"
	@echo "  uninstall - Remove cbonsai and man pages"
	@echo "  clean     - Remove built files"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Configuration variables:"
	@echo "  CC        - C compiler (default: cc)"
	@echo "  CFLAGS    - C compiler flags"
	@echo "  PREFIX    - Installation prefix (default: /usr/local)"
	@echo "  WITH_BASH - Install bash completion (default: 1)"

.PHONY: all install uninstall clean help deps
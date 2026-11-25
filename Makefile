# SPDX-License-Identifier: GPL-3.0-or-later
#
# turing-smart-screen - C++ port of turing-smart-screen-python, a system monitor and library for USB-C displays
# https://github.com/mathoudebine/turing-smart-screen-python/
#
# Build system for the C++ port (target platforms: Ubuntu 26.04 LTS, and Windows 11 with MSYS2 UCRT64).
#
#   make              build every program into build/bin/
#   make main         build only the system monitor (src/main.cpp)
#   make configure    build only the configuration GUI (src/configure.cpp)
#   make tools        build only the tools (src/tools/)
#   make test         build and run the unit tests
#   make deps         check that all build dependencies are installed
#   make clean        remove build/
#   make DEBUG=1      build without optimizations, with debug symbols
#
# The platform is detected (PLATFORM=linux or windows); it can be given to cross-compile, e.g.:
#   make PLATFORM=windows CXX=x86_64-w64-mingw32-g++ PKG_CONFIG=x86_64-w64-mingw32-pkg-config
#
# CPPFLAGS, CXXFLAGS, LDFLAGS and LDLIBS given on the command line are added to the project flags, e.g.:
#   make CXXFLAGS=-fsanitize=address LDFLAGS=-fsanitize=address
#
# Build dependencies (Ubuntu packages):
#   build-essential pkg-config libyaml-cpp-dev nlohmann-json3-dev libfreetype-dev libharfbuzz-dev libglib2.0-dev libpng-dev libjpeg-dev
#   libusb-1.0-0-dev libcurl4-openssl-dev libicu-dev libgtkmm-4.0-dev libgtest-dev
#
# Build dependencies (Windows, MSYS2 UCRT64 packages, to build from the "MSYS2 UCRT64" shell):
#   pacman -S make mingw-w64-ucrt-x86_64-{gcc,pkgconf,yaml-cpp,nlohmann-json,freetype,harfbuzz,glib2,libpng,libjpeg-turbo,libusb,
#   curl,icu,gtkmm-4.0,gtest}

ifeq ($(OS),Windows_NT)
  PLATFORM ?= windows
else
  PLATFORM ?= linux
endif

CXX      ?= g++
CXXSTD   := -std=c++20
WARNINGS := -Wall -Wextra -Wpedantic

ifeq ($(DEBUG),1)
  OPTFLAGS := -O0 -g
else
  OPTFLAGS := -O2
endif

SRC_DIR   := src
TEST_DIR  := tests/cpp
BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj
BIN_DIR   := $(BUILD_DIR)/bin

# pkg-config modules, grouped by what links them
LIB_PKGS  := yaml-cpp nlohmann_json freetype2 harfbuzz libpng libjpeg libusb-1.0 libcurl icu-uc icu-i18n gio-2.0
GUI_PKGS  := gtkmm-4.0
TEST_PKGS := gtest gtest_main

PKG_CONFIG ?= pkg-config
# Dependencies headers are included as system headers: their warnings are not reported
LIB_CFLAGS  := $(patsubst -I%,-isystem %,$(shell $(PKG_CONFIG) --cflags $(LIB_PKGS) 2>/dev/null))
LIB_LIBS    := $(shell $(PKG_CONFIG) --libs $(LIB_PKGS) 2>/dev/null)
GUI_CFLAGS  := $(patsubst -I%,-isystem %,$(shell $(PKG_CONFIG) --cflags $(GUI_PKGS) 2>/dev/null))
GUI_LIBS    := $(shell $(PKG_CONFIG) --libs $(GUI_PKGS) 2>/dev/null)
TEST_CFLAGS := $(patsubst -I%,-isystem %,$(shell $(PKG_CONFIG) --cflags $(TEST_PKGS) 2>/dev/null))
TEST_LIBS   := $(shell $(PKG_CONFIG) --libs $(TEST_PKGS) 2>/dev/null)

# Platform-specific settings. Windows: Windows 10 API or later, without the GDI macros of <windows.h> (ERROR...) and
# the min/max macros, with M_PI; system libraries for sockets, network interfaces, serial ports listing and sensors
ifeq ($(PLATFORM),windows)
  EXE           := .exe
  PLATFORM_DEFS := -D_WIN32_WINNT=0x0A00 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DNOGDI -D_USE_MATH_DEFINES
  PLATFORM_LIBS := -lws2_32 -liphlpapi -lsetupapi -lcfgmgr32 -lpdh -lpowrprof
else
  EXE           :=
  PLATFORM_DEFS :=
  PLATFORM_LIBS :=
endif

ALL_CPPFLAGS = -I$(SRC_DIR) -MMD -MP $(PLATFORM_DEFS) $(CPPFLAGS)
ALL_CXXFLAGS = $(CXXSTD) $(WARNINGS) $(OPTFLAGS) $(LIB_CFLAGS) $(CXXFLAGS)
ALL_LDLIBS   = $(LDLIBS) $(LIB_LIBS) $(PLATFORM_LIBS) -pthread

# Shared library code (src/library/), linked into every program
LIB_SRCS := $(shell find $(SRC_DIR)/library -name '*.cpp' 2>/dev/null)
LIB_OBJS := $(LIB_SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
LIB_A    := $(BUILD_DIR)/libturing.a

# Programs: one entry point per application
MAIN_OBJS           := $(OBJ_DIR)/main.o
CONFIGURE_OBJS      := $(OBJ_DIR)/configure.o
THEME_EDITOR_OBJS   := $(OBJ_DIR)/theme-editor.o
SIMPLE_PROGRAM_OBJS := $(OBJ_DIR)/simple-program.o

PROGRAMS := $(BIN_DIR)/main$(EXE) $(BIN_DIR)/configure$(EXE) $(BIN_DIR)/theme-editor$(EXE) $(BIN_DIR)/simple-program$(EXE)

# Tools: one program per source file in src/tools/
TOOL_SRCS := $(wildcard $(SRC_DIR)/tools/*.cpp)
TOOLS     := $(TOOL_SRCS:$(SRC_DIR)/tools/%.cpp=$(BIN_DIR)/%$(EXE))

# Unit tests (tests/cpp/)
TEST_SRCS := $(shell find $(TEST_DIR) -name '*.cpp' 2>/dev/null)
TEST_OBJS := $(TEST_SRCS:$(TEST_DIR)/%.cpp=$(OBJ_DIR)/tests/%.o)
TEST_BIN  := $(BIN_DIR)/unit-tests$(EXE)

.PHONY: all main configure theme-editor simple-program tools test deps clean

all: $(PROGRAMS) $(TOOLS)

tools: $(TOOLS)

main: $(BIN_DIR)/main$(EXE)
configure: $(BIN_DIR)/configure$(EXE)
theme-editor: $(BIN_DIR)/theme-editor$(EXE)
simple-program: $(BIN_DIR)/simple-program$(EXE)

$(LIB_A): $(LIB_OBJS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(BIN_DIR)/main$(EXE): $(MAIN_OBJS) $(LIB_A)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) -o $@ $^ $(ALL_LDLIBS)

$(BIN_DIR)/simple-program$(EXE): $(SIMPLE_PROGRAM_OBJS) $(LIB_A)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) -o $@ $^ $(ALL_LDLIBS)

$(BIN_DIR)/configure$(EXE): $(CONFIGURE_OBJS) $(LIB_A)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) -o $@ $^ $(GUI_LIBS) $(ALL_LDLIBS)

$(BIN_DIR)/theme-editor$(EXE): $(THEME_EDITOR_OBJS) $(LIB_A)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) -o $@ $^ $(GUI_LIBS) $(ALL_LDLIBS)

$(BIN_DIR)/%$(EXE): $(OBJ_DIR)/tools/%.o $(LIB_A)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) -o $@ $^ $(ALL_LDLIBS)

# GUI programs need the gtkmm include paths
$(CONFIGURE_OBJS) $(THEME_EDITOR_OBJS): ALL_CXXFLAGS += $(GUI_CFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c -o $@ $<

# Tests
$(OBJ_DIR)/tests/%.o: $(TEST_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) $(TEST_CFLAGS) -c -o $@ $<

$(TEST_BIN): $(TEST_OBJS) $(LIB_A)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) -o $@ $^ $(TEST_LIBS) $(ALL_LDLIBS)

test:
ifeq ($(strip $(TEST_SRCS)),)
	@echo "No C++ unit tests yet in $(TEST_DIR)/"
else
	@$(MAKE) --no-print-directory $(TEST_BIN)
	$(TEST_BIN)
endif

deps:
	@missing=0; \
	for pkg in $(LIB_PKGS) $(GUI_PKGS) $(TEST_PKGS); do \
		if $(PKG_CONFIG) --exists $$pkg; then \
			printf "  %-12s %s\n" $$pkg "$$($(PKG_CONFIG) --modversion $$pkg)"; \
		else \
			printf "  %-12s MISSING\n" $$pkg; missing=1; \
		fi; \
	done; \
	if [ $$missing -eq 1 ] && [ "$(PLATFORM)" = windows ]; then \
		echo "Install them with: pacman -S make mingw-w64-ucrt-x86_64-{gcc,pkgconf,yaml-cpp,nlohmann-json,freetype,harfbuzz,glib2,\
libpng,libjpeg-turbo,libusb,curl,icu,gtkmm-4.0,gtest}"; \
		exit 1; \
	elif [ $$missing -eq 1 ]; then \
		echo "Install them with: sudo apt install build-essential pkg-config libyaml-cpp-dev nlohmann-json3-dev libfreetype-dev libharfbuzz-dev libglib2.0-dev \
libpng-dev libjpeg-dev libusb-1.0-0-dev libcurl4-openssl-dev libicu-dev libgtkmm-4.0-dev libgtest-dev"; \
		exit 1; \
	fi

clean:
	rm -rf $(BUILD_DIR)

-include $(shell find $(OBJ_DIR) -name '*.d' 2>/dev/null)

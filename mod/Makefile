# Ultramaster KR-106 -- headless LV2 bundle for MOD (mod-host / mod-ui)
#
#   make                 build build/kr106-mod.lv2
#   make install         copy the bundle to $(DESTDIR)$(LV2DIR)  (default ~/.lv2)
#   make validate        run lv2_validate on the bundle (needs lilv-utils)
#   make test            render a few notes through the plugin (needs liblilv-dev)
#   make modgui          regenerate modgui/ from the port table (needs python3 + Pillow)
#
# Cross-compiling (mod-plugin-builder, Raspberry Pi toolchains): set CXX to
# the target compiler. The TTL generator runs on the build machine and is
# compiled with HOST_CXX.

CXX       ?= g++
HOST_CXX  ?= c++
CXXFLAGS  ?= -O3
LDFLAGS   ?=
LV2DIR    ?= $(HOME)/.lv2

BUILD_DIR = build
BUNDLE    = $(BUILD_DIR)/kr106-mod.lv2
BINARY    = kr106_mod.so

DSP_DIR     = ../Source/DSP
PRESETS_DIR = ../Source

# LV2 headers: system package (lv2-dev) if present, else the copy shipped with JUCE
LV2_CFLAGS := $(shell pkg-config --cflags lv2 2>/dev/null)
ifeq ($(shell pkg-config --exists lv2 2>/dev/null && echo yes),)
  LV2_CFLAGS := -I../libs/JUCE/modules/juce_audio_processors_headless/format_types/LV2_SDK/lv2
endif

# Same release flags as the main CMake build: FMA fusion, no errno on math calls
DSP_FLAGS = -std=c++17 -DNDEBUG -ffp-contract=fast -fno-math-errno -fvisibility=hidden \
            -I$(DSP_DIR) -I$(PRESETS_DIR) -Isrc
WARN      = -Wall -Wextra

SRC     = src/kr106_mod.cpp
HEADERS = src/kr106_mod_ports.h $(wildcard $(DSP_DIR)/*.h)

MODGUI_FILES = $(shell find modgui -type f)

.PHONY: all install uninstall validate test modgui clean

all: $(BUNDLE)/$(BINARY) $(BUNDLE)/manifest.ttl $(BUNDLE)/modgui.ttl $(BUILD_DIR)/modgui.stamp

$(BUNDLE):
	mkdir -p $(BUNDLE)/modgui

$(BUNDLE)/$(BINARY): $(SRC) $(HEADERS) | $(BUNDLE)
	$(CXX) $(CXXFLAGS) $(DSP_FLAGS) $(WARN) $(LV2_CFLAGS) -fPIC -shared \
	  -o $@ $(SRC) $(LDFLAGS) -Wl,--no-undefined -Wl,--as-needed -lm

$(BUILD_DIR)/kr106_mod_ttlgen: tools/kr106_mod_ttlgen.cpp $(HEADERS) $(PRESETS_DIR)/KR106_Presets_JUCE.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CXX) -O1 $(DSP_FLAGS) $(WARN) -o $@ $<

$(BUNDLE)/manifest.ttl: $(BUILD_DIR)/kr106_mod_ttlgen | $(BUNDLE)
	$(BUILD_DIR)/kr106_mod_ttlgen $(BUNDLE) $(BINARY)

$(BUNDLE)/modgui.ttl: modgui.ttl | $(BUNDLE)
	cp $< $@

$(BUILD_DIR)/modgui.stamp: $(MODGUI_FILES) | $(BUNDLE)
	rm -rf $(BUNDLE)/modgui
	cp -r modgui $(BUNDLE)/modgui
	@touch $@

$(BUILD_DIR)/ports.json: $(BUILD_DIR)/kr106_mod_ttlgen | $(BUNDLE)
	$(BUILD_DIR)/kr106_mod_ttlgen $(BUNDLE) $(BINARY) $@

modgui: $(BUILD_DIR)/ports.json
	python3 tools/gen_modgui.py $(BUILD_DIR)/ports.json modgui

install: all
	mkdir -p $(DESTDIR)$(LV2DIR)
	rm -rf $(DESTDIR)$(LV2DIR)/kr106-mod.lv2
	cp -r $(BUNDLE) $(DESTDIR)$(LV2DIR)/

uninstall:
	rm -rf $(DESTDIR)$(LV2DIR)/kr106-mod.lv2

validate: all
	lv2_validate $(BUNDLE)/*.ttl

$(BUILD_DIR)/kr106_mod_test: tools/kr106_mod_test.cpp
	$(HOST_CXX) -O2 -std=c++17 $(WARN) -o $@ $< $(shell pkg-config --cflags --libs lilv-0)

test: all $(BUILD_DIR)/kr106_mod_test
	LV2_PATH=$(abspath $(BUILD_DIR)) $(BUILD_DIR)/kr106_mod_test

clean:
	rm -rf $(BUILD_DIR)

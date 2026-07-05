# SPDX-License-Identifier: MIT
# Projection Cinema Engine - Makefile
# Ubuntu Server 22.04 LTS Build

CC := gcc
CXX := g++
CFLAGS := -Wall -Wextra -O2 -fPIC -Imodules
LDFLAGS := -pthread -lm

DECKLINK_ENFORCER_TARGET := decklink_2si_enforcer
DECKLINK_ENFORCER_SOURCE := tools/decklink_2si_enforcer.cpp
DECKLINK_HDR_ANCILLARY_TARGET := decklink_hdr_ancillary_inject
DECKLINK_HDR_ANCILLARY_SOURCE := tools/decklink_hdr_ancillary_inject.cpp
DECKLINK_SDK_INCLUDE ?= /opt/blackmagic/DesktopVideo/include
DECKLINK_SDK_LIB ?= /opt/blackmagic/DesktopVideo/lib
DECKLINK_ENFORCER_CXXFLAGS ?= -Wall -Wextra -O2 -I$(DECKLINK_SDK_INCLUDE)
DECKLINK_ENFORCER_LDFLAGS ?= -L$(DECKLINK_SDK_LIB) -lDeckLinkAPI -lpthread -ldl

# Detect dependency availability up-front so build behavior is explicit:
# - libvlc is hard-required (build aborts),
# - others degrade to warning paths to keep partial environments diagnosable.
HAVE_LIBAVFORMAT := $(shell pkg-config --exists libavformat && echo 1 || echo 0)
HAVE_LIBVLC := $(shell pkg-config --exists libvlc && echo 1 || echo 0)
HAVE_ALSA := $(shell pkg-config --exists alsa && echo 1 || echo 0)
HAVE_LIBCURL := $(shell pkg-config --exists libcurl && echo 1 || echo 0)
HAVE_PAHO := $(shell pkg-config --exists paho-mqtt3c && echo 1 || echo 0)

ifeq ($(HAVE_LIBAVFORMAT),1)
  CFLAGS += $(shell pkg-config --cflags libavformat libavutil)
  LDFLAGS += $(shell pkg-config --libs libavformat libavutil)
else
  $(warning libavformat not found - metadata extraction will fail)
endif

ifeq ($(HAVE_LIBVLC),1)
  CFLAGS += $(shell pkg-config --cflags libvlc)
  LDFLAGS += $(shell pkg-config --libs libvlc)
else
  $(error libvlc is required but not found)
endif

ifeq ($(HAVE_ALSA),1)
  CFLAGS += $(shell pkg-config --cflags alsa)
  LDFLAGS += $(shell pkg-config --libs alsa)
else
  $(warning ALSA not found - audio output will fail)
endif

ifeq ($(HAVE_LIBCURL),1)
  CFLAGS += $(shell pkg-config --cflags libcurl)
  LDFLAGS += $(shell pkg-config --libs libcurl)
else
  $(warning libcurl not found - CalDAV scheduling will be unavailable)
endif

ifeq ($(HAVE_PAHO),1)
	CFLAGS += $(shell pkg-config --cflags paho-mqtt3c) -DPROJECTION_USE_PAHO_MQTT
	LDFLAGS += $(shell pkg-config --libs paho-mqtt3c)
else
	$(warning paho-mqtt3c not found - MQTT will run in fallback non-broker mode)
endif

# Source files
SOURCES := sdi_cinema_engine.c \
           modules/metadata_extractor.c \
           modules/alsa_context.c \
           modules/mqtt_client.c \
           modules/christie_control.c \
           modules/playlist_manager.c \
		   modules/caldav_scheduler.c \
		   modules/dxd16_control.c \
		   modules/decklink_enforcer.c

OBJECTS := $(SOURCES:.c=.o)
TARGET := sdi_cinema_engine

# Default target
all: $(TARGET)

# Optional helper binaries are split into explicit targets so core engine build
# remains possible even when DeckLink SDK paths are not present.
decklink-enforcer: $(DECKLINK_ENFORCER_TARGET)
decklink-hdr-ancillary: $(DECKLINK_HDR_ANCILLARY_TARGET)

$(DECKLINK_ENFORCER_TARGET): $(DECKLINK_ENFORCER_SOURCE)
	@echo "Building $@..."
	$(CXX) $(DECKLINK_ENFORCER_CXXFLAGS) -o $@ $< $(DECKLINK_ENFORCER_LDFLAGS)
	@echo "✓ Build successful: $@"

$(DECKLINK_HDR_ANCILLARY_TARGET): $(DECKLINK_HDR_ANCILLARY_SOURCE)
	@echo "Building $@..."
	$(CXX) $(DECKLINK_ENFORCER_CXXFLAGS) -o $@ $< $(DECKLINK_ENFORCER_LDFLAGS)
	@echo "✓ Build successful: $@"

# Link the executable
$(TARGET): $(OBJECTS)
	@echo "Linking $@..."
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "✓ Build successful: $@"

# Compile object files
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c -o $@ $<

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(OBJECTS) $(TARGET) $(DECKLINK_ENFORCER_TARGET) $(DECKLINK_HDR_ANCILLARY_TARGET)
	@echo "✓ Clean complete"

# Show dependency information
show-deps:
	@echo "=== Dependency Check ==="
	@echo "libvlc: $(HAVE_LIBVLC)"
	@echo "libavformat: $(HAVE_LIBAVFORMAT)"
	@echo "ALSA: $(HAVE_ALSA)"
	@echo "libcurl: $(HAVE_LIBCURL)"
	@echo "paho-mqtt3c: $(HAVE_PAHO)"
	@echo ""
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"

# Install build dependencies (Debian/Ubuntu)
install-deps:
	@echo "Installing Ubuntu build dependencies..."
	sudo apt-get update
	sudo apt-get install -y build-essential pkg-config
	sudo apt-get install -y libvlc-dev libavformat-dev libavutil-dev
	sudo apt-get install -y libasound2-dev libcurl4-openssl-dev libmosquitto-dev
	sudo apt-get install -y libasound2-dev
	@echo "✓ Dependencies installed"

# Build and run with sample test
test-build: clean $(TARGET)
	@echo ""
	@echo "✓ Build successful!"
	@echo ""
	@echo "To run the engine, execute:"
	@echo "  ./$(TARGET) /path/to/media1.mp4 [/path/to/media2.mp4 ...]"
	@echo ""
	@echo "TCP control available on port 8080:"
	@echo "  echo 'PLAY' | nc localhost 8080"
	@echo "  echo 'PAUSE' | nc localhost 8080"
	@echo "  echo 'NEXT' | nc localhost 8080"
	@echo ""
	@echo "Optional DeckLink helper build:"
	@echo "  make decklink-enforcer"
	@echo "  make decklink-hdr-ancillary"

install-decklink-enforcer: $(DECKLINK_ENFORCER_TARGET)
	@echo "Installing $(DECKLINK_ENFORCER_TARGET) to /usr/local/bin..."
	sudo install -m 0755 $(DECKLINK_ENFORCER_TARGET) /usr/local/bin/$(DECKLINK_ENFORCER_TARGET)
	@echo "✓ Installed /usr/local/bin/$(DECKLINK_ENFORCER_TARGET)"

install-decklink-hdr-ancillary: $(DECKLINK_HDR_ANCILLARY_TARGET)
	@echo "Installing $(DECKLINK_HDR_ANCILLARY_TARGET) to /usr/local/bin..."
	sudo install -m 0755 $(DECKLINK_HDR_ANCILLARY_TARGET) /usr/local/bin/$(DECKLINK_HDR_ANCILLARY_TARGET)
	@echo "✓ Installed /usr/local/bin/$(DECKLINK_HDR_ANCILLARY_TARGET)"

.PHONY: all clean show-deps install-deps test-build decklink-enforcer decklink-hdr-ancillary install-decklink-enforcer install-decklink-hdr-ancillary

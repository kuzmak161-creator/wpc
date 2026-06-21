CXX      := g++
CXXFLAGS := $(shell pkg-config --cflags gtk+-3.0 cairo) -std=c++17 -pthread -O2
LIBS     := $(shell pkg-config --libs gtk+-3.0 cairo)
TARGET   := pw-menu
BUILDDIR := build

SOURCES  := $(wildcard *.cpp)
OBJECTS  := $(SOURCES:%.cpp=$(BUILDDIR)/%.o)

ifdef PREFIX
    BINDIR        := $(PREFIX)/bin
    ICONDIR       := $(PREFIX)/share/icons
    APPDIR        := $(PREFIX)/share/applications
    MPV_MPRIS_DIR := $(HOME)/.config/mpv/scripts
else
    PREFIX        := /usr/local
    BINDIR        := $(PREFIX)/bin
    ICONDIR       := $(PREFIX)/share/icons
    APPDIR        := $(PREFIX)/share/applications
    MPV_MPRIS_DIR := $(HOME)/.config/mpv/scripts
endif

.PHONY: all install uninstall clean mpv-mpris ask-mpv-mpris

all: $(BUILDDIR)/$(TARGET)

$(BUILDDIR)/$(TARGET): $(OBJECTS)
	@mkdir -p $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)
	@echo "✓ Build: $@"

$(BUILDDIR)/%.o: %.cpp common.h
	@mkdir -p $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

install: $(BUILDDIR)/$(TARGET)
	@mkdir -p $(BINDIR) $(ICONDIR) $(APPDIR)
	install -Dm755 $(BUILDDIR)/$(TARGET) $(BINDIR)/$(TARGET)
	@echo "✓ Installed to $(BINDIR)/$(TARGET)"
	
	@if [ -f ".photo/logo.png" ]; then \
	    cp .photo/logo.png $(ICONDIR)/pw-menu.png; \
	    echo "✓ Icon installed to $(ICONDIR)/pw-menu.png"; \
	elif [ -f ".photo/logo.svg" ]; then \
	    cp .photo/logo.svg $(ICONDIR)/pw-menu.svg; \
	    echo "✓ Icon installed to $(ICONDIR)/pw-menu.svg"; \
	else \
	    echo "⚠ Icon not found in .photo/"; \
	fi
	
	@echo "[Desktop Entry]" > $(APPDIR)/pw-menu.desktop
	@echo "Name=PW-menu" >> $(APPDIR)/pw-menu.desktop
	@echo "Comment=Widget Player Menu" >> $(APPDIR)/pw-menu.desktop
	@echo "Exec=$(BINDIR)/pw-menu --swim" >> $(APPDIR)/pw-menu.desktop
	@if [ -f "$(ICONDIR)/pw-menu.png" ]; then \
	    echo "Icon=$(ICONDIR)/pw-menu.png" >> $(APPDIR)/pw-menu.desktop; \
	elif [ -f "$(ICONDIR)/pw-menu.svg" ]; then \
	    echo "Icon=$(ICONDIR)/pw-menu.svg" >> $(APPDIR)/pw-menu.desktop; \
	else \
	    echo "Icon=audio-card" >> $(APPDIR)/pw-menu.desktop; \
	fi
	@echo "Terminal=false" >> $(APPDIR)/pw-menu.desktop
	@echo "Type=Application" >> $(APPDIR)/pw-menu.desktop
	@echo "Categories=AudioVideo;Player;" >> $(APPDIR)/pw-menu.desktop
	@echo "StartupNotify=true" >> $(APPDIR)/pw-menu.desktop
	@echo "✓ Desktop entry created at $(APPDIR)/pw-menu.desktop"
	
	@$(MAKE) --no-print-directory ask-mpv-mpris

ask-mpv-mpris:
	@if [ ! -f "$(MPV_MPRIS_DIR)/mpris.so" ]; then \
	    printf "Do you want to build and install mpv-mpris? (Required for MPV support) [y/N]: "; \
	    read answer; \
	    if [ "$$answer" = "y" ] || [ "$$answer" = "Y" ]; then \
	        $(MAKE) --no-print-directory mpv-mpris; \
	    else \
	        echo "Skipped mpv-mpris."; \
	    fi \
	else \
	    echo "✓ mpv-mpris already installed at $(MPV_MPRIS_DIR)/mpris.so"; \
	fi

mpv-mpris:
	@command -v git > /dev/null 2>&1 || { echo "❌ Error: git is required to build mpv-mpris."; exit 1; }
	@pkg-config --exists glib-2.0 > /dev/null 2>&1 || { \
	    echo "❌ Error: glib-2.0 not found! Please install it first."; \
	    exit 1; \
	}
	@rm -rf /tmp/mpv-mpris-build
	@git clone --depth=1 https://github.com/hoyon/mpv-mpris /tmp/mpv-mpris-build
	@cd /tmp/mpv-mpris-build && $(MAKE)
	@mkdir -p $(MPV_MPRIS_DIR)
	@cp /tmp/mpv-mpris-build/mpris.so $(MPV_MPRIS_DIR)/mpris.so
	@rm -rf /tmp/mpv-mpris-build
	@echo "✓ mpv-mpris installed to $(MPV_MPRIS_DIR)/mpris.so"

uninstall:
	rm -f $(BINDIR)/$(TARGET)
	rm -f $(ICONDIR)/pw-menu.png $(ICONDIR)/pw-menu.svg
	rm -f $(APPDIR)/pw-menu.desktop
	@echo "✓ Removed $(TARGET)"

clean:
	rm -rf $(BUILDDIR)
	@echo "✓ Cleaned build directory"

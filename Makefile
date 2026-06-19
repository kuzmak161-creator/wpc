CXX      := g++                                            CXXFLAGS := $(shell pkg-config --cflags gtk+-3.0 cairo) -std=c++17 -pthread -O2
LIBS     := $(shell pkg-config --libs gtk+-3.0 cairo)      TARGET   := pw-menu                                        SRC      := player.cpp
BUILDDIR := build                                                                                                     UNAME := $(shell uname -o 2>/dev/null || uname -s)
                                                           ifeq ($(UNAME), Android)                                       PREFIX := $(PREFIX)/bin
else                                                               PREFIX := /usr/local/bin                           endif
                                                           .PHONY: all install uninstall clean                        
all: $(BUILDDIR)/$(TARGET)                                                                                            $(BUILDDIR)/$(TARGET): $(SRC)
        @mkdir -p $(BUILDDIR)                                      $(CXX) $(CXXFLAGS) -o $(BUILDDIR)/$(TARGET) $(SRC) $(LIBS)
        @echo "✓ build: $(BUILDDIR)/$(TARGET)"                                                                       install: $(BUILDDIR)/$(TARGET)
        @mkdir -p $(PREFIX)                                        install -Dm755 $(BUILDDIR)/$(TARGET) $(PREFIX)/$(TARGET)
        @echo "✓ installed $(PREFIX)/$(TARGET)"                                                                    uninstall:
        rm -f $(PREFIX)/$(TARGET)                                  @echo "✓ Удалён из $(PREFIX)/$(TARGET)"            
clean:                                                             rm -rf $(BUILDDIR)                                         @echo "✓ clean"
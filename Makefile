CXX      := g++
CXXFLAGS := $(shell pkg-config --cflags gtk+-3.0 cairo) -std=c++17 -pthread -O2
LIBS     := $(shell pkg-config --libs gtk+-3.0 cairo)
TARGET   := pw-menu
SRC      := player.cpp
BUILDDIR := build

UNAME := $(shell uname -o 2>/dev/null || uname -s)

ifeq ($(UNAME), Android)
    
    BINDIR := $(PREFIX)/bin
else
    PREFIX := /usr/local
    BINDIR := $(PREFIX)/bin
endif

.PHONY: all install uninstall clean

all: $(BUILDDIR)/$(TARGET)

$(BUILDDIR)/$(TARGET): $(SRC)
	@mkdir -p $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $(BUILDDIR)/$(TARGET) $(SRC) $(LIBS)
	@echo "✓ build: $(BUILDDIR)/$(TARGET)"

install: $(BUILDDIR)/$(TARGET)
	@mkdir -p $(BINDIR)
	install -Dm755 $(BUILDDIR)/$(TARGET) $(BINDIR)/$(TARGET)
	@echo "✓ installed $(BINDIR)/$(TARGET)"

uninstall:
	rm -f $(BINDIR)/$(TARGET)
	@echo "✓ Удалён из $(BINDIR)/$(TARGET)"

clean:
	rm -rf $(BUILDDIR)
	@echo "✓ clean"

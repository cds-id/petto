CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -g
PKGS     = x11 xext xfixes xrender xtst cairo cairo-xlib
VERSION  := $(shell cat VERSION 2>/dev/null || echo 0.0.0)

# rlottie is vendored + statically linked (no apt package exists). Build it
# with packaging/build-rlottie.sh, which populates third_party/rlottie.
RLOTTIE_DIR = third_party/rlottie
RLOTTIE_LIB = $(RLOTTIE_DIR)/librlottie.a

CFLAGS  += $(shell pkg-config --cflags $(PKGS)) -DPETTO_VERSION='"$(VERSION)"' \
           -I$(RLOTTIE_DIR)/include
# static rlottie needs the C++ runtime; -lstdc++ pulls it in for our C build
LDFLAGS += $(RLOTTIE_LIB) $(shell pkg-config --libs $(PKGS)) -lstdc++ -lm -lpthread

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
BIN = petto

all: $(BIN)

$(RLOTTIE_LIB):
	./packaging/build-rlottie.sh

$(BIN): $(RLOTTIE_LIB) $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

src/%.o: src/%.c $(RLOTTIE_LIB)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

PREFIX  ?= /usr
DESTDIR ?=

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/petto
	install -Dm644 packaging/petto.desktop \
		$(DESTDIR)$(PREFIX)/share/applications/petto.desktop
	install -Dm644 assets/rocket.json \
		$(DESTDIR)$(PREFIX)/share/petto/rocket.json

install-strip: install
	strip $(DESTDIR)$(PREFIX)/bin/petto

run: $(BIN)
	./$(BIN)

.PHONY: all clean run install install-strip

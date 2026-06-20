CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -g
PKGS     = x11 xext xfixes xrender xtst cairo cairo-xlib
VERSION  := $(shell cat VERSION 2>/dev/null || echo 0.0.0)
CFLAGS  += $(shell pkg-config --cflags $(PKGS)) -DPETTO_VERSION='"$(VERSION)"'
LDFLAGS += $(shell pkg-config --libs $(PKGS)) -lm

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
BIN = petto

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

PREFIX  ?= /usr
DESTDIR ?=

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/petto
	install -Dm644 packaging/petto.desktop \
		$(DESTDIR)$(PREFIX)/share/applications/petto.desktop

install-strip: install
	strip $(DESTDIR)$(PREFIX)/bin/petto

run: $(BIN)
	./$(BIN)

.PHONY: all clean run install install-strip

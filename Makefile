CXX      ?= g++
CXXFLAGS ?= -O2 -pipe
PKGS     := libpulse wayland-client
CXXFLAGS += -std=c++23 -Wall -Wextra -Ibuild $(shell pkg-config --cflags $(PKGS))
LDLIBS   += $(shell pkg-config --libs $(PKGS))
PREFIX   ?= $(HOME)/.local

PROTO    := protocol/wlr-output-management-unstable-v1.xml
PROTO_H  := build/wlr-output-management-unstable-v1-client-protocol.h
PROTO_C  := build/wlr-output-management-unstable-v1-protocol.c
SRCS     := $(wildcard src/*.cpp)
OBJS     := $(SRCS:src/%.cpp=build/%.o) build/wlr-output-management-protocol.o

build/beamer: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.cpp src/*.hpp $(PROTO_H)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(PROTO_H): $(PROTO)
	@mkdir -p build
	wayland-scanner client-header $< $@

$(PROTO_C): $(PROTO)
	@mkdir -p build
	wayland-scanner private-code $< $@

build/wlr-output-management-protocol.o: $(PROTO_C)
	$(CC) -O2 -c -o $@ $<

build/layout_test: tests/layout_test.cpp build/layout.o build/common.o
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^ $(LDLIBS)

test: build/layout_test
	./build/layout_test

install: build/beamer
	install -Dm755 build/beamer $(DESTDIR)$(PREFIX)/bin/beamer

clean:
	rm -rf build

.PHONY: install clean test

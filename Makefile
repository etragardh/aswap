# aswap - advanced swapper
#
#   make            build ./aswap optimized for this machine
#   make test       check the C++ build against reference/aswap.py
#   make install    symlink ./aswap into PREFIX/bin
#   make uninstall  remove that symlink
#   make clean      remove the binary

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O3 -march=native -flto -DNDEBUG -Wall -Wextra
LDFLAGS  ?= -pthread
PREFIX   ?= /usr/local

BIN := aswap
SRC := aswap.cpp

.PHONY: all test install uninstall clean

all: $(BIN)

$(BIN): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

test: $(BIN)
	./tests/equivalence.sh

install: $(BIN)
	ln -sf $(CURDIR)/$(BIN) $(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(PREFIX)/bin/$(BIN)

clean:
	rm -f $(BIN)

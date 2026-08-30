CC      := gcc
BIN     := pack
BUILD   := build

VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

CFLAGS  := -Wall -Wextra -g -std=gnu11 -DPACK_VERSION='"$(VERSION)"' \
           $(EXTRA_CFLAGS)

LDFLAGS ?=

SRC     := $(wildcard src/*.c)
FMT_SRC := $(wildcard src/*.c src/*.h tests/*.c)
OBJ     := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
LIB_OBJ := $(filter-out $(BUILD)/main.o,$(OBJ))

.PHONY: all clean test unit e2e format check-format

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_units: tests/test_units.c $(LIB_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ $< $(LIB_OBJ) $(LDFLAGS)

unit: $(BUILD)/test_units
	./$(BUILD)/test_units

e2e: $(BIN)
	./tests/run_tests.sh

test: unit e2e

format:
	clang-format -i $(FMT_SRC)

check-format:
	clang-format --dry-run -Werror $(FMT_SRC)

clean:
	rm -rf $(BUILD) $(BIN) tests/tmp

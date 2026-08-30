CC      := gcc
CFLAGS  := -Wall -Wextra -g -std=gnu11
BIN     := pack
BUILD   := build

SRC     := $(wildcard src/*.c)
OBJ     := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
LIB_OBJ := $(filter-out $(BUILD)/main.o,$(OBJ))

.PHONY: all clean test unit e2e

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_units: tests/test_units.c $(LIB_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ $< $(LIB_OBJ)

unit: $(BUILD)/test_units
	./$(BUILD)/test_units

e2e: $(BIN)
	./tests/run_tests.sh

test: unit e2e

clean:
	rm -rf $(BUILD) $(BIN) tests/tmp

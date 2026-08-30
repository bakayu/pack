cc := "gcc"
cflags := "-Wall -Wextra -g -std=gnu11"
bin := "pack"

build:
    mkdir -p build
    for src in src/*.c; do \
        {{ cc }} {{ cflags }} -c "$src" -o "build/$(basename "$src" .c).o"; \
    done
    {{ cc }} {{ cflags }} -o {{ bin }} build/*.o

run *args: build
    ./{{ bin }} {{ args }}

# unit tests link every module except main.o
unit: build
    {{ cc }} {{ cflags }} -Isrc -o build/test_units tests/test_units.c \
        $(ls build/*.o | grep -v '/main.o$')
    ./build/test_units

e2e: build
    ./tests/run_tests.sh

test: unit e2e

clean:
    rm -rf build {{ bin }} tests/tmp

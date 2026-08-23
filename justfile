cc := "gcc"
cflags := "-Wall -Wextra -g"
bin := "pack"

build:
    mkdir -p build
    for src in src/*.c; do \
        {{ cc }} {{ cflags }} -c "$src" -o "build/$(basename "$src" .c).o"; \
    done
    {{ cc }} {{ cflags }} -o {{ bin }} build/*.o

run *args: build
    ./{{ bin }} {{ args }}

clean:
    rm -rf build {{ bin }}

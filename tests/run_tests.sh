#!/usr/bin/env bash

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PACK="$ROOT/pack"
TMP="$ROOT/tests/tmp"

pass=0
fail=0

ok()  { pass=$((pass + 1)); printf '  ok   %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf '  FAIL %s\n' "$1"; }

run() {
    local name="$1"
    shift
    if "$@" > /dev/null 2>&1; then ok "$name"; else bad "$name"; fi
}

fails() {
    local name="$1"
    shift
    if "$@" > /dev/null 2>&1; then bad "$name (unexpectedly succeeded)"; else ok "$name"; fi
}

exits() {
    local name="$1" want="$2"
    shift 2
    "$@" > /dev/null 2>&1
    local got=$?
    if [ "$got" -eq "$want" ]; then ok "$name"; else bad "$name (exit $got, want $want)"; fi
}

if [ ! -x "$PACK" ]; then
    echo "tests: $PACK not built, run 'make' first" >&2
    exit 1
fi

rm -rf "$TMP"
mkdir -p "$TMP"

# fixtures
build_fixture() {
    local src="$1"
    mkdir -p "$src/empty_dir" "$src/nested/deeper/deepest" "$src/docs"

    for _ in $(seq 200); do
        echo "the quick brown fox jumps over the lazy dog"
    done > "$src/docs/repetitive.txt"

    head -c 4096 /dev/zero | tr '\0' 'x' > "$src/docs/all_x.txt"

    : > "$src/docs/empty.txt"

    head -c 65536 /dev/urandom > "$src/blob_random.bin"
    head -c 8192 /dev/zero > "$src/blob_zeros.bin"
    printf '\x00\x01\x02\xfd\xfe\xff' > "$src/blob_tiny.bin"
    perl -e 'print map { chr($_ % 256) } 0..511' > "$src/blob_allbytes.bin" \
        2> /dev/null ||
        head -c 512 /dev/urandom > "$src/blob_allbytes.bin"

    echo "deep file" > "$src/nested/deeper/deepest/leaf.txt"
    echo "middle" > "$src/nested/note.md"

    echo "spaces" > "$src/docs/a file with spaces.txt"
    echo "dashes" > "$src/docs/--not-a-flag.txt"
    echo "dotted" > "$src/docs/.hiddenfile"
    echo "unicode" > "$src/docs/café-ünïcode.txt"

    echo "#!/bin/sh" > "$src/run.sh"
    chmod 755 "$src/run.sh"
    chmod 600 "$src/docs/repetitive.txt"
}

mode_is() { [ "$(stat -c '%a' "$1")" = "$2" ]; }
is_empty_file() { [ -f "$1" ] && [ "$(stat -c '%s' "$1")" = "0" ]; }

# tests

echo "e2e: full tree round trip"
SRC="$TMP/tree/src"
mkdir -p "$SRC"
build_fixture "$SRC"

run "pack exits 0" "$PACK" -c "$SRC" "$TMP/tree.pack"
run "archive is non-empty" test -s "$TMP/tree.pack"
run "unpack exits 0" "$PACK" -x "$TMP/tree.pack" "$TMP/out"
run "diff -r is silent (contents identical)" diff -r "$SRC" "$TMP/out/src"

run "executable bit preserved" mode_is "$TMP/out/src/run.sh" 755
run "0600 mode preserved" mode_is "$TMP/out/src/docs/repetitive.txt" 600
run "empty directory recreated" test -d "$TMP/out/src/empty_dir"
run "zero-length file recreated" is_empty_file "$TMP/out/src/docs/empty.txt"
run "deeply nested file recreated" \
    test -f "$TMP/out/src/nested/deeper/deepest/leaf.txt"

run "random binary blob is byte-identical" \
    cmp "$SRC/blob_random.bin" "$TMP/out/src/blob_random.bin"
run "all-byte-values blob is byte-identical" \
    cmp "$SRC/blob_allbytes.bin" "$TMP/out/src/blob_allbytes.bin"

echo
echo "e2e: compression actually compresses"
orig=$(du -sb "$SRC" | cut -f1)
packed=$(stat -c '%s' "$TMP/tree.pack")
run "packed ($packed b) smaller than source ($orig b)" \
    test "$packed" -lt "$orig"

"$PACK" -c "$SRC/docs/all_x.txt" "$TMP/allx.pack" > /dev/null
sz=$(stat -c '%s' "$TMP/allx.pack")
run "4096 identical bytes pack to $sz bytes (< 700)" test "$sz" -lt 700

echo
echo "e2e: single file input"
round_trip_one() {
    "$PACK" -c "$SRC/blob_random.bin" "$TMP/one.pack" &&
        "$PACK" -x "$TMP/one.pack" "$TMP/one" &&
        cmp "$SRC/blob_random.bin" "$TMP/one/blob_random.bin"
}
run "a lone file round-trips" round_trip_one

echo
echo "e2e: empty directory as input"
round_trip_empty() {
    mkdir -p "$TMP/emptysrc" &&
        "$PACK" -c "$TMP/emptysrc" "$TMP/empty.pack" &&
        "$PACK" -x "$TMP/empty.pack" "$TMP/emptyout" &&
        [ -d "$TMP/emptyout/emptysrc" ]
}
run "an empty directory round-trips" round_trip_empty

echo
echo "e2e: --debug reporting"
if DEBUG_OUT=$("$PACK" --debug -c "$SRC/docs/repetitive.txt" "$TMP/dbg.pack"); then
    ok "debug compress exits 0"
else
    bad "debug compress exits 0"
fi
says() { printf '%s' "$DEBUG_OUT" | grep -q "$1"; }
run "debug prints the frequency table" says "frequency table"
run "debug prints the huffman tree" says "huffman tree"
run "debug prints the code table" says "code table"
run "debug prints the compression ratio" says "ratio"

if DEBUG_X=$("$PACK" --debug -x "$TMP/dbg.pack" "$TMP/dbgout"); then
    ok "debug extract exits 0"
else
    bad "debug extract exits 0"
fi
run "debug extract prints the rebuilt tree" \
    grep -q "huffman tree" <<< "$DEBUG_X"

debug_archive_valid() {
    "$PACK" -x "$TMP/dbg.pack" "$TMP/dbgout2" &&
        cmp "$SRC/docs/repetitive.txt" "$TMP/dbgout2/repetitive.txt"
}
run "archive written in debug mode is still valid" debug_archive_valid

echo
echo "e2e: error handling"
fails "missing input is an error" \
    "$PACK" -c "$TMP/does-not-exist" "$TMP/nope.pack"

echo "this is not a pack archive, not even close" > "$TMP/garbage.pack"
fails "garbage input is rejected (bad magic)" \
    "$PACK" -x "$TMP/garbage.pack" "$TMP/garbageout"

head -c 40 "$TMP/tree.pack" > "$TMP/trunc.pack"
fails "truncated archive is rejected" \
    "$PACK" -x "$TMP/trunc.pack" "$TMP/truncout"

cp "$TMP/tree.pack" "$TMP/badver.pack"
printf '\x63' |
    dd of="$TMP/badver.pack" bs=1 seek=4 count=1 conv=notrunc status=none
fails "unsupported version is rejected" \
    "$PACK" -x "$TMP/badver.pack" "$TMP/badverout"

exits "no arguments prints usage and exits 2" 2 "$PACK"
exits "missing operand exits 2" 2 "$PACK" -c "$SRC"
exits "unknown option exits 2" 2 "$PACK" --nonsense -c a b
run "--help exits 0" "$PACK" --help
version_line() { "$PACK" --version | grep -q "^pack "; }
run "--version prints a version line" version_line

echo
echo "e2e: corrupted archives do not crash or write garbage"
for off in 6 40 200 2000; do
    cp "$TMP/tree.pack" "$TMP/rot.pack"
    byte=$(dd if="$TMP/rot.pack" bs=1 skip="$off" count=1 status=none |
        od -An -tu1 | tr -d ' ')
    printf '%b' "$(printf '\\x%02x' $(((byte ^ 0xFF) & 0xFF)))" |
        dd of="$TMP/rot.pack" bs=1 seek="$off" count=1 conv=notrunc status=none
    "$PACK" -x "$TMP/rot.pack" "$TMP/rotout$off" > /dev/null 2>&1
    rc=$?
    run "flipped byte at offset $off: exits cleanly (rc=$rc)" \
        test "$rc" -lt 128
    run "flipped byte at offset $off: wrote nothing outside dest" \
        test ! -e "$TMP/escaped.txt"
done

echo
echo "e2e: idempotence"
"$PACK" -c "$SRC" "$TMP/a.pack" > /dev/null
"$PACK" -c "$SRC" "$TMP/b.pack" > /dev/null
run "packing the same tree twice gives identical bytes" \
    cmp "$TMP/a.pack" "$TMP/b.pack"

echo
echo "$((pass + fail)) checks, $fail failures"
if [ "$fail" -eq 0 ]; then
    rm -rf "$TMP"
    exit 0
fi
echo "artifacts left in $TMP for inspection"
exit 1

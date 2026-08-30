#!/usr/bin/env bash

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PACK="$ROOT/pack"
TMP="$ROOT/tests/tmp"

pass=0
fail=0

ok()   { pass=$((pass + 1)); printf '  ok   %s\n' "$1"; }
bad()  { fail=$((fail + 1)); printf '  FAIL %s\n' "$1"; }
check() { if [ "$2" -eq 0 ]; then ok "$1"; else bad "$1"; fi; }

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
    head -c 8192  /dev/zero    > "$src/blob_zeros.bin"
    printf '\x00\x01\x02\xfd\xfe\xff' > "$src/blob_tiny.bin"
    perl -e 'print map { chr($_ % 256) } 0..511' > "$src/blob_allbytes.bin" \
        2>/dev/null || \
        head -c 512 /dev/urandom > "$src/blob_allbytes.bin"

    echo "deep file" > "$src/nested/deeper/deepest/leaf.txt"
    echo "middle"    > "$src/nested/note.md"

    echo "spaces"  > "$src/docs/a file with spaces.txt"
    echo "dashes"  > "$src/docs/--not-a-flag.txt"
    echo "dotted"  > "$src/docs/.hiddenfile"
    echo "unicode" > "$src/docs/café-ünïcode.txt"

    echo "#!/bin/sh" > "$src/run.sh"
    chmod 755 "$src/run.sh"
    chmod 600 "$src/docs/repetitive.txt"
}

# tests

echo "e2e: full tree round trip"
SRC="$TMP/tree/src"
mkdir -p "$SRC"
build_fixture "$SRC"

"$PACK" -c "$SRC" "$TMP/tree.pack" > /dev/null
check "pack exits 0" $?

[ -s "$TMP/tree.pack" ]
check "archive is non-empty" $?

"$PACK" -x "$TMP/tree.pack" "$TMP/out" > /dev/null
check "unpack exits 0" $?

diff -r "$SRC" "$TMP/out/src" > /dev/null 2>&1
check "diff -r is silent (contents identical)" $?

# permission bits
[ "$(stat -c '%a' "$TMP/out/src/run.sh")" = "755" ]
check "executable bit preserved" $?
[ "$(stat -c '%a' "$TMP/out/src/docs/repetitive.txt")" = "600" ]
check "0600 mode preserved" $?

[ -d "$TMP/out/src/empty_dir" ]
check "empty directory recreated" $?

[ -f "$TMP/out/src/docs/empty.txt" ] &&
    [ "$(stat -c '%s' "$TMP/out/src/docs/empty.txt")" = "0" ]
check "zero-length file recreated" $?

[ -f "$TMP/out/src/nested/deeper/deepest/leaf.txt" ]
check "deeply nested file recreated" $?

cmp -s "$SRC/blob_random.bin" "$TMP/out/src/blob_random.bin"
check "random binary blob is byte-identical" $?
cmp -s "$SRC/blob_allbytes.bin" "$TMP/out/src/blob_allbytes.bin"
check "all-byte-values blob is byte-identical" $?

echo
echo "e2e: compression working"
orig=$(du -sb "$SRC" | cut -f1)
packed=$(stat -c '%s' "$TMP/tree.pack")
[ "$packed" -lt "$orig" ]
check "packed ($packed b) smaller than source ($orig b)"  $?

# A file of one repeated byte
"$PACK" -c "$SRC/docs/all_x.txt" "$TMP/allx.pack" > /dev/null
sz=$(stat -c '%s' "$TMP/allx.pack")
[ "$sz" -lt 700 ]
check "4096 identical bytes pack to $sz bytes (< 700)" $?

echo
echo "e2e: single file input"
"$PACK" -c "$SRC/blob_random.bin" "$TMP/one.pack" > /dev/null &&
    "$PACK" -x "$TMP/one.pack" "$TMP/one" > /dev/null &&
    cmp -s "$SRC/blob_random.bin" "$TMP/one/blob_random.bin"
check "a lone file round-trips" $?

echo
echo "e2e: empty directory as input"
mkdir -p "$TMP/emptysrc"
"$PACK" -c "$TMP/emptysrc" "$TMP/empty.pack" > /dev/null &&
    "$PACK" -x "$TMP/empty.pack" "$TMP/emptyout" > /dev/null &&
    [ -d "$TMP/emptyout/emptysrc" ]
check "an empty directory round-trips" $?

echo
echo "e2e: --debug reporting"
DEBUG_OUT=$("$PACK" --debug -c "$SRC/docs/repetitive.txt" "$TMP/dbg.pack")
check "debug compress exits 0" $?
echo "$DEBUG_OUT" | grep -q "frequency table"
check "debug prints the frequency table" $?
echo "$DEBUG_OUT" | grep -q "huffman tree"
check "debug prints the huffman tree" $?
echo "$DEBUG_OUT" | grep -q "code table"
check "debug prints the code table" $?
echo "$DEBUG_OUT" | grep -q "ratio"
check "debug prints the compression ratio" $?

DEBUG_X=$("$PACK" --debug -x "$TMP/dbg.pack" "$TMP/dbgout")
check "debug extract exits 0" $?
echo "$DEBUG_X" | grep -q "huffman tree"
check "debug extract prints the rebuilt tree" $?

"$PACK" -x "$TMP/dbg.pack" "$TMP/dbgout2" > /dev/null &&
    cmp -s "$SRC/docs/repetitive.txt" \
        "$TMP/dbgout2/repetitive.txt"
check "archive written in debug mode is still valid" $?

echo
echo "e2e: error handling"
"$PACK" -c "$TMP/does-not-exist" "$TMP/nope.pack" > /dev/null 2>&1
[ $? -ne 0 ]
check "missing input is an error" $?

echo "this is not a pack archive, not even close" > "$TMP/garbage.pack"
"$PACK" -x "$TMP/garbage.pack" "$TMP/garbageout" > /dev/null 2>&1
[ $? -ne 0 ]
check "garbage input is rejected (bad magic)" $?

head -c 40 "$TMP/tree.pack" > "$TMP/trunc.pack"
"$PACK" -x "$TMP/trunc.pack" "$TMP/truncout" > /dev/null 2>&1
[ $? -ne 0 ]
check "truncated archive is rejected" $?

cp "$TMP/tree.pack" "$TMP/badver.pack"
printf '\x63' | dd of="$TMP/badver.pack" bs=1 seek=4 count=1 conv=notrunc \
    status=none
"$PACK" -x "$TMP/badver.pack" "$TMP/badverout" > /dev/null 2>&1
[ $? -ne 0 ]
check "unsupported version is rejected" $?

"$PACK" > /dev/null 2>&1
[ $? -eq 2 ]
check "no arguments prints usage and exits 2" $?

"$PACK" -c "$SRC" > /dev/null 2>&1
[ $? -eq 2 ]
check "missing operand exits 2" $?

"$PACK" --nonsense -c a b > /dev/null 2>&1
[ $? -eq 2 ]
check "unknown option exits 2" $?

"$PACK" --help > /dev/null 2>&1
check "--help exits 0" $?

echo
echo "e2e: corrupted archives do not crash or write garbage"
for off in 6 40 200 2000; do
    cp "$TMP/tree.pack" "$TMP/rot.pack"
    orig_byte=$(dd if="$TMP/rot.pack" bs=1 skip="$off" count=1 status=none |
        od -An -tu1 | tr -d ' ')
    printf "\\x$(printf '%02x' $(( (orig_byte ^ 0xFF) & 0xFF )))" |
        dd of="$TMP/rot.pack" bs=1 seek="$off" count=1 conv=notrunc status=none
    "$PACK" -x "$TMP/rot.pack" "$TMP/rotout$off" > /dev/null 2>&1
    rc=$?
    [ "$rc" -lt 128 ]
    check "flipped byte at offset $off: exits cleanly (rc=$rc)" $?
    [ ! -e "$TMP/escaped.txt" ]
    check "flipped byte at offset $off: wrote nothing outside dest" $?
done

echo
echo "e2e: idempotence"
"$PACK" -c "$SRC" "$TMP/a.pack" > /dev/null
"$PACK" -c "$SRC" "$TMP/b.pack" > /dev/null
cmp -s "$TMP/a.pack" "$TMP/b.pack"
check "packing the same tree twice gives identical bytes" $?

echo
echo "$((pass + fail)) checks, $fail failures"
if [ "$fail" -eq 0 ]; then
    rm -rf "$TMP"
    exit 0
fi
echo "artifacts left in $TMP for inspection"
exit 1

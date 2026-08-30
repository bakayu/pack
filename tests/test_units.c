#include "archive.h"
#include "bitio.h"
#include "huffman.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                      \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

static FILE *tmp(void) {
    FILE *f = tmpfile();
    if (f == NULL) {
        perror("tmpfile");
        exit(1);
    }
    return f;
}

static void rewind_stream(FILE *f) {
    fflush(f);
    fseek(f, 0, SEEK_SET);
}

static void test_bitio_single_bits(void) {
    printf("bitio: single bits\n");
    const int pattern[] = {1, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 1, 0};
    const int n = (int)(sizeof(pattern) / sizeof(pattern[0]));

    FILE *f = tmp();
    BitWriter w;
    bw_init(&w, f);
    for (int i = 0; i < n; i++)
        bw_write_bit(&w, pattern[i]);
    bw_flush(&w);

    rewind_stream(f);
    fseek(f, 0, SEEK_END);
    CHECK(ftell(f) == 2, "13 bits should occupy 2 bytes, got %ld", ftell(f));
    rewind_stream(f);

    BitReader r;
    br_init(&r, f);
    for (int i = 0; i < n; i++) {
        int bit = br_read_bit(&r);
        CHECK(bit == pattern[i], "bit %d: expected %d, got %d", i, pattern[i],
              bit);
    }
    fclose(f);
}

static void test_bitio_multibit(void) {
    printf("bitio: multi-bit fields\n");
    struct {
        uint64_t value;
        int width;
    } fields[] = {{0x5A, 8},
                  {0x3, 2},
                  {0, 1},
                  {0xDEADBEEF, 32},
                  {0x1FF, 9},
                  {1, 1},
                  {0xFFFFFFFFFFFFFFFFull, 64}};
    const int n = (int)(sizeof(fields) / sizeof(fields[0]));

    FILE *f = tmp();
    BitWriter w;
    bw_init(&w, f);
    for (int i = 0; i < n; i++)
        bw_write_bits(&w, fields[i].value, fields[i].width);
    bw_flush(&w);
    rewind_stream(f);

    BitReader r;
    br_init(&r, f);
    for (int i = 0; i < n; i++) {
        uint64_t got = 0;
        CHECK(br_read_bits(&r, fields[i].width, &got) == 0,
              "field %d: stream ended early", i);
        CHECK(got == fields[i].value, "field %d: expected %llu, got %llu", i,
              (unsigned long long)fields[i].value, (unsigned long long)got);
    }
    fclose(f);
}

static void test_bitio_eof(void) {
    printf("bitio: reading past the end reports EOF\n");
    FILE *f = tmp();
    BitWriter w;
    bw_init(&w, f);
    bw_write_bits(&w, 0xFF, 8);
    bw_flush(&w);
    rewind_stream(f);

    BitReader r;
    br_init(&r, f);
    for (int i = 0; i < 8; i++)
        CHECK(br_read_bit(&r) == 1, "bit %d should be 1", i);
    CHECK(br_read_bit(&r) == -1, "9th bit should report EOF");

    uint64_t sink;
    CHECK(br_read_bits(&r, 4, &sink) == -1, "read past EOF should fail");
    fclose(f);
}

static long huffman_round_trip(const unsigned char *data, size_t len,
                               const char *label) {
    FILE *in = tmp();
    if (len > 0 && fwrite(data, 1, len, in) != len) {
        printf("  FAIL could not stage input for %s\n", label);
        failures++;
        fclose(in);
        return -1;
    }
    rewind_stream(in);

    FILE *packed = tmp();
    CHECK(huffman_compress(in, packed, 0) == 0, "%s: compress failed", label);
    fclose(in);

    fflush(packed);
    fseek(packed, 0, SEEK_END);
    long packed_size = ftell(packed);
    rewind_stream(packed);

    FILE *out = tmp();
    CHECK(huffman_decompress(packed, out, 0) == 0, "%s: decompress failed",
          label);
    fclose(packed);

    fflush(out);
    fseek(out, 0, SEEK_END);
    long out_size = ftell(out);
    rewind_stream(out);

    CHECK(out_size == (long)len, "%s: expected %zu bytes back, got %ld", label,
          len, out_size);

    if (out_size == (long)len && len > 0) {
        unsigned char *got = malloc(len);
        size_t n = fread(got, 1, len, out);
        CHECK(n == len, "%s: short read of decoded output", label);
        CHECK(memcmp(got, data, len) == 0, "%s: decoded bytes differ", label);
        free(got);
    }
    fclose(out);
    return packed_size;
}

static void test_huffman_empty(void) {
    printf("huffman: empty input\n");
    huffman_round_trip((const unsigned char *)"", 0, "empty");
}

static void test_huffman_single_symbol(void) {
    printf("huffman: one distinct byte (degenerate one-node tree)\n");
    unsigned char data[1000];
    memset(data, 'x', sizeof(data));
    long packed = huffman_round_trip(data, sizeof(data), "single symbol");
    CHECK(packed > 0 && packed < 200, "1000 identical bytes packed to %ld",
          packed);
}

static void test_huffman_two_symbols(void) {
    printf("huffman: two distinct bytes\n");
    unsigned char data[512];
    for (size_t i = 0; i < sizeof(data); i++)
        data[i] = (i % 3 == 0) ? 'a' : 'b';
    huffman_round_trip(data, sizeof(data), "two symbols");
}

static void test_huffman_text(void) {
    printf("huffman: skewed text actually shrinks\n");
    const char *unit = "the quick brown fox jumps over the lazy dog. ";
    size_t ulen = strlen(unit);
    size_t len = ulen * 200;
    unsigned char *data = malloc(len);
    for (size_t i = 0; i < len; i++)
        data[i] = (unsigned char)unit[i % ulen];

    long packed = huffman_round_trip(data, len, "text");
    CHECK(packed > 0 && packed < (long)len,
          "text should shrink: %zu -> %ld bytes", len, packed);
    free(data);
}

static void test_huffman_all_bytes(void) {
    printf("huffman: every byte value present, uniform distribution\n");
    unsigned char data[256 * 4];
    for (size_t i = 0; i < sizeof(data); i++)
        data[i] = (unsigned char)(i % 256);
    huffman_round_trip(data, sizeof(data), "all 256 byte values");
}

static void test_huffman_binary_blob(void) {
    printf("huffman: pseudo-random binary blob (nul bytes and high bits)\n");
    size_t len = 64 * 1024;
    unsigned char *data = malloc(len);
    uint32_t state = 0x1234567u;
    for (size_t i = 0; i < len; i++) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        data[i] = (unsigned char)((state & 0xFF) & (state >> 8 & 0xFF));
    }
    huffman_round_trip(data, len, "binary blob");
    free(data);
}

static void test_huffman_code_table(void) {
    printf("huffman: rarer bytes get codes at least as long as common ones\n");
    uint64_t freq[256] = {0};
    freq['a'] = 100;
    freq['b'] = 50;
    freq['c'] = 20;
    freq['d'] = 1;

    Node *root = buildTree(freq);
    CHECK(root != NULL, "buildTree returned NULL for a non-empty table");
    CHECK(root->freq == 171, "root freq should be 171, got %llu",
          (unsigned long long)root->freq);

    Code table[256];
    buildCodeTable(root, table);

    CHECK(table['a'].length > 0 && table['d'].length > 0,
          "all present bytes need a code");
    CHECK(table['a'].length <= table['b'].length, "'a' should be no longer "
                                                  "than 'b'");
    CHECK(table['b'].length <= table['c'].length, "'b' should be no longer "
                                                  "than 'c'");
    CHECK(table['c'].length <= table['d'].length, "'c' should be no longer "
                                                  "than 'd'");
    CHECK(table['z'].length == 0, "absent bytes must have no code");

    double kraft = 0.0;
    for (int i = 0; i < 256; i++)
        if (table[i].length > 0)
            kraft += 1.0 / (double)(1u << table[i].length);
    CHECK(kraft > 0.999 && kraft < 1.001, "Kraft sum should be 1, got %f",
          kraft);

    freeTree(root);
}

static void test_huffman_empty_tree(void) {
    printf("huffman: buildTree on an all-zero table returns NULL\n");
    uint64_t freq[256] = {0};
    CHECK(buildTree(freq) == NULL, "empty frequency table should give NULL");
}

static void test_huffman_truncated_stream(void) {
    printf("huffman: a truncated stream is rejected, not decoded\n");
    unsigned char data[300];
    for (size_t i = 0; i < sizeof(data); i++)
        data[i] = (unsigned char)(i * 7);

    FILE *in = tmp();
    fwrite(data, 1, sizeof(data), in);
    rewind_stream(in);

    FILE *packed = tmp();
    huffman_compress(in, packed, 0);
    fclose(in);

    fflush(packed);
    fseek(packed, 0, SEEK_END);
    long size = ftell(packed);

    rewind_stream(packed);
    FILE *cut = tmp();
    for (long i = 0; i < size - 4; i++)
        fputc(fgetc(packed), cut);
    fclose(packed);
    rewind_stream(cut);

    FILE *out = tmp();
    CHECK(huffman_decompress(cut, out, 0) == -1,
          "truncated stream should be rejected");
    fclose(cut);
    fclose(out);
}

static void write_entry(FILE *f, uint8_t type, uint32_t mode, const char *path,
                        const char *payload) {
    size_t plen = strlen(path);
    size_t dlen = payload ? strlen(payload) : 0;
    write_u8(f, type);
    write_u32(f, mode);
    write_u16(f, (uint16_t)plen);
    fwrite(path, 1, plen, f);
    write_u64(f, (uint64_t)dlen);
    if (dlen)
        fwrite(payload, 1, dlen, f);
}

static char *make_dest(void) {
    char *dir = strdup("/tmp/pack_test_XXXXXX");
    if (dir == NULL || mkdtemp(dir) == NULL) {
        perror("mkdtemp");
        exit(1);
    }
    return dir;
}

static void test_extract_rejects_zip_slip(void) {
    printf("archive: extraction refuses paths that escape the destination\n");
    const char *escapes[] = {"../escaped.txt", "a/../../escaped.txt",
                             "/tmp/pack_absolute_escape.txt", ".."};

    for (size_t i = 0; i < sizeof(escapes) / sizeof(escapes[0]); i++) {
        FILE *stream = tmp();
        write_entry(stream, 1, 0644, escapes[i], "pwned");
        rewind_stream(stream);

        char *dest = make_dest();
        char victim[512];
        snprintf(victim, sizeof(victim), "%s/../escaped.txt", dest);

        CHECK(archive_extract(stream, dest, 0) == -1,
              "\"%s\" should be rejected", escapes[i]);
        CHECK(fopen(victim, "rb") == NULL,
              "\"%s\" created a file outside "
              "the destination",
              escapes[i]);
        CHECK(fopen("/tmp/pack_absolute_escape.txt", "rb") == NULL,
              "\"%s\" wrote to an absolute path", escapes[i]);

        fclose(stream);
        rmdir(dest);
        free(dest);
    }
}

static void test_extract_rejects_bad_type(void) {
    printf("archive: an unknown entry type is rejected\n");
    FILE *stream = tmp();
    write_entry(stream, 9, 0644, "ok.txt", "data");
    rewind_stream(stream);

    char *dest = make_dest();
    CHECK(archive_extract(stream, dest, 0) == -1,
          "entry type 9 should be rejected");
    fclose(stream);
    rmdir(dest);
    free(dest);
}

static void test_extract_truncated_entry(void) {
    printf("archive: a stream cut mid-header is rejected\n");
    FILE *stream = tmp();
    write_u8(stream, 1);
    write_u32(stream, 0644);
    rewind_stream(stream);

    char *dest = make_dest();
    CHECK(archive_extract(stream, dest, 0) == -1,
          "a truncated entry header should be rejected");
    fclose(stream);
    rmdir(dest);
    free(dest);
}

static void test_path_safety(void) {
    printf("util: path validation rejects escapes\n");
    const char *safe[] = {"a",        "a/b",     "a/b/c.txt",
                          "..hidden", "a/..b/c", "dir.tar.gz"};
    const char *unsafe[] = {"",     "/etc/passwd", "..",   "../a",
                            "a/..", "a/../../b",   "a//b", "/"};

    for (size_t i = 0; i < sizeof(safe) / sizeof(safe[0]); i++)
        CHECK(path_is_safe(safe[i]) == 1, "\"%s\" should be safe", safe[i]);
    for (size_t i = 0; i < sizeof(unsafe) / sizeof(unsafe[0]); i++)
        CHECK(path_is_safe(unsafe[i]) == 0, "\"%s\" should be rejected",
              unsafe[i]);
    CHECK(path_is_safe(NULL) == 0, "NULL should be rejected");
}

static void test_path_helpers(void) {
    printf("util: join_path and base_name\n");
    struct {
        const char *a, *b, *want;
    } joins[] = {
        {"dir", "file", "dir/file"},    {"dir/", "file", "dir/file"},
        {"dir///", "file", "dir/file"}, {".", "file", "./file"},
        {"/", "file", "/file"},
    };
    for (size_t i = 0; i < sizeof(joins) / sizeof(joins[0]); i++) {
        char *got = join_path(joins[i].a, joins[i].b);
        CHECK(strcmp(got, joins[i].want) == 0,
              "join_path(\"%s\", \"%s\") = \"%s\", want \"%s\"", joins[i].a,
              joins[i].b, got, joins[i].want);
        free(got);
    }

    struct {
        const char *in, *want;
    } bases[] = {
        {"foo", "foo"},        {"a/b/foo", "foo"}, {"a/b/foo/", "foo"},
        {"a/b/foo///", "foo"}, {"/", "root"},      {"", "root"},
    };
    for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
        char *got = base_name(bases[i].in);
        CHECK(strcmp(got, bases[i].want) == 0,
              "base_name(\"%s\") = \"%s\", want \"%s\"", bases[i].in, got,
              bases[i].want);
        free(got);
    }
}

static void test_endian_io(void) {
    printf("util: big-endian fixed-width fields round-trip\n");
    FILE *f = tmp();
    write_u8(f, 0xAB);
    write_u16(f, 0xBEEF);
    write_u32(f, 0xDEADBEEF);
    write_u64(f, 0x0123456789ABCDEFull);
    rewind_stream(f);

    uint8_t a;
    uint16_t b;
    uint32_t c;
    uint64_t d;
    CHECK(read_u8(f, &a) == 0 && a == 0xAB, "u8 round trip");
    CHECK(read_u16(f, &b) == 0 && b == 0xBEEF, "u16 round trip");
    CHECK(read_u32(f, &c) == 0 && c == 0xDEADBEEF, "u32 round trip");
    CHECK(read_u64(f, &d) == 0 && d == 0x0123456789ABCDEFull, "u64 round trip");
    CHECK(read_u8(f, &a) == -1, "read past the end should report EOF");

    rewind_stream(f);
    fseek(f, 1, SEEK_SET);
    CHECK(fgetc(f) == 0xBE, "u16 must be written big-endian");
    fclose(f);
}

int main(void) {
    test_bitio_single_bits();
    test_bitio_multibit();
    test_bitio_eof();

    test_huffman_empty();
    test_huffman_single_symbol();
    test_huffman_two_symbols();
    test_huffman_text();
    test_huffman_all_bytes();
    test_huffman_binary_blob();
    test_huffman_code_table();
    test_huffman_empty_tree();
    test_huffman_truncated_stream();

    test_extract_rejects_zip_slip();
    test_extract_rejects_bad_type();
    test_extract_truncated_entry();

    test_path_safety();
    test_path_helpers();
    test_endian_io();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

#include "archive.h"
#include "huffman.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

#define MAGIC "PACK"
#define MAGIC_LEN 4
#define FORMAT_VERSION 1

static void usage(FILE *f) {
    fprintf(f, "usage:\n"
               "  pack -c <input> <archive.pack>   archive and compress a file "
               "or directory\n"
               "  pack -x <archive.pack> <dest>    decompress and extract into "
               "dest\n"
               "\n"
               "options:\n"
               "  --debug    print the frequency table, the huffman tree, the "
               "code table,\n"
               "             the archive entries and the compression ratio\n"
               "  -h, --help show this message\n");
}

static void write_magic(FILE *out) {
    if (fwrite(MAGIC, 1, MAGIC_LEN, out) != MAGIC_LEN)
        die_errno("cannot write archive header");
    write_u8(out, FORMAT_VERSION);
}

static int check_magic(FILE *in) {
    char magic[MAGIC_LEN];
    if (fread(magic, 1, MAGIC_LEN, in) != MAGIC_LEN ||
        memcmp(magic, MAGIC, MAGIC_LEN) != 0) {
        warn_msg("not a pack archive (bad magic)");
        return -1;
    }
    uint8_t version;
    if (read_u8(in, &version) < 0) {
        warn_msg("archive is truncated");
        return -1;
    }
    if (version != FORMAT_VERSION) {
        warn_msg("unsupported archive version %u (this build reads %u)",
                 version, FORMAT_VERSION);
        return -1;
    }
    return 0;
}

static FILE *open_stage(void) {
    FILE *tmp = tmpfile();
    if (tmp == NULL)
        die_errno("cannot create a temporary file");
    return tmp;
}

static int cmd_compress(const char *src, const char *dest, int debug) {
    FILE *stage = open_stage();

    if (archive_create(src, stage, debug) < 0) {
        fclose(stage);
        return 1;
    }
    if (fflush(stage) != 0)
        die_errno("cannot flush the staged archive");
    if (fseek(stage, 0, SEEK_SET) < 0)
        die_errno("cannot rewind the staged archive");

    FILE *out = fopen(dest, "wb");
    if (out == NULL)
        die_errno("cannot create %s", dest);

    write_magic(out);
    if (debug)
        printf("\n");
    int rc = huffman_compress(stage, out, debug);

    fclose(stage);
    if (fclose(out) != 0)
        die_errno("cannot finish writing %s", dest);

    if (rc == 0 && !debug)
        printf("packed %s -> %s\n", src, dest);
    return rc == 0 ? 0 : 1;
}

static int cmd_extract(const char *src, const char *dest, int debug) {
    FILE *in = fopen(src, "rb");
    if (in == NULL)
        die_errno("cannot open %s", src);

    if (check_magic(in) < 0) {
        fclose(in);
        return 1;
    }

    FILE *stage = open_stage();
    int rc = huffman_decompress(in, stage, debug);
    fclose(in);
    if (rc < 0) {
        fclose(stage);
        return 1;
    }

    if (fflush(stage) != 0)
        die_errno("cannot flush the staged archive");
    if (fseek(stage, 0, SEEK_SET) < 0)
        die_errno("cannot rewind the staged archive");

    if (debug)
        printf("\n");
    rc = archive_extract(stage, dest, debug);
    fclose(stage);

    if (rc == 0 && !debug)
        printf("unpacked %s -> %s\n", src, dest);
    return rc == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    int debug = 0;
    const char *mode = NULL;
    const char *positional[2] = {NULL, NULL};
    int npos = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--debug") == 0) {
            debug = 1;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(arg, "-c") == 0 || strcmp(arg, "-x") == 0) {
            if (mode != NULL && strcmp(mode, arg) != 0) {
                fprintf(stderr, "pack: -c and -x are mutually exclusive\n");
                return 2;
            }
            mode = arg;
        } else if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "pack: unknown option: %s\n", arg);
            usage(stderr);
            return 2;
        } else if (npos < 2) {
            positional[npos++] = arg;
        } else {
            fprintf(stderr, "pack: too many arguments\n");
            usage(stderr);
            return 2;
        }
    }

    if (mode == NULL || npos != 2) {
        usage(stderr);
        return 2;
    }

    if (strcmp(mode, "-c") == 0)
        return cmd_compress(positional[0], positional[1], debug);
    return cmd_extract(positional[0], positional[1], debug);
}

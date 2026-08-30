#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("pack: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

void die_errno(const char *fmt, ...) {
    int e = errno;
    va_list ap;
    va_start(ap, fmt);
    fputs("pack: ", stderr);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, ": %s\n", strerror(e));
    va_end(ap);
    exit(1);
}

void warn_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("pack: warning: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (p == NULL)
        die("out of memory");
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (q == NULL)
        die("out of memory");
    return q;
}

void write_u8(FILE *f, uint8_t v) {
    if (fputc(v, f) == EOF)
        die_errno("write failed");
}

void write_u16(FILE *f, uint16_t v) {
    for (int shift = 8; shift >= 0; shift -= 8)
        write_u8(f, (uint8_t)(v >> shift));
}

void write_u32(FILE *f, uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8)
        write_u8(f, (uint8_t)(v >> shift));
}

void write_u64(FILE *f, uint64_t v) {
    for (int shift = 56; shift >= 0; shift -= 8)
        write_u8(f, (uint8_t)(v >> shift));
}

int read_u8(FILE *f, uint8_t *out) {
    int c = fgetc(f);
    if (c == EOF)
        return -1;
    *out = (uint8_t)c;
    return 0;
}

static int read_be(FILE *f, uint64_t *out, int nbytes) {
    uint64_t v = 0;
    for (int i = 0; i < nbytes; i++) {
        uint8_t b;
        if (read_u8(f, &b) < 0)
            return -1;
        v = (v << 8) | b;
    }
    *out = v;
    return 0;
}

int read_u16(FILE *f, uint16_t *out) {
    uint64_t v;
    if (read_be(f, &v, 2) < 0)
        return -1;
    *out = (uint16_t)v;
    return 0;
}

int read_u32(FILE *f, uint32_t *out) {
    uint64_t v;
    if (read_be(f, &v, 4) < 0)
        return -1;
    *out = (uint32_t)v;
    return 0;
}

int read_u64(FILE *f, uint64_t *out) { return read_be(f, out, 8); }

int path_is_safe(const char *path) {
    if (path == NULL || path[0] == '\0')
        return 0;
    if (path[0] == '/')
        return 0;

    const char *p = path;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len == 2 && p[0] == '.' && p[1] == '.')
            return 0;
        if (len == 0 && slash)
            return 0;
        if (!slash)
            break;
        p = slash + 1;
    }
    return 1;
}

int mkdir_p(const char *path, unsigned int mode) {
    char *copy = strdup(path);
    if (copy == NULL)
        die("out of memory");

    for (char *p = copy + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(copy, mode) < 0 && errno != EEXIST) {
            free(copy);
            return -1;
        }
        *p = '/';
    }

    int rc = 0;
    if (mkdir(copy, mode) < 0 && errno != EEXIST)
        rc = -1;
    free(copy);
    return rc;
}

int mkdir_parents(const char *path, unsigned int mode) {
    const char *slash = strrchr(path, '/');
    if (slash == NULL)
        return 0;

    size_t len = (size_t)(slash - path);
    char *parent = xmalloc(len + 1);
    memcpy(parent, path, len);
    parent[len] = '\0';

    int rc = (len == 0) ? 0 : mkdir_p(parent, mode);
    free(parent);
    return rc;
}

char *join_path(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la == 0)
        return strdup(b);

    while (la > 0 && a[la - 1] == '/')
        la--;

    char *out = xmalloc(la + 1 + lb + 1);
    memcpy(out, a, la);
    out[la] = '/';
    memcpy(out + la + 1, b, lb);
    out[la + 1 + lb] = '\0';
    return out;
}

char *base_name(const char *path) {
    size_t end = strlen(path);
    while (end > 0 && path[end - 1] == '/')
        end--;
    if (end == 0) /* path was "/" or "" */
        return strdup("root");

    size_t start = end;
    while (start > 0 && path[start - 1] != '/')
        start--;

    size_t len = end - start;
    char *out = xmalloc(len + 1);
    memcpy(out, path + start, len);
    out[len] = '\0';
    return out;
}

#include "archive.h"

#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ENTRY_DIR 0
#define ENTRY_FILE 1

static void write_entry_header(FILE *out, uint8_t type, uint32_t mode,
                               const char *rel_path, uint64_t size) {
    size_t len = strlen(rel_path);
    if (len > UINT16_MAX)
        die("path too long to archive: %s", rel_path);

    write_u8(out, type);
    write_u32(out, mode);
    write_u16(out, (uint16_t)len);
    if (len > 0 && fwrite(rel_path, 1, len, out) != len)
        die_errno("write failed");
    write_u64(out, size);
}

static void copy_bytes(FILE *in, FILE *out, uint64_t count, const char *what) {
    unsigned char buf[65536];
    while (count > 0) {
        size_t want = count < sizeof(buf) ? (size_t)count : sizeof(buf);
        size_t got = fread(buf, 1, want, in);
        if (got == 0)
            die("unexpected end of %s", what);
        if (fwrite(buf, 1, got, out) != got)
            die_errno("write failed");
        count -= got;
    }
}

static int name_cmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int archive_walk(const char *fs_path, const char *rel_path, FILE *out,
                        int debug);

static int archive_dir(const char *fs_path, const char *rel_path, FILE *out,
                       int debug) {
    DIR *d = opendir(fs_path);
    if (d == NULL) {
        warn_msg("cannot open directory %s: %s", fs_path, strerror(errno));
        return 0;
    }

    char **names = NULL;
    size_t count = 0, cap = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (count == cap) {
            cap = cap ? cap * 2 : 16;
            names = xrealloc(names, cap * sizeof(char *));
        }
        names[count] = strdup(ent->d_name);
        if (names[count] == NULL)
            die("out of memory");
        count++;
    }
    closedir(d);

    if (count > 1)
        qsort(names, count, sizeof(char *), name_cmp);

    int rc = 0;
    for (size_t i = 0; i < count; i++) {
        char *child_fs = join_path(fs_path, names[i]);
        char *child_rel = join_path(rel_path, names[i]);
        if (archive_walk(child_fs, child_rel, out, debug) < 0)
            rc = -1;
        free(child_fs);
        free(child_rel);
        free(names[i]);
    }
    free(names);
    return rc;
}

static int archive_file(const char *fs_path, const char *rel_path,
                        const struct stat *st, FILE *out, int debug) {
    FILE *f = fopen(fs_path, "rb");
    if (f == NULL) {
        warn_msg("skipping %s: %s", fs_path, strerror(errno));
        return 0;
    }

    uint64_t size = (uint64_t)st->st_size;
    write_entry_header(out, ENTRY_FILE, (uint32_t)(st->st_mode & 07777),
                       rel_path, size);
    copy_bytes(f, out, size, fs_path);
    fclose(f);

    if (debug)
        printf("  file  %04o %12llu  %s\n", st->st_mode & 07777,
               (unsigned long long)size, rel_path);
    return 0;
}

static int archive_walk(const char *fs_path, const char *rel_path, FILE *out,
                        int debug) {
    struct stat st;
    if (lstat(fs_path, &st) < 0) {
        warn_msg("cannot stat %s: %s", fs_path, strerror(errno));
        return 0;
    }

    if (S_ISDIR(st.st_mode)) {
        write_entry_header(out, ENTRY_DIR, (uint32_t)(st.st_mode & 07777),
                           rel_path, 0);
        if (debug)
            printf("  dir   %04o %12s  %s/\n", st.st_mode & 07777, "-",
                   rel_path);
        return archive_dir(fs_path, rel_path, out, debug);
    }

    if (S_ISREG(st.st_mode))
        return archive_file(fs_path, rel_path, &st, out, debug);

    warn_msg("skipping %s (not a regular file or directory)", fs_path);
    return 0;
}

int archive_create(const char *src_path, FILE *out, int debug) {
    struct stat st;
    if (lstat(src_path, &st) < 0) {
        warn_msg("cannot stat %s: %s", src_path, strerror(errno));
        return -1;
    }
    if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) {
        warn_msg("%s is neither a regular file nor a directory", src_path);
        return -1;
    }

    char *root = base_name(src_path);
    if (debug)
        printf("archiving %s as \"%s\":\n", src_path, root);

    int rc = archive_walk(src_path, root, out, debug);
    free(root);
    return rc;
}

int archive_extract(FILE *in, const char *dest_dir, int debug) {
    if (mkdir_p(dest_dir, 0755) < 0)
        die_errno("cannot create destination %s", dest_dir);

    if (debug)
        printf("extracting into %s:\n", dest_dir);

    while (1) {
        uint8_t type;
        if (read_u8(in, &type) < 0)
            break;

        uint32_t mode;
        uint16_t path_len;
        if (read_u32(in, &mode) < 0 || read_u16(in, &path_len) < 0) {
            warn_msg("archive stream is truncated in an entry header");
            return -1;
        }

        char *rel = xmalloc((size_t)path_len + 1);
        if (path_len > 0 && fread(rel, 1, path_len, in) != path_len) {
            warn_msg("archive stream is truncated in an entry path");
            free(rel);
            return -1;
        }
        rel[path_len] = '\0';

        uint64_t size;
        if (read_u64(in, &size) < 0) {
            warn_msg("archive stream is truncated in an entry size");
            free(rel);
            return -1;
        }

        if (!path_is_safe(rel)) {
            warn_msg("refusing unsafe path in archive: %s", rel);
            free(rel);
            return -1;
        }

        char *full = join_path(dest_dir, rel);
        mode &= 07777;

        if (type == ENTRY_DIR) {
            if (mkdir_p(full, mode | 0700) < 0)
                die_errno("cannot create directory %s", full);
            if (debug)
                printf("  dir   %04o %12s  %s\n", mode, "-", rel);
        } else if (type == ENTRY_FILE) {
            if (mkdir_parents(full, 0755) < 0)
                die_errno("cannot create parent directories for %s", full);
            FILE *f = fopen(full, "wb");
            if (f == NULL)
                die_errno("cannot create %s", full);
            copy_bytes(in, f, size, "archive stream");
            fclose(f);
            if (chmod(full, mode) < 0)
                warn_msg("cannot set mode on %s: %s", full, strerror(errno));
            if (debug)
                printf("  file  %04o %12llu  %s\n", mode,
                       (unsigned long long)size, rel);
        } else {
            warn_msg("unknown entry type %u in archive", type);
            free(rel);
            free(full);
            return -1;
        }

        free(rel);
        free(full);
    }

    return 0;
}

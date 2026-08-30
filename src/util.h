#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

void die(const char *fmt, ...);
void die_errno(const char *fmt, ...);
void warn_msg(const char *fmt, ...);

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);

void write_u8(FILE *f, uint8_t v);
void write_u16(FILE *f, uint16_t v);
void write_u32(FILE *f, uint32_t v);
void write_u64(FILE *f, uint64_t v);

int read_u8(FILE *f, uint8_t *out);
int read_u16(FILE *f, uint16_t *out);
int read_u32(FILE *f, uint32_t *out);
int read_u64(FILE *f, uint64_t *out);

int path_is_safe(const char *path);

int mkdir_p(const char *path, unsigned int mode);

int mkdir_parents(const char *path, unsigned int mode);

char *join_path(const char *a, const char *b);

char *base_name(const char *path);

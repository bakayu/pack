#pragma once
#include <stdio.h>

int archive_create(const char *src_path, FILE *out, int debug);

int archive_extract(FILE *in, const char *dest_dir, int debug);

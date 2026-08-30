#pragma once
#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *f;
    unsigned char buf;
    int nbits;
} BitWriter;

void bw_init(BitWriter *w, FILE *f);
void bw_write_bit(BitWriter *w, int bit);
void bw_write_bits(BitWriter *w, uint64_t bits, int count);
void bw_flush(BitWriter *w);

typedef struct {
    FILE *f;
    unsigned char buf;
    int nbits;
} BitReader;

void br_init(BitReader *r, FILE *f);
int br_read_bit(BitReader *r);
int br_read_bits(BitReader *r, int count, uint64_t *out);

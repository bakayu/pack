#include "bitio.h"

#include "util.h"

void bw_init(BitWriter *w, FILE *f) {
    w->f = f;
    w->buf = 0;
    w->nbits = 0;
}

void bw_write_bit(BitWriter *w, int bit) {
    w->buf = (unsigned char)((w->buf << 1) | (bit & 1));
    w->nbits++;
    if (w->nbits == 8) {
        if (fputc(w->buf, w->f) == EOF)
            die_errno("write failed");
        w->buf = 0;
        w->nbits = 0;
    }
}

void bw_write_bits(BitWriter *w, uint64_t bits, int count) {
    for (int i = count - 1; i >= 0; i--)
        bw_write_bit(w, (int)((bits >> i) & 1));
}

void bw_flush(BitWriter *w) {
    if (w->nbits == 0)
        return;
    w->buf = (unsigned char)(w->buf << (8 - w->nbits));
    if (fputc(w->buf, w->f) == EOF)
        die_errno("write failed");
    w->buf = 0;
    w->nbits = 0;
}

void br_init(BitReader *r, FILE *f) {
    r->f = f;
    r->buf = 0;
    r->nbits = 0;
}

int br_read_bit(BitReader *r) {
    if (r->nbits == 0) {
        int c = fgetc(r->f);
        if (c == EOF)
            return -1;
        r->buf = (unsigned char)c;
        r->nbits = 8;
    }
    r->nbits--;
    return (r->buf >> r->nbits) & 1;
}

int br_read_bits(BitReader *r, int count, uint64_t *out) {
    uint64_t v = 0;
    for (int i = 0; i < count; i++) {
        int bit = br_read_bit(r);
        if (bit < 0)
            return -1;
        v = (v << 1) | (uint64_t)bit;
    }
    *out = v;
    return 0;
}

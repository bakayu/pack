#include "huffman.h"

#include "bitio.h"
#include "util.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

Node *makeLeaf(unsigned char byte, uint64_t freq) {
    Node *node = (Node *)xmalloc(sizeof(Node));
    node->byte = byte;
    node->freq = freq;
    node->left = NULL;
    node->right = NULL;
    return node;
}

Node *makeNode(Node *left, Node *right) {
    Node *node = (Node *)xmalloc(sizeof(Node));
    node->byte = 0;
    node->freq = left->freq + right->freq;
    node->left = left;
    node->right = right;
    return node;
}

void freeTree(Node *root) {
    if (root == NULL)
        return;
    freeTree(root->left);
    freeTree(root->right);
    free(root);
}

typedef struct Heap {
    Node **data;
    int size;
    int capacity;
} Heap;

static Heap *createHeap(void) {
    Heap *h = (Heap *)xmalloc(sizeof(Heap));
    h->capacity = 4;
    h->data = (Node **)xmalloc(sizeof(Node *) * h->capacity);
    h->size = 0;
    return h;
}

static int compare(Node *a, Node *b) { return a->freq < b->freq; }

static void swap(Node **x, Node **y) {
    Node *tmp = *x;
    *x = *y;
    *y = tmp;
}

static void siftUp(Heap *h, int i) {
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (compare(h->data[i], h->data[parent])) {
            swap(&h->data[i], &h->data[parent]);
            i = parent;
        } else
            break;
    }
}

static void siftDown(Heap *h, int i) {
    while (1) {
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        int top = i;

        if (left < h->size && compare(h->data[left], h->data[top]))
            top = left;
        if (right < h->size && compare(h->data[right], h->data[top]))
            top = right;

        if (top == i)
            break;
        swap(&h->data[i], &h->data[top]);
        i = top;
    }
}

static void push(Heap *h, Node *node) {
    if (h->size == h->capacity) {
        h->capacity *= 2;
        h->data = (Node **)xrealloc(h->data, sizeof(Node *) * h->capacity);
    }
    h->data[h->size] = node;
    siftUp(h, h->size);
    h->size++;
}

static Node *pop(Heap *h) {
    Node *top = h->data[0];
    h->size--;
    h->data[0] = h->data[h->size];
    siftDown(h, 0);
    return top;
}

static int isEmpty(Heap *h) { return h->size == 0; }

static void cleanup(Heap *h) {
    free(h->data);
    free(h);
}

uint64_t countFrequencies(FILE *in, uint64_t freq[256]) {
    memset(freq, 0, sizeof(uint64_t) * 256);

    unsigned char buf[65536];
    uint64_t total = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        for (size_t i = 0; i < n; i++)
            freq[buf[i]]++;
        total += n;
    }
    if (ferror(in))
        die_errno("read failed while counting frequencies");
    return total;
}

Node *buildTree(uint64_t freq[256]) {
    Heap *h = createHeap();
    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0)
            push(h, makeLeaf((unsigned char)i, freq[i]));
    }
    if (isEmpty(h)) {
        cleanup(h);
        return NULL;
    }

    while (h->size > 1) {
        Node *left = pop(h);
        Node *right = pop(h);
        push(h, makeNode(left, right));
    }

    Node *result = pop(h);
    cleanup(h);
    return result;
}

static void buildCodeTableHelper(Node *node, uint64_t code,
                                 unsigned char length, Code table[256]) {
    if (IS_LEAF(node)) {
        table[node->byte] = (Code){code, length};
        return;
    }
    buildCodeTableHelper(node->left, code << 1, length + 1, table);
    buildCodeTableHelper(node->right, (code << 1) | 1, length + 1, table);
}

void buildCodeTable(Node *root, Code table[256]) {
    memset(table, 0, sizeof(Code) * 256);
    if (root == NULL)
        return;
    if (IS_LEAF(root)) {
        table[root->byte] = (Code){0, 1};
        return;
    }
    buildCodeTableHelper(root, 0, 0, table);
}

static void writeTree(BitWriter *w, Node *node) {
    if (IS_LEAF(node)) {
        bw_write_bit(w, 1);
        bw_write_bits(w, node->byte, 8);
        return;
    }
    bw_write_bit(w, 0);
    writeTree(w, node->left);
    writeTree(w, node->right);
}

static Node *readTree(BitReader *r, int depth) {
    if (depth > 256)
        return NULL;

    int marker = br_read_bit(r);
    if (marker < 0)
        return NULL;

    if (marker == 1) {
        uint64_t byte;
        if (br_read_bits(r, 8, &byte) < 0)
            return NULL;
        return makeLeaf((unsigned char)byte, 0);
    }

    Node *left = readTree(r, depth + 1);
    if (left == NULL)
        return NULL;
    Node *right = readTree(r, depth + 1);
    if (right == NULL) {
        freeTree(left);
        return NULL;
    }
    return makeNode(left, right);
}

static const char *byte_label(unsigned char b) {
    static char buf[8];
    if (isprint(b) && b != '\\' && b != '\'')
        snprintf(buf, sizeof(buf), "'%c' ", b);
    else
        snprintf(buf, sizeof(buf), "\\x%02x", b);
    return buf;
}

void debug_print_frequencies(const uint64_t freq[256], uint64_t total) {
    int distinct = 0;
    printf("frequency table (%llu bytes of input):\n",
           (unsigned long long)total);
    for (int i = 0; i < 256; i++) {
        if (freq[i] == 0)
            continue;
        distinct++;
        printf("  %s  %10llu  %6.2f%%\n", byte_label((unsigned char)i),
               (unsigned long long)freq[i],
               total ? 100.0 * (double)freq[i] / (double)total : 0.0);
    }
    printf("  -> %d distinct byte values\n", distinct);
}

static void print_tree_rec(Node *node, const char *prefix, int is_left,
                           int is_root) {
    if (node == NULL)
        return;

    printf("%s", prefix);
    if (!is_root)
        printf("%s", is_left ? "|-- " : "`-- ");

    if (IS_LEAF(node)) {
        printf("%s (%llu)\n", byte_label(node->byte),
               (unsigned long long)node->freq);
    } else {
        printf("* (%llu)\n", (unsigned long long)node->freq);
    }

    char child_prefix[512];
    snprintf(child_prefix, sizeof(child_prefix), "%s%s", prefix,
             is_root ? "" : (is_left ? "|   " : "    "));

    print_tree_rec(node->left, child_prefix, 1, 0);
    print_tree_rec(node->right, child_prefix, 0, 0);
}

void debug_print_tree(Node *root) {
    printf("\nhuffman tree:\n");
    if (root == NULL) {
        printf("  (empty)\n");
        return;
    }
    print_tree_rec(root, "", 0, 1);
}

void debug_print_code_table(const Code table[256], const uint64_t freq[256]) {
    printf("\ncode table:\n");
    printf("  byte        freq   bits  code\n");
    for (int i = 0; i < 256; i++) {
        if (table[i].length == 0)
            continue;
        printf("  %s  %10llu  %5u  ", byte_label((unsigned char)i),
               (unsigned long long)(freq ? freq[i] : 0), table[i].length);
        for (int b = table[i].length - 1; b >= 0; b--)
            putchar((table[i].bits >> b) & 1 ? '1' : '0');
        putchar('\n');
    }
}

int huffman_compress(FILE *in, FILE *out, int debug) {
    long out_start = ftell(out);

    uint64_t freq[256];
    uint64_t total = countFrequencies(in, freq);

    if (fseek(in, 0, SEEK_SET) < 0)
        die_errno("cannot rewind input for the encoding pass");

    write_u64(out, total);

    if (debug)
        debug_print_frequencies(freq, total);

    if (total == 0) {
        if (debug)
            printf("\ninput is empty; nothing to encode\n");
        return 0;
    }

    Node *root = buildTree(freq);
    Code table[256];
    buildCodeTable(root, table);

    if (debug) {
        debug_print_tree(root);
        debug_print_code_table(table, freq);
    }

    BitWriter w;
    bw_init(&w, out);
    writeTree(&w, root);

    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        for (size_t i = 0; i < n; i++) {
            Code c = table[buf[i]];
            bw_write_bits(&w, c.bits, c.length);
        }
    }
    if (ferror(in))
        die_errno("read failed while encoding");
    bw_flush(&w);

    if (debug) {
        if (fflush(out) != 0)
            die_errno("flush failed");
        long out_end = ftell(out);
        uint64_t compressed = (uint64_t)(out_end - out_start);

        uint64_t code_bits = 0;
        for (int i = 0; i < 256; i++)
            code_bits += freq[i] * table[i].length;

        printf("\ncompression:\n");
        printf("  input          %12llu bytes\n", (unsigned long long)total);
        printf("  encoded body   %12llu bytes (%llu bits)\n",
               (unsigned long long)((code_bits + 7) / 8),
               (unsigned long long)code_bits);
        printf("  output total   %12llu bytes (incl. header + tree)\n",
               (unsigned long long)compressed);
        printf("  ratio          %11.2f%% of original\n",
               100.0 * (double)compressed / (double)total);
        printf("  saved          %12lld bytes\n",
               (long long)total - (long long)compressed);
        printf("  avg code len   %11.2f bits/byte (was 8.00)\n",
               (double)code_bits / (double)total);
    }

    freeTree(root);
    return 0;
}

int huffman_decompress(FILE *in, FILE *out, int debug) {
    uint64_t total;
    if (read_u64(in, &total) < 0) {
        warn_msg("compressed stream is truncated (no header)");
        return -1;
    }

    if (total == 0) {
        if (debug)
            printf("compressed stream holds 0 bytes\n");
        return 0;
    }

    BitReader r;
    br_init(&r, in);

    Node *root = readTree(&r, 0);
    if (root == NULL) {
        warn_msg("compressed stream has a malformed huffman tree");
        return -1;
    }

    if (debug) {
        printf("decoding %llu bytes\n", (unsigned long long)total);
        debug_print_tree(root);
    }

    int single = IS_LEAF(root);

    for (uint64_t written = 0; written < total; written++) {
        Node *node = root;
        if (single) {
            if (br_read_bit(&r) < 0) {
                warn_msg("compressed stream ended early");
                freeTree(root);
                return -1;
            }
        } else {
            while (!IS_LEAF(node)) {
                int bit = br_read_bit(&r);
                if (bit < 0) {
                    warn_msg("compressed stream ended early");
                    freeTree(root);
                    return -1;
                }
                node = bit ? node->right : node->left;
            }
        }
        if (fputc(node->byte, out) == EOF)
            die_errno("write failed while decoding");
    }

    freeTree(root);
    return 0;
}

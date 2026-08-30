#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct Node {
    unsigned char byte;
    uint64_t freq;
    struct Node *left;
    struct Node *right;
} Node;

#define IS_LEAF(n) ((n)->left == NULL)

Node *makeLeaf(unsigned char byte, uint64_t freq);
Node *makeNode(Node *left, Node *right);
void freeTree(Node *root);

uint64_t countFrequencies(FILE *in, uint64_t freq[256]);

Node *buildTree(uint64_t freq[256]);

typedef struct {
    uint64_t bits;
    unsigned char length;
} Code;

void buildCodeTable(Node *root, Code table[256]);

int huffman_compress(FILE *in, FILE *out, int debug);

int huffman_decompress(FILE *in, FILE *out, int debug);

void debug_print_frequencies(const uint64_t freq[256], uint64_t total);
void debug_print_tree(Node *root);
void debug_print_code_table(const Code table[256], const uint64_t freq[256]);

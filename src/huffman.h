#pragma once
#include <stdint.h>
#include <stdlib.h>

typedef struct Node {
    unsigned char byte;
    int freq;
    struct Node *left;
    struct Node *right;
} Node;

Node *makeLeaf(unsigned char byte, int freq);
Node *makeNode(Node *left, Node *right);

// returns NULL for empty/all-zero input case
Node *buildTree(int freq[256]);

typedef struct {
    uint32_t bits;
    unsigned char length;
} Code;

void buildCodeTable(Node *root, Code table[256]);

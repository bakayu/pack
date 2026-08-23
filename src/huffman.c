#include "huffman.h"
#include <stdlib.h>

typedef struct Heap {
    Node **data;
    int size;
    int capacity;
} Heap;

static Heap *createHeap();
static void swap(Node **x, Node **y);
static void siftUp(Heap *h, int i);
static void siftDown(Heap *h, int i);
static void push(Heap *h, Node *val);
static Node *pop(Heap *h);
static int peek(Heap *h);
static int isEmpty(Heap *h);
static void cleanup(Heap *h);
static void buildCodeTableHelper(Node *node, uint32_t code,
                                 unsigned char length, Code table[256]);

Node *makeLeaf(unsigned char byte, int freq) {
    Node *node = (Node *)malloc(sizeof(Node));
    node->byte = byte;
    node->freq = freq;
    node->left = NULL;
    node->right = NULL;
    return node;
}

Node *makeNode(Node *left, Node *right) {
    Node *node = malloc(sizeof(Node));
    node->byte = 0;
    node->freq = left->freq + right->freq;
    node->left = left;
    node->right = right;
    return node;
}

static Heap *createHeap() {
    Heap *h = (Heap *)malloc(sizeof(Heap));
    h->capacity = 4;
    h->data = (Node **)malloc(sizeof(Node *) * h->capacity);
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
    // grow if full
    if (h->size == h->capacity) {
        h->capacity *= 2;
        h->data = (Node **)realloc(h->data, sizeof(Node *) * h->capacity);
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

static int peek(Heap *h) { return h->data[0]->freq; }

static int isEmpty(Heap *h) { return h->size == 0; }

static void cleanup(Heap *h) {
    free(h->data);
    free(h);
}

Node *buildTree(int freq[256]) {
    Heap *h = createHeap();
    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            push(h, makeLeaf(i, freq[i]));
        }
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

void buildCodeTable(Node *root, Code table[256]) {
    if (root->left == NULL) {
        table[root->byte] = (Code){0, 1};
        return;
    }
    buildCodeTableHelper(root, 0, 0, table);
}

void buildCodeTableHelper(Node *node, uint32_t code, unsigned char length,
                          Code table[256]) {
    if (node->left == NULL) {
        table[node->byte] = (Code){code, length};
        return;
    }
    buildCodeTableHelper(node->left, code << 1, length + 1, table);
    buildCodeTableHelper(node->right, code << 1 | 1, length + 1, table);
}

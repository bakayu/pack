#include "huffman.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
void debug_huffman_tree(Node *root) {
    if (root == NULL) {
        return;
    }
    debug_huffman_tree(root->left);
    printf("(byte: %c, freq: %d) ", root->byte, root->freq);
    debug_huffman_tree(root->right);
}

void print_huffman_tree(Node *node, const char *prefix, int is_left,
                        int is_root) {
    if (node == NULL) {
        return;
    }

    printf("%s", prefix);
    if (!is_root) {
        printf("%s", is_left ? "|-- " : "`-- ");
    }

    if (node->left == NULL) {
        printf("'%c' (%d)\n", node->byte, node->freq);
    } else {
        printf("* (%d)\n", node->freq);
    }

    char child_prefix[256];
    snprintf(child_prefix, sizeof(child_prefix), "%s%s", prefix,
             is_root ? "" : (is_left ? "|   " : "    "));

    print_huffman_tree(node->left, child_prefix, 1, 0);
    print_huffman_tree(node->right, child_prefix, 0, 0);
}

int main(void) {
    int freq[256] = {0};
    const char *sample = "abcd";

    printf("frequency table derived from input:\n");
    for (size_t i = 0; sample[i] != '\0'; i++) {
        unsigned char ch = sample[i];
        freq[ch]++;
    }

    int distinct = 0;
    for (int i = 0; i < 256; i++) {
        if (freq[i]) {
            printf("%c: %d\n", i, freq[i]);
            distinct++;
        }
    }

    Node *root_node = buildTree(freq);

    // DEBUG: printing huffman tree
    debug_huffman_tree(root_node);
    printf("\n");

    printf("\nHuffman tree structure:\n");
    print_huffman_tree(root_node, "", 0, 1);

    Code table[256] = {0};
    buildCodeTable(root_node, table);

    printf("\nCode table derived from huffman tree:\n");
    for (int i = 0; i < 256; i++) {
        if (table[i].length > 0) {
            printf("char: %c, original code: %b, compressed code: %b, length: "
                   "%d\n",
                   i, i, table[i].bits, table[i].length);
        }
    }

    return 0;
}

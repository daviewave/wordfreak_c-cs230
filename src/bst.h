#ifndef BST_H
#define BST_H

#include <stddef.h>

/* One distinct word and how many times it was seen. */
typedef struct BstNode {
    char *word;
    unsigned long count;
    struct BstNode *left;
    struct BstNode *right;
} BstNode;

/* Called once per node in sorted order. Returns 0 to continue. */
typedef int (*BstVisitor)(const BstNode *node, void *context);

int bst_insert(BstNode **root, const char *word);
const BstNode *bst_find(const BstNode *root, const char *word);
int bst_visit_in_order(const BstNode *root, BstVisitor visit, void *context);
size_t bst_size(const BstNode *root);
void bst_free(BstNode *root);

#endif

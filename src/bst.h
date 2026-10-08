#ifndef BST_H
#define BST_H

#include <stddef.h>

/* One distinct word and how many times it was seen. */
typedef struct BstNode {
    char *word;               /* heap copy, lowercase, NUL-terminated; owned by the node */
    unsigned long count;      /* occurrences across every input */
    struct BstNode *left;     /* words that sort before this one */
    struct BstNode *right;    /* words that sort after this one */
} BstNode;

/* Called once per node in sorted order. Returns 0 to continue. */
typedef int (*BstVisitor)(const BstNode *node, void *context);

int bst_insert(BstNode **root, const char *word);
const BstNode *bst_find(const BstNode *root, const char *word);
int bst_visit_in_order(const BstNode *root, BstVisitor visit, void *context);
size_t bst_size(const BstNode *root);
void bst_free(BstNode *root);

#endif

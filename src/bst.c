#include "bst.h"

#include <stdlib.h>
#include <string.h>

/* Allocates a node holding its own copy of word with count 1, or NULL. */
static BstNode *make_node(const char *word) {
    BstNode *node = malloc(sizeof *node);
    if (node == NULL) {
        return NULL;
    }
    size_t size = strlen(word) + 1;
    node->word = malloc(size);
    if (node->word == NULL) {
        free(node);
        return NULL;
    }
    memcpy(node->word, word, size);
    node->count = 1;
    node->left = NULL;
    node->right = NULL;
    return node;
}

/* Walks from link to the link where word is or would be stored. */
static BstNode **find_link(BstNode **link, const char *word) {
    while (*link != NULL) {
        int order = strcmp(word, (*link)->word);
        if (order == 0) {
            return link;
        }
        link = order < 0 ? &(*link)->left : &(*link)->right;
    }
    return link;
}

/* Counts one occurrence of word: increments an existing node or inserts a
 * new one. Iterative, so sorted input cannot exhaust the stack.
 * @return 0, or -1 when memory is exhausted. */
int bst_insert(BstNode **root, const char *word) {
    BstNode **link = find_link(root, word);
    if (*link != NULL) {
        (*link)->count++;
        return 0;
    }
    *link = make_node(word);
    return *link == NULL ? -1 : 0;
}

/* Returns the node for word, or NULL when it is not in the tree. */
const BstNode *bst_find(const BstNode *root, const char *word) {
    BstNode *mutable_root = (BstNode *)root;
    return *find_link(&mutable_root, word);
}

/* Visits every node in sorted order. Returns 0, or the visitor's first
 * non-zero result, which stops the walk. */
int bst_visit_in_order(const BstNode *root, BstVisitor visit, void *context) {
    if (root == NULL) {
        return 0;
    }
    int result = bst_visit_in_order(root->left, visit, context);
    if (result != 0) {
        return result;
    }
    result = visit(root, context);
    if (result != 0) {
        return result;
    }
    return bst_visit_in_order(root->right, visit, context);
}

/* Number of distinct words in the tree. */
size_t bst_size(const BstNode *root) {
    if (root == NULL) {
        return 0;
    }
    return 1 + bst_size(root->left) + bst_size(root->right);
}

/* Releases every node and word in the tree (post-order). */
void bst_free(BstNode *root) {
    if (root == NULL) {
        return;
    }
    bst_free(root->left);
    bst_free(root->right);
    free(root->word);
    free(root);
}

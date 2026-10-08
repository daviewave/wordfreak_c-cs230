#include <string.h>
#include "../../src/bst.h"
#include "check.h"

typedef struct {
    char joined[4096];
    unsigned long counts[64];
    int visited;
} Walk;

static int record(const BstNode *node, void *context) {
    Walk *walk = context;
    if (walk->visited > 0) {
        strcat(walk->joined, " ");
    }
    strcat(walk->joined, node->word);
    walk->counts[walk->visited] = node->count;
    walk->visited++;
    return 0;
}

static void test_insert_counts_duplicates(void) {
    BstNode *root = NULL;
    CHECK_EQ_INT(bst_insert(&root, "the"), 0);
    CHECK_EQ_INT(bst_insert(&root, "the"), 0);
    CHECK_EQ_INT(bst_insert(&root, "then"), 0);
    CHECK_EQ_INT(bst_size(root), 2);
    CHECK(bst_find(root, "the") != NULL);
    CHECK_EQ_INT(bst_find(root, "the")->count, 2);
    CHECK_EQ_INT(bst_find(root, "then")->count, 1);
    CHECK(bst_find(root, "th") == NULL);
    CHECK(bst_find(NULL, "the") == NULL);
    bst_free(root);
}

static void test_in_order_is_alphabetical(void) {
    BstNode *root = NULL;
    const char *words[] = {"mango", "apple", "zebra", "banana", "apple", "cherry", "mango"};
    for (size_t index = 0; index < sizeof words / sizeof words[0]; index++) {
        CHECK_EQ_INT(bst_insert(&root, words[index]), 0);
    }
    Walk walk;
    memset(&walk, 0, sizeof walk);
    CHECK_EQ_INT(bst_visit_in_order(root, record, &walk), 0);
    CHECK_EQ_STR(walk.joined, "apple banana cherry mango zebra");
    CHECK_EQ_INT(walk.counts[0], 2);
    CHECK_EQ_INT(walk.counts[3], 2);
    CHECK_EQ_INT(walk.visited, 5);
    bst_free(root);
}

static void test_sorted_insert_builds_degenerate_tree_fine(void) {
    BstNode *root = NULL;
    char word[16];
    for (int number = 0; number < 2000; number++) {
        snprintf(word, sizeof word, "w%05d", number);
        CHECK_EQ_INT(bst_insert(&root, word), 0);
    }
    CHECK_EQ_INT(bst_size(root), 2000);
    CHECK(root->left == NULL);
    bst_free(root);
}

static void test_inserted_word_is_copied(void) {
    BstNode *root = NULL;
    char scratch[8];
    strcpy(scratch, "copy");
    CHECK_EQ_INT(bst_insert(&root, scratch), 0);
    strcpy(scratch, "junk");
    CHECK(bst_find(root, "copy") != NULL);
    CHECK_EQ_STR(root->word, "copy");
    bst_free(root);
}

static int stop_at_second(const BstNode *node, void *context) {
    int *seen = context;
    (void)node;
    (*seen)++;
    return *seen == 2 ? 5 : 0;
}

static void test_visitor_can_abort(void) {
    BstNode *root = NULL;
    CHECK_EQ_INT(bst_insert(&root, "b"), 0);
    CHECK_EQ_INT(bst_insert(&root, "a"), 0);
    CHECK_EQ_INT(bst_insert(&root, "c"), 0);
    int seen = 0;
    CHECK_EQ_INT(bst_visit_in_order(root, stop_at_second, &seen), 5);
    CHECK_EQ_INT(seen, 2);
    bst_free(root);
}

static void test_empty_tree(void) {
    Walk walk;
    memset(&walk, 0, sizeof walk);
    CHECK_EQ_INT(bst_visit_in_order(NULL, record, &walk), 0);
    CHECK_EQ_INT(walk.visited, 0);
    CHECK_EQ_INT(bst_size(NULL), 0);
    bst_free(NULL);
}

int main(void) {
    test_insert_counts_duplicates();
    test_in_order_is_alphabetical();
    test_sorted_insert_builds_degenerate_tree_fine();
    test_inserted_word_is_copied();
    test_visitor_can_abort();
    test_empty_tree();
    CHECK_REPORT("test_bst");
}

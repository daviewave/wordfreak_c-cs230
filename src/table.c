#include "table.h"

#include <stddef.h>

/* Empties all 26 trees. */
void table_init(WordTable *table) {
    for (int slot = 0; slot < TABLE_LETTER_COUNT; slot++) {
        table->trees[slot] = NULL;
    }
}

/* The spec's hash function from a word to its tree: first letter minus 'a'.
 * @return 0..25 for 'a'..'z'; any other input is the caller's error. */
int table_hash(char first_letter) {
    return first_letter - 'a';
}

/* True when the word is non-empty and starts with a lowercase letter. */
static int is_hashable(const char *word) {
    return word[0] >= 'a' && word[0] <= 'z';
}

/* Counts one occurrence of word in the tree its first letter selects.
 * @return 0; -1 when the word cannot be hashed or memory is exhausted. */
int table_add(WordTable *table, const char *word) {
    if (!is_hashable(word)) {
        return -1;
    }
    return bst_insert(&table->trees[table_hash(word[0])], word);
}

/* Visits every word alphabetically: tree 'a' first, each tree in order.
 * Returns 0 or the visitor's first non-zero result. */
int table_visit_in_order(const WordTable *table, BstVisitor visit, void *context) {
    for (int slot = 0; slot < TABLE_LETTER_COUNT; slot++) {
        int result = bst_visit_in_order(table->trees[slot], visit, context);
        if (result != 0) {
            return result;
        }
    }
    return 0;
}

/* Releases every tree and leaves the table empty and reusable. */
void table_free(WordTable *table) {
    for (int slot = 0; slot < TABLE_LETTER_COUNT; slot++) {
        bst_free(table->trees[slot]);
        table->trees[slot] = NULL;
    }
}

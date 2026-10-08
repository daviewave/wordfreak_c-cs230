#ifndef TABLE_H
#define TABLE_H

#include "bst.h"

/* One tree per letter 'a'..'z'. */
#define TABLE_LETTER_COUNT 26

/* The spec's hashtable of binary search trees: trees[table_hash(c)] holds
 * every word that starts with the letter c. */
typedef struct {
    BstNode *trees[TABLE_LETTER_COUNT];
} WordTable;

void table_init(WordTable *table);
int table_hash(char first_letter);
int table_add(WordTable *table, const char *word);
int table_visit_in_order(const WordTable *table, BstVisitor visit, void *context);
void table_free(WordTable *table);

#endif

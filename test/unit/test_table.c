#include <string.h>
#include "../../src/table.h"
#include "check.h"

typedef struct {
    char joined[4096];
    int visited;
} Walk;

static int record(const BstNode *node, void *context) {
    Walk *walk = context;
    if (walk->visited > 0) {
        strcat(walk->joined, " ");
    }
    strcat(walk->joined, node->word);
    walk->visited++;
    return 0;
}

static void test_hash_maps_each_letter_to_its_slot(void) {
    for (char letter = 'a'; letter <= 'z'; letter++) {
        CHECK_EQ_INT(table_hash(letter), letter - 'a');
    }
    CHECK_EQ_INT(table_hash('a'), 0);
    CHECK_EQ_INT(table_hash('z'), TABLE_LETTER_COUNT - 1);
}

static void test_init_gives_26_empty_trees(void) {
    WordTable table;
    table_init(&table);
    for (int slot = 0; slot < TABLE_LETTER_COUNT; slot++) {
        CHECK(table.trees[slot] == NULL);
    }
    table_free(&table);
}

static void test_add_routes_by_first_letter(void) {
    WordTable table;
    table_init(&table);
    CHECK_EQ_INT(table_add(&table, "zebra"), 0);
    CHECK_EQ_INT(table_add(&table, "apple"), 0);
    CHECK_EQ_INT(table_add(&table, "avocado"), 0);
    CHECK_EQ_INT(table_add(&table, "apple"), 0);
    CHECK(table.trees[table_hash('a')] != NULL);
    CHECK(table.trees[table_hash('z')] != NULL);
    CHECK(table.trees[table_hash('b')] == NULL);
    CHECK_EQ_INT(bst_size(table.trees[0]), 2);
    CHECK_EQ_INT(bst_find(table.trees[0], "apple")->count, 2);
    table_free(&table);
}

static void test_visit_is_alphabetical_across_trees(void) {
    WordTable table;
    table_init(&table);
    const char *words[] = {"zoo", "bee", "ant", "bat", "zip", "cat", "ant"};
    for (size_t index = 0; index < sizeof words / sizeof words[0]; index++) {
        CHECK_EQ_INT(table_add(&table, words[index]), 0);
    }
    Walk walk;
    memset(&walk, 0, sizeof walk);
    CHECK_EQ_INT(table_visit_in_order(&table, record, &walk), 0);
    CHECK_EQ_STR(walk.joined, "ant bat bee cat zip zoo");
    CHECK_EQ_INT(walk.visited, 6);
    table_free(&table);
}

static void test_add_rejects_words_outside_a_to_z(void) {
    WordTable table;
    table_init(&table);
    CHECK_EQ_INT(table_add(&table, ""), -1);
    CHECK_EQ_INT(table_add(&table, "Apple"), -1);
    CHECK_EQ_INT(table_add(&table, "4ever"), -1);
    for (int slot = 0; slot < TABLE_LETTER_COUNT; slot++) {
        CHECK(table.trees[slot] == NULL);
    }
    table_free(&table);
}

static void test_free_resets_to_empty(void) {
    WordTable table;
    table_init(&table);
    CHECK_EQ_INT(table_add(&table, "word"), 0);
    table_free(&table);
    CHECK(table.trees[table_hash('w')] == NULL);
    table_free(&table);
}

int main(void) {
    test_hash_maps_each_letter_to_its_slot();
    test_init_gives_26_empty_trees();
    test_add_routes_by_first_letter();
    test_visit_is_alphabetical_across_trees();
    test_add_rejects_words_outside_a_to_z();
    test_free_resets_to_empty();
    CHECK_REPORT("test_table");
}

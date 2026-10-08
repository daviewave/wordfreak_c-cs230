#include <stdlib.h>
#include <string.h>
#include "../../src/words.h"
#include "check.h"

/* Collects every emitted word into one string separated by '|'. */
typedef struct {
    char joined[8192];
    size_t length;
    int words;
} Collector;

static int collect(const char *word, size_t length, void *context) {
    Collector *collector = context;
    CHECK_EQ_INT(strlen(word), length);
    if (collector->words > 0) {
        collector->joined[collector->length++] = '|';
    }
    memcpy(collector->joined + collector->length, word, length);
    collector->length += length;
    collector->joined[collector->length] = '\0';
    collector->words++;
    return 0;
}

static void feed_in_slices(const char *text, size_t slice, Collector *collector) {
    Tokenizer tokenizer;
    CHECK_EQ_INT(tokenizer_init(&tokenizer), 0);
    memset(collector, 0, sizeof *collector);
    size_t total = strlen(text);
    for (size_t offset = 0; offset < total; offset += slice) {
        size_t count = total - offset < slice ? total - offset : slice;
        CHECK_EQ_INT(tokenizer_feed(&tokenizer, (const unsigned char *)text + offset, count,
                                    collect, collector), 0);
    }
    CHECK_EQ_INT(tokenizer_finish(&tokenizer, collect, collector), 0);
    tokenizer_free(&tokenizer);
}

static void test_letter_classification(void) {
    CHECK(words_is_letter('a'));
    CHECK(words_is_letter('z'));
    CHECK(words_is_letter('A'));
    CHECK(words_is_letter('Z'));
    CHECK(!words_is_letter('4'));
    CHECK(!words_is_letter('\''));
    CHECK(!words_is_letter(' '));
    CHECK(!words_is_letter(0xE2));
    CHECK(!words_is_letter(0));
    CHECK_EQ_INT(words_to_lower('A'), 'a');
    CHECK_EQ_INT(words_to_lower('Z'), 'z');
    CHECK_EQ_INT(words_to_lower('q'), 'q');
}

static void test_spec_example_potato(void) {
    Collector collector;
    feed_in_slices("Isn\xE2\x80\x99t that a POT4TO???", 4096, &collector);
    CHECK_EQ_STR(collector.joined, "isn|t|that|a|pot|to");
    CHECK_EQ_INT(collector.words, 6);
}

static void test_boundary_slices(void) {
    const char *text = "Alpha beta,GAMMA\ndelta4epsilon  zeta";
    for (size_t slice = 1; slice <= 9; slice++) {
        Collector collector;
        feed_in_slices(text, slice, &collector);
        CHECK_EQ_STR(collector.joined, "alpha|beta|gamma|delta|epsilon|zeta");
    }
}

static void test_word_across_exact_chunk_boundary(void) {
    char text[4096 + 10];
    memset(text, ' ', sizeof text);
    memcpy(text + 4090, "straddling", 10);
    text[sizeof text - 1] = '\0';
    Collector collector;
    feed_in_slices(text, 4096, &collector);
    CHECK_EQ_STR(collector.joined, "straddling");
    Tokenizer tokenizer;
    CHECK_EQ_INT(tokenizer_init(&tokenizer), 0);
    memset(&collector, 0, sizeof collector);
    CHECK_EQ_INT(tokenizer_feed(&tokenizer, (const unsigned char *)text, 4096, collect, &collector), 0);
    CHECK_EQ_INT(collector.words, 0);
    CHECK_EQ_INT(tokenizer_feed(&tokenizer, (const unsigned char *)text + 4096, 10, collect, &collector), 0);
    CHECK_EQ_INT(tokenizer_finish(&tokenizer, collect, &collector), 0);
    CHECK_EQ_STR(collector.joined, "straddling");
    tokenizer_free(&tokenizer);
}

static void test_empty_and_separator_only_input(void) {
    Collector collector;
    feed_in_slices("", 1, &collector);
    CHECK_EQ_INT(collector.words, 0);
    feed_in_slices(" \n\t123 ,.;'\"", 3, &collector);
    CHECK_EQ_INT(collector.words, 0);
}

static void test_long_word_grows_buffer(void) {
    char text[1000];
    memset(text, 'w', sizeof text - 1);
    text[sizeof text - 1] = '\0';
    Collector collector;
    feed_in_slices(text, 100, &collector);
    CHECK_EQ_INT(collector.words, 1);
    CHECK_EQ_INT(strlen(collector.joined), 999);
}

static void test_finish_resets_for_reuse(void) {
    Tokenizer tokenizer;
    Collector collector;
    memset(&collector, 0, sizeof collector);
    CHECK_EQ_INT(tokenizer_init(&tokenizer), 0);
    CHECK_EQ_INT(tokenizer_feed(&tokenizer, (const unsigned char *)"one", 3, collect, &collector), 0);
    CHECK_EQ_INT(tokenizer_finish(&tokenizer, collect, &collector), 0);
    CHECK_EQ_INT(tokenizer_feed(&tokenizer, (const unsigned char *)"two", 3, collect, &collector), 0);
    CHECK_EQ_INT(tokenizer_finish(&tokenizer, collect, &collector), 0);
    CHECK_EQ_INT(tokenizer_finish(&tokenizer, collect, &collector), 0);
    CHECK_EQ_STR(collector.joined, "one|two");
    tokenizer_free(&tokenizer);
}

static int refuse(const char *word, size_t length, void *context) {
    (void)word;
    (void)length;
    (void)context;
    return 7;
}

static void test_visitor_error_is_propagated(void) {
    Tokenizer tokenizer;
    CHECK_EQ_INT(tokenizer_init(&tokenizer), 0);
    CHECK_EQ_INT(tokenizer_feed(&tokenizer, (const unsigned char *)"ab cd", 5, refuse, NULL), 7);
    tokenizer_free(&tokenizer);
}

int main(void) {
    test_letter_classification();
    test_spec_example_potato();
    test_boundary_slices();
    test_word_across_exact_chunk_boundary();
    test_empty_and_separator_only_input();
    test_long_word_grows_buffer();
    test_finish_resets_for_reuse();
    test_visitor_error_is_propagated();
    CHECK_REPORT("test_words");
}

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../src/io.h"
#include "../../src/output.h"
#include "../../src/table.h"
#include "check.h"

static void test_digit_count(void) {
    CHECK_EQ_INT(output_digit_count(0), 1);
    CHECK_EQ_INT(output_digit_count(1), 1);
    CHECK_EQ_INT(output_digit_count(9), 1);
    CHECK_EQ_INT(output_digit_count(10), 2);
    CHECK_EQ_INT(output_digit_count(126), 3);
    CHECK_EQ_INT(output_digit_count(1000), 4);
}

static void test_format_line_matches_spec_examples(void) {
    ColumnWidths widths = {12, 3};
    char line[64];
    int length = output_format_line(line, sizeof line, "a", 49, &widths);
    CHECK_EQ_STR(line, "a            :  49\n");
    CHECK_EQ_INT(length, 19);
    output_format_line(line, sizeof line, "respectfully", 1, &widths);
    CHECK_EQ_STR(line, "respectfully :   1\n");
    output_format_line(line, sizeof line, "the", 126, &widths);
    CHECK_EQ_STR(line, "the          : 126\n");
    output_format_line(line, sizeof line, "your", 7, &widths);
    CHECK_EQ_STR(line, "your         :   7\n");
}

static void test_format_line_typed_in_example(void) {
    ColumnWidths widths = {7, 1};
    char line[64];
    output_format_line(line, sizeof line, "and", 1, &widths);
    CHECK_EQ_STR(line, "and     : 1\n");
    output_format_line(line, sizeof line, "control", 1, &widths);
    CHECK_EQ_STR(line, "control : 1\n");
}

static void test_format_line_reports_small_buffer(void) {
    ColumnWidths widths = {12, 3};
    char line[8];
    CHECK_EQ_INT(output_format_line(line, sizeof line, "a", 49, &widths), -1);
}

static void test_measure_finds_longest_word_and_widest_count(void) {
    WordTable table;
    table_init(&table);
    CHECK_EQ_INT(table_add(&table, "a"), 0);
    CHECK_EQ_INT(table_add(&table, "respectfully"), 0);
    for (int repeat = 0; repeat < 126; repeat++) {
        CHECK_EQ_INT(table_add(&table, "the"), 0);
    }
    ColumnWidths widths;
    CHECK_EQ_INT(output_measure(&table, &widths), 0);
    CHECK_EQ_INT(widths.longest_word, 12);
    CHECK_EQ_INT(widths.widest_count, 3);
    table_free(&table);
}

static void test_measure_empty_table(void) {
    WordTable table;
    table_init(&table);
    ColumnWidths widths = {99, 99};
    CHECK_EQ_INT(output_measure(&table, &widths), 0);
    CHECK_EQ_INT(widths.longest_word, 0);
    CHECK_EQ_INT(widths.widest_count, 0);
    table_free(&table);
}

static void test_single_letter_table(void) {
    WordTable table;
    table_init(&table);
    CHECK_EQ_INT(table_add(&table, "i"), 0);
    ColumnWidths widths;
    CHECK_EQ_INT(output_measure(&table, &widths), 0);
    CHECK_EQ_INT(widths.longest_word, 1);
    CHECK_EQ_INT(widths.widest_count, 1);
    char line[16];
    output_format_line(line, sizeof line, "i", 1, &widths);
    CHECK_EQ_STR(line, "i : 1\n");
    table_free(&table);
}

/* Writes the table to a scratch file and returns its contents in out. */
static void write_table_to_string(const WordTable *table, char *out, size_t capacity) {
    char path[4096];
    const char *tmpdir = getenv("TMPDIR");
    snprintf(path, sizeof path, "%s/wordfreak_out_XXXXXX", tmpdir ? tmpdir : "/tmp");
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    Writer writer;
    writer_init(&writer, fd);
    CHECK_EQ_INT(output_write_table(table, &writer), 0);
    CHECK_EQ_INT(writer_flush(&writer), 0);
    CHECK_EQ_INT(io_close(fd), 0);
    int in = io_open_for_reading(path);
    CHECK(in >= 0);
    ssize_t got = io_read_chunk(in, out, capacity - 1);
    CHECK(got >= 0);
    out[got < 0 ? 0 : got] = '\0';
    CHECK_EQ_INT(io_close(in), 0);
    unlink(path);
}

static void test_write_table_spec_potato_example(void) {
    WordTable table;
    table_init(&table);
    const char *words[] = {"isn", "t", "that", "a", "pot", "to"};
    for (size_t index = 0; index < sizeof words / sizeof words[0]; index++) {
        CHECK_EQ_INT(table_add(&table, words[index]), 0);
    }
    char contents[256];
    write_table_to_string(&table, contents, sizeof contents);
    CHECK_EQ_STR(contents, "a    : 1\nisn  : 1\npot  : 1\nt    : 1\nthat : 1\nto   : 1\n");
    table_free(&table);
}

static void test_write_empty_table_writes_nothing(void) {
    WordTable table;
    table_init(&table);
    char contents[16];
    write_table_to_string(&table, contents, sizeof contents);
    CHECK_EQ_STR(contents, "");
    table_free(&table);
}

int main(void) {
    test_digit_count();
    test_format_line_matches_spec_examples();
    test_format_line_typed_in_example();
    test_format_line_reports_small_buffer();
    test_measure_finds_longest_word_and_widest_count();
    test_measure_empty_table();
    test_single_letter_table();
    test_write_table_spec_potato_example();
    test_write_empty_table_writes_nothing();
    CHECK_REPORT("test_output");
}

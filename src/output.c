#include "output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bytes in a line beyond the two columns: " : " and "\n", plus the NUL. */
#define LINE_OVERHEAD 5

/* What one output line carries while the table is being written. */
typedef struct {
    Writer *writer;
    const ColumnWidths *widths;
    char *line;
    size_t capacity;
} LineSink;

/* Number of decimal digits needed to print value (0 needs one). */
size_t output_digit_count(unsigned long value) {
    size_t digits = 1;
    while (value >= 10) {
        value /= 10;
        digits++;
    }
    return digits;
}

/* Visitor: widens the columns to fit one node. */
static int widen_to_fit(const BstNode *node, void *context) {
    ColumnWidths *widths = context;
    size_t length = strlen(node->word);
    if (length > widths->longest_word) {
        widths->longest_word = length;
    }
    size_t digits = output_digit_count(node->count);
    if (digits > widths->widest_count) {
        widths->widest_count = digits;
    }
    return 0;
}

/* Computes the column widths for the whole table; both 0 when it is empty. */
int output_measure(const WordTable *table, ColumnWidths *widths) {
    widths->longest_word = 0;
    widths->widest_count = 0;
    return table_visit_in_order(table, widen_to_fit, widths);
}

/* Formats "[word][pad] : [pad][count]\n" into buffer.
 * @return the line length, or -1 when it does not fit. */
int output_format_line(char *buffer, size_t capacity, const char *word, unsigned long count,
                       const ColumnWidths *widths) {
    int length = snprintf(buffer, capacity, "%-*s : %*lu\n", (int)widths->longest_word, word,
                          (int)widths->widest_count, count);
    if (length < 0 || (size_t)length >= capacity) {
        return -1;
    }
    return length;
}

/* Visitor: formats one node and hands the line to the writer. */
static int write_line(const BstNode *node, void *context) {
    LineSink *sink = context;
    int length = output_format_line(sink->line, sink->capacity, node->word, node->count,
                                    sink->widths);
    if (length < 0) {
        return -1;
    }
    return writer_put(sink->writer, sink->line, (size_t)length);
}

/* Writes every word in the table, alphabetically, with aligned colons.
 * @return 0, or -1 on a write or allocation failure. */
int output_write_table(const WordTable *table, Writer *writer) {
    ColumnWidths widths;
    if (output_measure(table, &widths) != 0) {
        return -1;
    }
    LineSink sink;
    sink.writer = writer;
    sink.widths = &widths;
    sink.capacity = widths.longest_word + widths.widest_count + LINE_OVERHEAD;
    sink.line = malloc(sink.capacity);
    if (sink.line == NULL) {
        return -1;
    }
    int result = table_visit_in_order(table, write_line, &sink);
    free(sink.line);
    return result;
}

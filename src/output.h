#ifndef OUTPUT_H
#define OUTPUT_H

#include <stddef.h>
#include "io.h"
#include "table.h"

/* Column widths that make every colon line up. */
typedef struct {
    size_t longest_word;      /* letters in the longest word: the word column width */
    size_t widest_count;      /* digits in the largest count: the count column width */
} ColumnWidths;

size_t output_digit_count(unsigned long value);
int output_measure(const WordTable *table, ColumnWidths *widths);
int output_format_line(char *buffer, size_t capacity, const char *word, unsigned long count,
                       const ColumnWidths *widths);
int output_write_table(const WordTable *table, Writer *writer);

#endif

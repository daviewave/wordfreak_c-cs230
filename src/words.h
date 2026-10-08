#ifndef WORDS_H
#define WORDS_H

#include <stddef.h>

/* Receives each completed word (NUL-terminated, lowercase, owned by the
 * tokenizer). Returns 0 to continue; any other value aborts the feed. */
typedef int (*WordVisitor)(const char *word, size_t length, void *context);

/* The partially assembled word carried between chunks. */
typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} Tokenizer;

int words_is_letter(int byte);
char words_to_lower(int byte);

int tokenizer_init(Tokenizer *tokenizer);
int tokenizer_feed(Tokenizer *tokenizer, const unsigned char *bytes, size_t count,
                   WordVisitor visit, void *context);
int tokenizer_finish(Tokenizer *tokenizer, WordVisitor visit, void *context);
void tokenizer_free(Tokenizer *tokenizer);

#endif

#include "words.h"

#include <stdlib.h>

/* Initial word buffer; doubles whenever a longer word arrives. */
#define INITIAL_WORD_CAPACITY 32

/* True for ASCII letters only; every other byte separates words. */
int words_is_letter(int byte) {
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
}

/* Maps 'A'..'Z' to 'a'..'z'; other bytes are returned unchanged. */
char words_to_lower(int byte) {
    if (byte >= 'A' && byte <= 'Z') {
        return (char)(byte - 'A' + 'a');
    }
    return (char)byte;
}

/* Allocates the word buffer. Returns 0, or -1 when memory is exhausted. */
int tokenizer_init(Tokenizer *tokenizer) {
    tokenizer->data = malloc(INITIAL_WORD_CAPACITY);
    tokenizer->length = 0;
    tokenizer->capacity = INITIAL_WORD_CAPACITY;
    return tokenizer->data == NULL ? -1 : 0;
}

/* Makes room for one more letter plus the terminator. Returns 0 or -1. */
static int reserve_one_letter(Tokenizer *tokenizer) {
    if (tokenizer->length + 1 < tokenizer->capacity) {
        return 0;
    }
    size_t doubled = tokenizer->capacity * 2;
    char *grown = realloc(tokenizer->data, doubled);
    if (grown == NULL) {
        return -1;
    }
    tokenizer->data = grown;
    tokenizer->capacity = doubled;
    return 0;
}

/* Appends a lowercased letter to the word in progress. Returns 0 or -1. */
static int append_letter(Tokenizer *tokenizer, int byte) {
    if (reserve_one_letter(tokenizer) == -1) {
        return -1;
    }
    tokenizer->data[tokenizer->length++] = words_to_lower(byte);
    return 0;
}

/* Delivers the word in progress, if any, and starts a new one.
 * Returns 0 or the visitor's non-zero result. */
static int emit_pending_word(Tokenizer *tokenizer, WordVisitor visit, void *context) {
    if (tokenizer->length == 0) {
        return 0;
    }
    tokenizer->data[tokenizer->length] = '\0';
    int result = visit(tokenizer->data, tokenizer->length, context);
    tokenizer->length = 0;
    return result;
}

/* Consumes one chunk of input, delivering every word that ends inside it.
 * A word cut by the chunk boundary stays in tokenizer->data until the next
 * chunk or finish completes it (docs/design.md section 5.2).
 * @return 0, the visitor's first non-zero result, or -1 on allocation failure. */
int tokenizer_feed(Tokenizer *tokenizer, const unsigned char *bytes, size_t count,
                   WordVisitor visit, void *context) {
    for (size_t index = 0; index < count; index++) {
        int byte = bytes[index];
        int result = words_is_letter(byte) ? append_letter(tokenizer, byte)
                                           : emit_pending_word(tokenizer, visit, context);
        if (result != 0) {
            return result;
        }
    }
    return 0;
}

/* Delivers the word left over at end of input, if any. */
int tokenizer_finish(Tokenizer *tokenizer, WordVisitor visit, void *context) {
    return emit_pending_word(tokenizer, visit, context);
}

/* Releases the word buffer. */
void tokenizer_free(Tokenizer *tokenizer) {
    free(tokenizer->data);
    tokenizer->data = NULL;
    tokenizer->length = 0;
    tokenizer->capacity = 0;
}

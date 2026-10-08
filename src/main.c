#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "io.h"
#include "output.h"
#include "table.h"
#include "words.h"

/* The spec-mandated name of the result file, in the current directory. */
#define OUTPUT_FILE_NAME "output.txt"
/* Environment variable naming one extra input file. */
#define INPUT_ENVIRONMENT_VARIABLE "WORD_FREAK"
/* Descriptor numbers the program reads from and reports to. */
#define STDIN_DESCRIPTOR 0
#define STDERR_DESCRIPTOR 2
/* Room for "wordfreak: <action> '<path>': <strerror>\n" with a PATH_MAX path. */
#define MESSAGE_CAPACITY 4352

/* Writes "wordfreak: <action> '<subject>': <strerror(errno)>\n" to fd 2. */
static void report_failure(const char *action, const char *subject) {
    char message[MESSAGE_CAPACITY];
    snprintf(message, sizeof message, "wordfreak: %s '%s': %s\n", action, subject,
             strerror(errno));
    io_write_string(STDERR_DESCRIPTOR, message);
}

/* WordVisitor: counts one word in the table (the context). */
static int count_word(const char *word, size_t length, void *context) {
    (void)length;
    return table_add(context, word);
}

/* Feeds chunks from fd into the tokenizer until EOF. A read error is
 * reported and ends this input; the words counted so far are kept.
 * @return 0, or -1 after reporting an allocation failure. */
static int read_words_from_descriptor(int fd, const char *name, Tokenizer *tokenizer,
                                      WordTable *table) {
    unsigned char chunk[IO_CHUNK_SIZE];
    for (;;) {
        ssize_t got = io_read_chunk(fd, chunk, sizeof chunk);
        if (got == -1) {
            report_failure("cannot read", name);
            return 0;
        }
        if (got == 0) {
            break;
        }
        if (tokenizer_feed(tokenizer, chunk, (size_t)got, count_word, table) != 0) {
            report_failure("cannot allocate memory while reading", name);
            return -1;
        }
    }
    if (tokenizer_finish(tokenizer, count_word, table) != 0) {
        report_failure("cannot allocate memory while reading", name);
        return -1;
    }
    return 0;
}

/* Counts the words of one named file; a file that cannot be opened is
 * reported and skipped so the remaining inputs are still processed.
 * @return 0, or -1 when memory ran out (a skipped file is not a failure). */
static int count_file(const char *path, Tokenizer *tokenizer, WordTable *table) {
    int fd = io_open_for_reading(path);
    if (fd == -1) {
        report_failure("cannot open", path);
        return 0;
    }
    int result = read_words_from_descriptor(fd, path, tokenizer, table);
    if (io_close(fd) == -1) {
        report_failure("cannot close", path);
    }
    return result;
}

/* Counts every file named on the command line. */
static int count_argument_files(int argc, char **argv, Tokenizer *tokenizer, WordTable *table) {
    for (int index = 1; index < argc; index++) {
        if (count_file(argv[index], tokenizer, table) == -1) {
            return -1;
        }
    }
    return 0;
}

/* Counts the file named by WORD_FREAK when the variable is set and non-empty. */
static int count_environment_file(Tokenizer *tokenizer, WordTable *table) {
    const char *path = getenv(INPUT_ENVIRONMENT_VARIABLE);
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    return count_file(path, tokenizer, table);
}

/* Reads standard input (always), then every argument file, then the
 * environment file, into one shared table. */
static int count_all_inputs(int argc, char **argv, Tokenizer *tokenizer, WordTable *table) {
    if (read_words_from_descriptor(STDIN_DESCRIPTOR, "standard input", tokenizer, table) == -1) {
        return -1;
    }
    if (count_argument_files(argc, argv, tokenizer, table) == -1) {
        return -1;
    }
    return count_environment_file(tokenizer, table);
}

/* Creates or truncates output.txt and writes the table into it.
 * @return 0, or -1 after reporting the failure. */
static int write_output_file(const WordTable *table) {
    int fd = io_open_output_file(OUTPUT_FILE_NAME);
    if (fd == -1) {
        report_failure("cannot create", OUTPUT_FILE_NAME);
        return -1;
    }
    Writer writer;
    writer_init(&writer, fd);
    int result = output_write_table(table, &writer);
    if (result == 0) {
        result = writer_flush(&writer);
    }
    if (io_close(fd) == -1) {
        result = -1;
    }
    if (result == -1) {
        report_failure("cannot write", OUTPUT_FILE_NAME);
    }
    return result;
}

/* Counts words from every input source and writes output.txt. */
int main(int argc, char **argv) {
    WordTable table;
    table_init(&table);
    Tokenizer tokenizer;
    if (tokenizer_init(&tokenizer) == -1) {
        report_failure("cannot allocate", "word buffer");
        return EXIT_FAILURE;
    }
    int status = count_all_inputs(argc, argv, &tokenizer, &table);
    if (status == 0) {
        status = write_output_file(&table);
    }
    tokenizer_free(&tokenizer);
    table_free(&table);
    return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

# wordfreak Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `wordfreak`, a word-frequency counter that reads stdin, argv files and the `WORD_FREAK` file through POSIX `open`/`read`/`write`/`close` only, counts lowercase ASCII words in 26 binary search trees, and writes an aligned `output.txt`.

**Architecture:** Six small C modules (`io`, `words`, `bst`, `table`, `output`, `main`) with the interfaces fixed in `docs/design.md` section 4. Each module is a pure, unit-tested unit; `main.c` is plumbing only. One module per task, test first, one commit per task.

**Tech Stack:** C99 with `-D_POSIX_C_SOURCE=200809L`, gcc 16, GNU make, bash e2e scripts, the `test/unit/check.h` harness. No sanitizers or valgrind on this machine; `gcc -fanalyzer` is the static check.

**Spec:** `docs/spec.md` (requirements and rubric), `docs/design.md` (interfaces and decisions), `docs/conventions.md` (repository contract).

## Global Constraints

- Executable name: `wordfreak` (built at `build/wordfreak`; `dist/wordfreak` from the flat bundle).
- I/O only through `open()`, `close()`, `read()`, `write()`, `lseek()`; `sprintf`/`snprintf` allowed. No `printf`, `fprintf`, `puts`, `fopen`, `fgets`, `fread`, `fwrite`, `perror`, `getchar`, `scanf` anywhere in `src/`.
- Inputs: stdin always; every `argv[i]`; the file named by env `WORD_FREAK`. A file that fails to open is reported on fd 2 with `write()` and skipped.
- Word = maximal run of ASCII letters, lowercased. `"POT4TO???"` gives `pot`, `to`; `"isn’t"` gives `isn`, `t`.
- 26 BSTs selected by `first_letter - 'a'`.
- `output.txt` opened `O_WRONLY|O_CREAT|O_TRUNC`, mode `0644`; lines `[word][pad] : [pad][count]\n`, colons aligned; `a            :  49` with longest word 12 letters and widest count 3 digits.
- At most 5 global variables; this design has 0.
- Compile flags: `-std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wconversion -Wvla -Werror`; `make check` (`-fanalyzer`) clean.
- Every function in `src/` has a header comment (purpose; `@param`/`@return` only when not obvious). No narration comments in bodies.
- Commit subjects are conventional (`feat:`, `test:`, `docs:`, `build:`); never push; no `Co-Authored-By` trailers.

## Review Focus

1. A word straddling a 4096-byte `read` boundary must be counted as one word, not two: pinned in Task 2 (`test_boundary_slices`) and Task 6 (`long_input` e2e case).
2. A terminal delivers one line per `read` and `^D` ends input: the reader must not assume full chunks: pinned in Task 1 (`test_read_chunk_short_reads` via a pipe written in pieces).
3. A missing argv file must produce a message on fd 2 and the run must still write `output.txt` from the other inputs with exit 0: Task 6 (`missing_file` e2e case).
4. Empty input must produce an empty, existing `output.txt` that replaces any previous content: Task 6 (`empty_input` e2e case, which pre-seeds a stale `output.txt`).
5. A single-letter word and a count of 0 digits edge (`output_digit_count(0) == 1`), and widths when the longest word is 1 letter: Task 5 (`test_digit_count`, `test_single_letter_table`).

---

### Task 0: Scaffold (done by the project agent before sub-agents start)

**Files:**
- Create: `.gitignore`, `Makefile`, `test/unit/check.h`, `test/run_tests.sh`, `README.txt`, `docs/conventions.md`, `docs/spec.md`, `docs/design.md`, `docs/plan.md`, `docs/research.md`

- [x] `git init`, add the files above, `git commit -m "build: scaffold repository layout, Makefile and test harness"`.

The Makefile contract every task relies on:

- `make` / `make all` builds `build/wordfreak` (`-O2`).
- `make debug` builds `build/debug/wordfreak` (`-O0 -g3`).
- `make test` builds, then runs `test/run_tests.sh`, which compiles every `test/unit/test_*.c` against all `src/*.c` except `main.c` into `build/test/`, runs each, runs every `test/e2e/*.sh`, and exits non-zero on any failure.
- `make check` runs `gcc -fanalyzer` on each `src/*.c`.
- `make run` builds and runs `./build/wordfreak` on the e2e sample files.
- `make dist` builds the flat bundle in `dist/` and proves it compiles.
- `make clean` removes `build/`, `dist/`, `output.txt`.

Unit tests compile with the same warning set, so every test function must be `static` (or the test file fails `-Wmissing-prototypes`).

---

### Task 1: io module (the only file that calls the system calls)

**Files:**
- Create: `src/io.h`, `src/io.c`
- Test: `test/unit/test_io.c`

**Interfaces:**
- Consumes: nothing.
- Produces: everything in `docs/design.md` section 4 "io.h": `IO_CHUNK_SIZE`, `Writer`, `io_open_for_reading`, `io_open_output_file`, `io_close`, `io_read_chunk`, `io_write_all`, `io_write_string`, `writer_init`, `writer_put`, `writer_flush`.

- [ ] **Step 1: Write the failing test**

`test/unit/test_io.c`:

```c
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../src/io.h"
#include "check.h"

/* Builds "$TMPDIR/wordfreak_io_XXXXXX" and creates it with mkstemp. */
static int make_scratch_file(char *path, size_t capacity) {
    const char *tmpdir = getenv("TMPDIR");
    if (tmpdir == NULL) {
        tmpdir = "/tmp";
    }
    snprintf(path, capacity, "%s/wordfreak_io_XXXXXX", tmpdir);
    return mkstemp(path);
}

static void test_write_all_then_read_chunk_round_trip(void) {
    char path[4096];
    int fd = make_scratch_file(path, sizeof path);
    CHECK(fd >= 0);
    CHECK_EQ_INT(io_write_all(fd, "hello world", 11), 0);
    CHECK_EQ_INT(io_close(fd), 0);

    int in = io_open_for_reading(path);
    CHECK(in >= 0);
    char buffer[IO_CHUNK_SIZE];
    CHECK_EQ_INT(io_read_chunk(in, buffer, sizeof buffer), 11);
    CHECK_EQ_INT(memcmp(buffer, "hello world", 11), 0);
    CHECK_EQ_INT(io_read_chunk(in, buffer, sizeof buffer), 0);
    CHECK_EQ_INT(io_close(in), 0);
    unlink(path);
}

static void test_open_for_reading_missing_file_sets_errno(void) {
    errno = 0;
    CHECK_EQ_INT(io_open_for_reading("/nonexistent/wordfreak/none.txt"), -1);
    CHECK_EQ_INT(errno, ENOENT);
}

static void test_open_output_file_truncates_and_sets_mode(void) {
    char path[4096];
    int fd = make_scratch_file(path, sizeof path);
    CHECK(fd >= 0);
    CHECK_EQ_INT(io_write_all(fd, "stale contents", 14), 0);
    CHECK_EQ_INT(io_close(fd), 0);

    int out = io_open_output_file(path);
    CHECK(out >= 0);
    CHECK_EQ_INT(io_close(out), 0);
    struct stat info;
    CHECK_EQ_INT(stat(path, &info), 0);
    CHECK_EQ_INT(info.st_size, 0);
    unlink(path);

    char fresh[4096];
    snprintf(fresh, sizeof fresh, "%s.fresh", path);
    umask(0);
    out = io_open_output_file(fresh);
    CHECK(out >= 0);
    CHECK_EQ_INT(io_close(out), 0);
    CHECK_EQ_INT(stat(fresh, &info), 0);
    CHECK_EQ_INT(info.st_mode & 0777, 0644);
    unlink(fresh);
}

static void test_read_chunk_short_reads(void) {
    int pipe_fds[2];
    CHECK_EQ_INT(pipe(pipe_fds), 0);
    CHECK_EQ_INT(io_write_all(pipe_fds[1], "abc", 3), 0);
    char buffer[IO_CHUNK_SIZE];
    CHECK_EQ_INT(io_read_chunk(pipe_fds[0], buffer, sizeof buffer), 3);
    CHECK_EQ_INT(io_write_all(pipe_fds[1], "defgh", 5), 0);
    CHECK_EQ_INT(io_close(pipe_fds[1]), 0);
    CHECK_EQ_INT(io_read_chunk(pipe_fds[0], buffer, sizeof buffer), 5);
    CHECK_EQ_INT(memcmp(buffer, "defgh", 5), 0);
    CHECK_EQ_INT(io_read_chunk(pipe_fds[0], buffer, sizeof buffer), 0);
    CHECK_EQ_INT(io_close(pipe_fds[0]), 0);
}

static void test_read_chunk_reports_error(void) {
    char buffer[8];
    CHECK_EQ_INT(io_read_chunk(-1, buffer, sizeof buffer), -1);
    CHECK_EQ_INT(io_write_all(-1, "x", 1), -1);
}

static void test_writer_buffers_and_flushes(void) {
    char path[4096];
    int fd = make_scratch_file(path, sizeof path);
    CHECK(fd >= 0);
    Writer writer;
    writer_init(&writer, fd);
    CHECK_EQ_INT(writer_put(&writer, "ab", 2), 0);
    struct stat info;
    CHECK_EQ_INT(stat(path, &info), 0);
    CHECK_EQ_INT(info.st_size, 0);
    CHECK_EQ_INT(writer_flush(&writer), 0);
    CHECK_EQ_INT(stat(path, &info), 0);
    CHECK_EQ_INT(info.st_size, 2);
    CHECK_EQ_INT(io_close(fd), 0);
    unlink(path);
}

static void test_writer_handles_more_than_one_chunk(void) {
    char path[4096];
    int fd = make_scratch_file(path, sizeof path);
    CHECK(fd >= 0);
    Writer writer;
    writer_init(&writer, fd);
    char block[IO_CHUNK_SIZE + 100];
    memset(block, 'x', sizeof block);
    CHECK_EQ_INT(writer_put(&writer, block, 3000), 0);
    CHECK_EQ_INT(writer_put(&writer, block, 3000), 0);
    CHECK_EQ_INT(writer_put(&writer, block, sizeof block), 0);
    CHECK_EQ_INT(writer_flush(&writer), 0);
    struct stat info;
    CHECK_EQ_INT(stat(path, &info), 0);
    CHECK_EQ_INT(info.st_size, 6000 + IO_CHUNK_SIZE + 100);
    CHECK_EQ_INT(io_close(fd), 0);
    unlink(path);
}

static void test_write_string_to_closed_fd_does_not_crash(void) {
    io_write_string(-1, "ignored\n");
    CHECK(1);
}

int main(void) {
    test_write_all_then_read_chunk_round_trip();
    test_open_for_reading_missing_file_sets_errno();
    test_open_output_file_truncates_and_sets_mode();
    test_read_chunk_short_reads();
    test_read_chunk_reports_error();
    test_writer_buffers_and_flushes();
    test_writer_handles_more_than_one_chunk();
    test_write_string_to_closed_fd_does_not_crash();
    CHECK_REPORT("test_io");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test`
Expected: FAIL (the compile of `test_io` fails with "io.h: No such file").

- [ ] **Step 3: Write minimal implementation**

`src/io.h`:

```c
#ifndef IO_H
#define IO_H

#include <stddef.h>
#include <sys/types.h>

/* Bytes moved per read() and buffered per write(): one page, one disk block. */
#define IO_CHUNK_SIZE 4096

/* Output buffer in front of one file descriptor. */
typedef struct {
    int fd;
    char data[IO_CHUNK_SIZE];
    size_t length;
} Writer;

int io_open_for_reading(const char *path);
int io_open_output_file(const char *path);
int io_close(int fd);
ssize_t io_read_chunk(int fd, void *buffer, size_t capacity);
int io_write_all(int fd, const void *bytes, size_t count);
void io_write_string(int fd, const char *text);

void writer_init(Writer *writer, int fd);
int writer_put(Writer *writer, const char *bytes, size_t count);
int writer_flush(Writer *writer);

#endif
```

`src/io.c`:

```c
#include "io.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Permission bits for a created output.txt: owner rw, everyone else r. */
#define OUTPUT_FILE_MODE 0644

/* Opens an input file read-only. Returns the descriptor or -1 (errno set). */
int io_open_for_reading(const char *path) {
    return open(path, O_RDONLY);
}

/* Creates or truncates the output file, readable by all. Returns fd or -1. */
int io_open_output_file(const char *path) {
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, OUTPUT_FILE_MODE);
}

/* Closes a descriptor. Returns 0, or -1 with errno set. */
int io_close(int fd) {
    return close(fd);
}

/* Reads up to capacity bytes, retrying when a signal interrupts the call.
 * @return bytes read, 0 at end of file, -1 on error (errno set). */
ssize_t io_read_chunk(int fd, void *buffer, size_t capacity) {
    ssize_t got;
    do {
        got = read(fd, buffer, capacity);
    } while (got == -1 && errno == EINTR);
    return got;
}

/* Writes every byte, continuing after short writes and EINTR.
 * @return 0 when all bytes were written, -1 on error (errno set). */
int io_write_all(int fd, const void *bytes, size_t count) {
    const char *cursor = bytes;
    size_t remaining = count;
    while (remaining > 0) {
        ssize_t written = write(fd, cursor, remaining);
        if (written == -1 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return -1;
        }
        cursor += written;
        remaining -= (size_t)written;
    }
    return 0;
}

/* Writes a NUL-terminated string; failures are ignored (used for fd 2). */
void io_write_string(int fd, const char *text) {
    (void)io_write_all(fd, text, strlen(text));
}

/* Prepares an empty writer in front of fd. */
void writer_init(Writer *writer, int fd) {
    writer->fd = fd;
    writer->length = 0;
}

/* Hands the buffered bytes to write(). Returns 0 or -1. */
int writer_flush(Writer *writer) {
    if (writer->length == 0) {
        return 0;
    }
    if (io_write_all(writer->fd, writer->data, writer->length) == -1) {
        return -1;
    }
    writer->length = 0;
    return 0;
}

/* Appends bytes to the buffer, flushing when it fills; a block larger than
 * the buffer bypasses it. Returns 0 or -1. */
int writer_put(Writer *writer, const char *bytes, size_t count) {
    if (count > IO_CHUNK_SIZE - writer->length) {
        if (writer_flush(writer) == -1) {
            return -1;
        }
    }
    if (count > IO_CHUNK_SIZE) {
        return io_write_all(writer->fd, bytes, count);
    }
    memcpy(writer->data + writer->length, bytes, count);
    writer->length += count;
    return 0;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test`
Expected: `PASS test_io` with 0 failures; `make check` clean.

- [ ] **Step 5: Commit**

```bash
git add src/io.h src/io.c test/unit/test_io.c
git commit -m "feat: add io module wrapping open, read, write and close"
```

---

### Task 2: words module (tokeniser)

**Files:**
- Create: `src/words.h`, `src/words.c`
- Test: `test/unit/test_words.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `WordVisitor`, `Tokenizer`, `words_is_letter`, `words_to_lower`, `tokenizer_init`, `tokenizer_feed`, `tokenizer_finish`, `tokenizer_free` as in `docs/design.md` section 4 "words.h".

- [ ] **Step 1: Write the failing test**

`test/unit/test_words.c`:

```c
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
    CHECK_EQ_STR(collector.joined, "straddlin");
    text[sizeof text - 1] = 'g';
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test`
Expected: FAIL compiling `test_words` ("words.h: No such file").

- [ ] **Step 3: Write minimal implementation**

`src/words.h`:

```c
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
```

`src/words.c`:

```c
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
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test`
Expected: `PASS test_words`; `make check` clean.

- [ ] **Step 5: Commit**

```bash
git add src/words.h src/words.c test/unit/test_words.c
git commit -m "feat: add tokenizer that assembles words across read chunks"
```

---

### Task 3: bst module

**Files:**
- Create: `src/bst.h`, `src/bst.c`
- Test: `test/unit/test_bst.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `BstNode`, `BstVisitor`, `bst_insert`, `bst_find`, `bst_visit_in_order`, `bst_size`, `bst_free` as in `docs/design.md` section 4 "bst.h".

- [ ] **Step 1: Write the failing test**

`test/unit/test_bst.c`:

```c
#include <string.h>
#include "../../src/bst.h"
#include "check.h"

typedef struct {
    char joined[4096];
    unsigned long counts[64];
    int visited;
} Walk;

static int record(const BstNode *node, void *context) {
    Walk *walk = context;
    if (walk->visited > 0) {
        strcat(walk->joined, " ");
    }
    strcat(walk->joined, node->word);
    walk->counts[walk->visited] = node->count;
    walk->visited++;
    return 0;
}

static void test_insert_counts_duplicates(void) {
    BstNode *root = NULL;
    CHECK_EQ_INT(bst_insert(&root, "the"), 0);
    CHECK_EQ_INT(bst_insert(&root, "the"), 0);
    CHECK_EQ_INT(bst_insert(&root, "then"), 0);
    CHECK_EQ_INT(bst_size(root), 2);
    CHECK(bst_find(root, "the") != NULL);
    CHECK_EQ_INT(bst_find(root, "the")->count, 2);
    CHECK_EQ_INT(bst_find(root, "then")->count, 1);
    CHECK(bst_find(root, "th") == NULL);
    CHECK(bst_find(NULL, "the") == NULL);
    bst_free(root);
}

static void test_in_order_is_alphabetical(void) {
    BstNode *root = NULL;
    const char *words[] = {"mango", "apple", "zebra", "banana", "apple", "cherry", "mango"};
    for (size_t index = 0; index < sizeof words / sizeof words[0]; index++) {
        CHECK_EQ_INT(bst_insert(&root, words[index]), 0);
    }
    Walk walk;
    memset(&walk, 0, sizeof walk);
    CHECK_EQ_INT(bst_visit_in_order(root, record, &walk), 0);
    CHECK_EQ_STR(walk.joined, "apple banana cherry mango zebra");
    CHECK_EQ_INT(walk.counts[0], 2);
    CHECK_EQ_INT(walk.counts[3], 2);
    CHECK_EQ_INT(walk.visited, 5);
    bst_free(root);
}

static void test_sorted_insert_builds_degenerate_tree_fine(void) {
    BstNode *root = NULL;
    char word[16];
    for (int number = 0; number < 2000; number++) {
        snprintf(word, sizeof word, "w%05d", number);
        CHECK_EQ_INT(bst_insert(&root, word), 0);
    }
    CHECK_EQ_INT(bst_size(root), 2000);
    CHECK(root->left == NULL);
    bst_free(root);
}

static void test_inserted_word_is_copied(void) {
    BstNode *root = NULL;
    char scratch[8];
    strcpy(scratch, "copy");
    CHECK_EQ_INT(bst_insert(&root, scratch), 0);
    strcpy(scratch, "junk");
    CHECK(bst_find(root, "copy") != NULL);
    CHECK_EQ_STR(root->word, "copy");
    bst_free(root);
}

static int stop_at_second(const BstNode *node, void *context) {
    int *seen = context;
    (void)node;
    (*seen)++;
    return *seen == 2 ? 5 : 0;
}

static void test_visitor_can_abort(void) {
    BstNode *root = NULL;
    CHECK_EQ_INT(bst_insert(&root, "b"), 0);
    CHECK_EQ_INT(bst_insert(&root, "a"), 0);
    CHECK_EQ_INT(bst_insert(&root, "c"), 0);
    int seen = 0;
    CHECK_EQ_INT(bst_visit_in_order(root, stop_at_second, &seen), 5);
    CHECK_EQ_INT(seen, 2);
    bst_free(root);
}

static void test_empty_tree(void) {
    Walk walk;
    memset(&walk, 0, sizeof walk);
    CHECK_EQ_INT(bst_visit_in_order(NULL, record, &walk), 0);
    CHECK_EQ_INT(walk.visited, 0);
    CHECK_EQ_INT(bst_size(NULL), 0);
    bst_free(NULL);
}

int main(void) {
    test_insert_counts_duplicates();
    test_in_order_is_alphabetical();
    test_sorted_insert_builds_degenerate_tree_fine();
    test_inserted_word_is_copied();
    test_visitor_can_abort();
    test_empty_tree();
    CHECK_REPORT("test_bst");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test`
Expected: FAIL compiling `test_bst`.

- [ ] **Step 3: Write minimal implementation**

`src/bst.h`:

```c
#ifndef BST_H
#define BST_H

#include <stddef.h>

/* One distinct word and how many times it was seen. */
typedef struct BstNode {
    char *word;
    unsigned long count;
    struct BstNode *left;
    struct BstNode *right;
} BstNode;

/* Called once per node in sorted order. Returns 0 to continue. */
typedef int (*BstVisitor)(const BstNode *node, void *context);

int bst_insert(BstNode **root, const char *word);
const BstNode *bst_find(const BstNode *root, const char *word);
int bst_visit_in_order(const BstNode *root, BstVisitor visit, void *context);
size_t bst_size(const BstNode *root);
void bst_free(BstNode *root);

#endif
```

`src/bst.c`:

```c
#include "bst.h"

#include <stdlib.h>
#include <string.h>

/* Allocates a node holding its own copy of word with count 1, or NULL. */
static BstNode *make_node(const char *word) {
    BstNode *node = malloc(sizeof *node);
    if (node == NULL) {
        return NULL;
    }
    size_t size = strlen(word) + 1;
    node->word = malloc(size);
    if (node->word == NULL) {
        free(node);
        return NULL;
    }
    memcpy(node->word, word, size);
    node->count = 1;
    node->left = NULL;
    node->right = NULL;
    return node;
}

/* Walks from link to the link where word is or would be stored. */
static BstNode **find_link(BstNode **link, const char *word) {
    while (*link != NULL) {
        int order = strcmp(word, (*link)->word);
        if (order == 0) {
            return link;
        }
        link = order < 0 ? &(*link)->left : &(*link)->right;
    }
    return link;
}

/* Counts one occurrence of word: increments an existing node or inserts a
 * new one. Iterative, so sorted input cannot exhaust the stack.
 * @return 0, or -1 when memory is exhausted. */
int bst_insert(BstNode **root, const char *word) {
    BstNode **link = find_link(root, word);
    if (*link != NULL) {
        (*link)->count++;
        return 0;
    }
    *link = make_node(word);
    return *link == NULL ? -1 : 0;
}

/* Returns the node for word, or NULL when it is not in the tree. */
const BstNode *bst_find(const BstNode *root, const char *word) {
    BstNode *mutable_root = (BstNode *)root;
    return *find_link(&mutable_root, word);
}

/* Visits every node in sorted order. Returns 0, or the visitor's first
 * non-zero result, which stops the walk. */
int bst_visit_in_order(const BstNode *root, BstVisitor visit, void *context) {
    if (root == NULL) {
        return 0;
    }
    int result = bst_visit_in_order(root->left, visit, context);
    if (result != 0) {
        return result;
    }
    result = visit(root, context);
    if (result != 0) {
        return result;
    }
    return bst_visit_in_order(root->right, visit, context);
}

/* Number of distinct words in the tree. */
size_t bst_size(const BstNode *root) {
    if (root == NULL) {
        return 0;
    }
    return 1 + bst_size(root->left) + bst_size(root->right);
}

/* Releases every node and word in the tree (post-order). */
void bst_free(BstNode *root) {
    if (root == NULL) {
        return;
    }
    bst_free(root->left);
    bst_free(root->right);
    free(root->word);
    free(root);
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test`
Expected: `PASS test_bst`; `make check` clean.

- [ ] **Step 5: Commit**

```bash
git add src/bst.h src/bst.c test/unit/test_bst.c
git commit -m "feat: add binary search tree with insert-or-increment and in-order visit"
```

---

### Task 4: table module (26 trees and the hash function)

**Files:**
- Create: `src/table.h`, `src/table.c`
- Test: `test/unit/test_table.c`

**Interfaces:**
- Consumes: `bst.h` (`BstNode`, `BstVisitor`, `bst_insert`, `bst_visit_in_order`, `bst_free`).
- Produces: `TABLE_LETTER_COUNT`, `WordTable`, `table_init`, `table_hash`, `table_add`, `table_visit_in_order`, `table_free` as in `docs/design.md` section 4 "table.h".

- [ ] **Step 1: Write the failing test**

`test/unit/test_table.c`:

```c
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test`
Expected: FAIL compiling `test_table`.

- [ ] **Step 3: Write minimal implementation**

`src/table.h`:

```c
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
```

`src/table.c`:

```c
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
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test`
Expected: `PASS test_table`; `make check` clean.

- [ ] **Step 5: Commit**

```bash
git add src/table.h src/table.c test/unit/test_table.c
git commit -m "feat: add 26-tree word table keyed by first letter"
```

---

### Task 5: output module (widths and line format)

**Files:**
- Create: `src/output.h`, `src/output.c`
- Test: `test/unit/test_output.c`

**Interfaces:**
- Consumes: `table.h` (`WordTable`, `table_visit_in_order`, `BstNode`), `io.h` (`Writer`, `writer_put`).
- Produces: `ColumnWidths`, `output_digit_count`, `output_measure`, `output_format_line`, `output_write_table` as in `docs/design.md` section 4 "output.h".

- [ ] **Step 1: Write the failing test**

`test/unit/test_output.c`:

```c
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test`
Expected: FAIL compiling `test_output`.

- [ ] **Step 3: Write minimal implementation**

`src/output.h`:

```c
#ifndef OUTPUT_H
#define OUTPUT_H

#include <stddef.h>
#include "io.h"
#include "table.h"

/* Column widths that make every colon line up. */
typedef struct {
    size_t longest_word;
    size_t widest_count;
} ColumnWidths;

size_t output_digit_count(unsigned long value);
int output_measure(const WordTable *table, ColumnWidths *widths);
int output_format_line(char *buffer, size_t capacity, const char *word, unsigned long count,
                       const ColumnWidths *widths);
int output_write_table(const WordTable *table, Writer *writer);

#endif
```

`src/output.c`:

```c
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
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test`
Expected: `PASS test_output`; `make check` clean.

- [ ] **Step 5: Commit**

```bash
git add src/output.h src/output.c test/unit/test_output.c
git commit -m "feat: add aligned output formatting for the word table"
```

---

### Task 6: main.c plumbing and the end-to-end suite

**Files:**
- Create: `src/main.c`, `test/e2e/run_cases.sh`, `test/e2e/no_stdio.sh`, `test/e2e/cases/*.txt` (sample inputs), `test/e2e/cases/*.cmd`, `test/e2e/cases/*.expected`, `test/e2e/cases/*.stderr` (optional grep patterns)

**Interfaces:**
- Consumes: every header from Tasks 1-5.
- Produces: the `wordfreak` binary's behaviour.

- [ ] **Step 1: Write the failing e2e tests**

`test/e2e/run_cases.sh` (bash, `set -euo pipefail`): for every `test/e2e/cases/<name>.cmd`, create a fresh scratch directory under `$TMPDIR`, copy `test/e2e/cases/*.txt` into it, `cd` there, run the command line from the `.cmd` file with `WORDFREAK` set to the absolute path of `build/wordfreak` under `timeout 10`, then `diff` `output.txt` against `test/e2e/cases/<name>.expected`; if `<name>.stderr` exists, `grep -q -f` that pattern file in the captured stderr; if `<name>.status` exists, compare the exit status with its content (default 0). Print `PASS <name>` / `FAIL <name>` and exit non-zero on any failure.

Sample inputs (write them by hand; short):

- `fable.txt`: four or five sentences of a short invented fable, with capitals, commas, a hyphenated word, a number and an exclamation.
- `second.txt`: three sentences sharing some words with `fable.txt` so counts add up across files.
- `punct.txt`: `Isn’t that a POT4TO???` with the UTF-8 curly apostrophe (bytes `E2 80 99`) plus a line of `123 456 --- ***`.
- `empty.txt`: zero bytes.

Cases (each `<name>.cmd` is a one-line shell command; `$WORDFREAK` is the binary):

| name | .cmd | expected |
| ---- | ---- | -------- |
| `stdin_piped` | `cat fable.txt \| $WORDFREAK` | counts of fable.txt |
| `stdin_herestring` | `$WORDFREAK <<< "I can write words here,"$'\n'"and end the file with control plus d"` | the spec's typed-in listing, exactly |
| `spec_potato` | `echo "Isn’t that a POT4TO???" \| $WORDFREAK` | `a    : 1`, `isn  : 1`, `pot  : 1`, `t    : 1`, `that : 1`, `to   : 1` |
| `argv_files` | `echo "" \| $WORDFREAK fable.txt second.txt` | combined counts |
| `env_file` | `echo "" \| WORD_FREAK=fable.txt $WORDFREAK` | counts of fable.txt |
| `all_at_once` | `cat second.txt \| WORD_FREAK=punct.txt $WORDFREAK fable.txt` | combined counts of all three |
| `missing_file` | `echo "" \| $WORDFREAK nope.txt fable.txt` | counts of fable.txt; `.stderr` contains `cannot open 'nope.txt'` |
| `empty_input` | `: > output.txt; echo stale >> output.txt; $WORDFREAK < empty.txt` | empty file |
| `env_missing` | `echo "" \| WORD_FREAK=absent.txt $WORDFREAK fable.txt` | counts of fable.txt; stderr mentions `absent.txt` |
| `long_input` | `python3 -I -c 'import sys; sys.stdout.write(("x"*4094 + " ab" + "cd"*2 + " ")*3)' \| $WORDFREAK` | `abcdcd : 3` and the x word `: 3`, with the 4094-x line padded correctly |

Compute every `.expected` by hand from the sample texts (not by running the program) and format the columns by the rules in `docs/design.md` section 5.4.

`test/e2e/no_stdio.sh`: `grep -Ewn 'printf|fprintf|vprintf|vfprintf|dprintf|puts|fputs|putchar|putc|fputc|fopen|fdopen|freopen|fclose|fgets|gets|getc|fgetc|getchar|fread|fwrite|fflush|fseek|ftell|rewind|perror|scanf|fscanf|sscanf' src/` must find nothing (`-w` keeps `snprintf` from matching `printf`); also `grep -rn 'stdin\|stdout\|stderr' src/` must find nothing (fd numbers 0/1/2 are used instead). Exit non-zero when a match is found and print it.

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test`
Expected: the e2e cases FAIL because `build/wordfreak` does not exist (the build itself fails with no `main`).

- [ ] **Step 3: Write minimal implementation**

`src/main.c`:

```c
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
/* Room for "wordfreak: <action> '<path>': <strerror>\n". */
#define MESSAGE_CAPACITY 4352

/* Writes "wordfreak: <action> '<subject>': <reason>\n" to fd 2. */
static void report_failure(const char *action, const char *subject) {
    char message[MESSAGE_CAPACITY];
    snprintf(message, sizeof message, "wordfreak: %s '%s': %s\n", action, subject,
             strerror(errno));
    io_write_string(STDERR_DESCRIPTOR, message);
}

/* WordVisitor: counts one word in the table. */
static int count_word(const char *word, size_t length, void *context) {
    (void)length;
    return table_add(context, word);
}

/* Feeds chunks from fd into the tokenizer until EOF or a read error.
 * @return 0, or -1 after reporting a read failure (words so far are kept). */
static int read_words_from_descriptor(int fd, const char *name, Tokenizer *tokenizer,
                                      WordTable *table) {
    unsigned char chunk[IO_CHUNK_SIZE];
    for (;;) {
        ssize_t got = io_read_chunk(fd, chunk, sizeof chunk);
        if (got == -1) {
            report_failure("cannot read", name);
            return -1;
        }
        if (got == 0) {
            return tokenizer_finish(tokenizer, count_word, table);
        }
        if (tokenizer_feed(tokenizer, chunk, (size_t)got, count_word, table) != 0) {
            return -1;
        }
    }
}

/* Counts the words of one named file; a file that cannot be opened is
 * reported and skipped. Returns 0 or -1 (the latter only on memory exhaustion). */
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

/* Reads standard input, every argument file and the environment file. */
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
```

Note on `tokenizer_feed` returning non-zero: `table_add` returns `-1` only when memory is exhausted (the tokeniser never emits a word that fails `is_hashable`), so a non-zero result from the feed is a fatal allocation failure; `main` reports via `report_failure("cannot allocate", ...)` where that path is reachable (add one `report_failure` call in `read_words_from_descriptor` for the feed failure branch, with `errno` set by `malloc`).

- [ ] **Step 4: Run tests to verify they pass**

Run: `make clean && make && make check && make test`
Expected: every unit test and e2e case prints `PASS`; `make check` clean. Also run `make dist` and confirm `dist/wordfreak` exists.

- [ ] **Step 5: Commit**

```bash
git add src/main.c test/e2e
git commit -m "feat: add wordfreak entry point and end-to-end tests"
```

---

### Task 7: README requirements map and final gate

**Files:**
- Modify: `README.txt`

- [ ] **Step 1: Run the gate**

Run: `make clean && make && make check && make test && make dist`
Expected: all succeed.

- [ ] **Step 2: Independent review**

A reviewer who has read `docs/spec.md` reads every file in `src/` and lists every unmet rubric item, and greps `src/` for stdio I/O calls. Fix each finding, re-run the gate.

- [ ] **Step 3: Finish README.txt**

Sections, in order: overview paragraph; build and run (`make`, `./build/wordfreak`, the four input styles from the spec); "Requirements map" with one line per rubric bullet naming file and function (BSTs: `src/bst.c`; hash function: `src/table.c:table_hash`; syscalls: `src/io.c`; stdin/argv/env: `src/main.c:count_all_inputs`; format: `src/output.c:output_format_line`; error checking: `src/main.c:count_file`, `report_failure`; globals: none); design notes; `Video: <VIDEO URL TO BE ADDED>`.

- [ ] **Step 4: Commit**

```bash
git add README.txt
git commit -m "docs: complete README requirements map"
```

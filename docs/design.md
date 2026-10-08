# wordfreak design

This document records the data structures, the algorithms and every decision a
reviewer could question, with the reason. The spec is `docs/spec.md`; the
repository contract is `docs/conventions.md`; the sources consulted are in
`docs/research.md`.

## 1. What the program does

`wordfreak` reads bytes from standard input (always), from every file named on
the command line, and from the file named by the `WORD_FREAK` environment
variable (when set). It splits the bytes into words (maximal runs of ASCII
letters, lowercased), counts each distinct word across all inputs, and writes
`output.txt` in the current directory with one `word : count` line per distinct
word in alphabetical order, columns aligned.

All I/O goes through `open`, `close`, `read` and `write` (`lseek` is permitted
but not needed). No stdio I/O function is called anywhere in `src/`; only
`snprintf` (formatting into memory) is used from `<stdio.h>`.

## 2. Module map

| File | Responsibility | Depends on |
| ---- | -------------- | ---------- |
| `src/io.c/.h` | The only file that calls `open`, `close`, `read`, `write`. Chunked reader, buffered writer with a write-all loop, error reporting on fd 2. | libc |
| `src/words.c/.h` | Tokeniser: turns a byte stream, delivered in arbitrary chunks, into lowercased words delivered to a visitor callback. Carries a growable word buffer across chunks. | libc |
| `src/bst.c/.h` | One binary search tree: insert-or-increment, in-order visit, free. Owns its node and word memory. | libc |
| `src/table.c/.h` | The 26-tree table and the spec's "hash function" `first_letter - 'a'`. | `bst` |
| `src/output.c/.h` | Column width measurement and line formatting; writes the whole table through a `Writer`. | `table`, `io` |
| `src/main.c` | Plumbing: stdin, argv files, `WORD_FREAK`, the output file, exit code. | everything |

Every function that is not `main` and not declared in a header is `static`.
Every header has an include guard `<FILE>_H`.

### Where the rubric items live

- **BSTs**: `src/bst.c` (`BstNode`, `bst_insert`, `bst_visit_in_order`, `bst_free`).
- **The "hash function"**: `src/table.c`, function `table_hash`, which returns
  `first_letter - 'a'`; `WordTable.trees[26]` is the table it indexes.
- **The five permitted system calls**: `src/io.c` only.

## 3. Data structures

```c
typedef struct BstNode {
    char *word;               /* heap copy, lowercase, NUL-terminated */
    unsigned long count;      /* occurrences across every input */
    struct BstNode *left;
    struct BstNode *right;
} BstNode;

typedef struct {
    BstNode *trees[TABLE_LETTER_COUNT];   /* trees[table_hash(c)] holds words starting with c */
} WordTable;

typedef struct {
    char *data;               /* letters of the word being assembled, lowercased */
    size_t length;
    size_t capacity;
} Tokenizer;

typedef struct {
    int fd;
    char data[IO_CHUNK_SIZE];
    size_t length;            /* bytes buffered and not yet written */
} Writer;

typedef struct {
    size_t longest_word;      /* strlen of the longest word in the table */
    size_t widest_count;      /* decimal digits of the largest count */
} ColumnWidths;
```

`count` is `unsigned long`: a count can never be negative and 32-bit `long`
would be the only platform limit, which no text corpus approaches.

## 4. Public interfaces (what the tests and the other modules compile against)

### io.h

```c
#define IO_CHUNK_SIZE 4096

int     io_open_for_reading(const char *path);          /* open(path, O_RDONLY); -1 on failure, errno set */
int     io_open_output_file(const char *path);          /* open(path, O_WRONLY|O_CREAT|O_TRUNC, 0644) */
int     io_close(int fd);                                /* close(fd); returns 0 or -1 */
ssize_t io_read_chunk(int fd, void *buffer, size_t capacity);  /* read() retried on EINTR; 0 at EOF; -1 on error */
int     io_write_all(int fd, const void *bytes, size_t count); /* write() loop: short writes and EINTR; 0 or -1 */
void    io_write_string(int fd, const char *text);      /* io_write_all of strlen(text); result ignored (used for fd 2) */

void    writer_init(Writer *writer, int fd);
int     writer_put(Writer *writer, const char *bytes, size_t count); /* buffer, flushing when full; 0 or -1 */
int     writer_flush(Writer *writer);                    /* 0 or -1 */
```

### words.h

```c
typedef int (*WordVisitor)(const char *word, size_t length, void *context); /* return 0 to continue */

int  words_is_letter(int byte);     /* 'A'..'Z' or 'a'..'z' only; any other byte (incl. >127) is a separator */
char words_to_lower(int byte);      /* 'A'..'Z' -> 'a'..'z'; other bytes unchanged */

int  tokenizer_init(Tokenizer *tokenizer);     /* 0 or -1 (allocation failed) */
int  tokenizer_feed(Tokenizer *tokenizer, const unsigned char *bytes, size_t count,
                    WordVisitor visit, void *context); /* 0, or the first non-zero visitor result, or -1 on OOM */
int  tokenizer_finish(Tokenizer *tokenizer, WordVisitor visit, void *context); /* emits a pending word */
void tokenizer_free(Tokenizer *tokenizer);
```

The word passed to the visitor is NUL-terminated and owned by the tokenizer;
the visitor must copy it if it needs to keep it (`bst_insert` does).

### bst.h

```c
typedef int (*BstVisitor)(const BstNode *node, void *context); /* return 0 to continue */

int            bst_insert(BstNode **root, const char *word);   /* insert with count 1 or ++count; 0 or -1 on OOM */
const BstNode *bst_find(const BstNode *root, const char *word); /* NULL when absent */
int            bst_visit_in_order(const BstNode *root, BstVisitor visit, void *context);
size_t         bst_size(const BstNode *root);                   /* number of nodes */
void           bst_free(BstNode *root);
```

### table.h

```c
#define TABLE_LETTER_COUNT 26

void table_init(WordTable *table);
int  table_hash(char first_letter);                        /* first_letter - 'a', the spec's hash function */
int  table_add(WordTable *table, const char *word);        /* bst_insert into trees[table_hash(word[0])]; 0 or -1 */
int  table_visit_in_order(const WordTable *table, BstVisitor visit, void *context); /* tree 'a' first */
void table_free(WordTable *table);
```

`table_add` requires a non-empty lowercase word; the tokeniser guarantees it.

### output.h

```c
size_t output_digit_count(unsigned long value);                       /* 0 -> 1, 126 -> 3 */
int    output_measure(const WordTable *table, ColumnWidths *widths);  /* both zero for an empty table */
int    output_format_line(char *buffer, size_t capacity, const char *word, unsigned long count,
                          const ColumnWidths *widths);               /* bytes written, or -1 if it did not fit */
int    output_write_table(const WordTable *table, Writer *writer);   /* measure, then one line per word; 0 or -1 */
```

## 5. Algorithms

### 5.1 Reading

`main` processes each input with the same routine: read `IO_CHUNK_SIZE` bytes at
a time with `io_read_chunk` and hand each chunk to `tokenizer_feed`; at EOF
(`read` returns 0) call `tokenizer_finish`. Chunk size 4096 bytes matches the
page size and the typical `st_blksize`, so each `read` moves one block. The
reader never assumes a full chunk: a terminal delivers one line per `read`, a
pipe delivers whatever is available, and the loop is correct for both.

A `read` error (-1) is reported on fd 2 and that input is abandoned; words
counted so far are kept and processing continues with the next input, which is
the "gracefully move on" behaviour the rubric asks for.

### 5.2 Tokenising across chunk boundaries

The tokeniser is a two-state machine (inside a word / between words) whose
only state is the partially assembled word in `Tokenizer.data`. Because that
buffer lives in the struct rather than on the stack of `tokenizer_feed`, a word
that starts in one `read` chunk and ends in the next is assembled whole. This
is the project's top review-focus item and is pinned by unit tests that feed
the same text in 1-byte, 7-byte and 4096-byte slices and by an e2e case whose
input is longer than one chunk.

A byte is a letter only if it is in `'A'..'Z'` or `'a'..'z'`. Explicit range
checks are used instead of `isalpha` so that bytes above 127 (UTF-8 sequences,
such as the curly apostrophe in the spec's `Isn’t`) are separators regardless
of locale, and so that no `char` value is ever passed to a `<ctype.h>` function
(negative `char` values are undefined behaviour there).

The word buffer starts at 32 bytes and doubles with `realloc` when full, so
there is no maximum word length. The only outputs are calls to the visitor;
the tokeniser never touches the table, which keeps it independently testable.

### 5.3 The table and the trees

`table_add` computes `table_hash(word[0])` and inserts into that tree. The
hash is exactly the spec's `first_letter - 'a'`. Every word the tokeniser emits
starts with a lowercase letter, so the index is always in `0..25`; `table_add`
still rejects anything else with `-1` rather than indexing out of bounds.

`bst_insert` is iterative: it walks a `BstNode **` link until it finds the
word (then increments the count) or a `NULL` link (then allocates a node with a
heap copy of the word and count 1). An iterative insert means a degenerate
tree built from sorted input cannot overflow the stack during the build.
Traversal and free are recursive because their depth equals the tree height,
and the height is bounded by the number of distinct words starting with one
letter; a stack frame is a few dozen bytes, so even a pathological 100 000-word
sorted dictionary stays well inside the default 8 MiB stack. The trees are not
balanced; the spec says that is fine.

Ownership: a node owns its `word`; a tree owns its nodes; the table owns its
26 roots. `table_free` is the single call that releases everything, and `main`
calls it on every exit path after the table exists.

### 5.4 Output format

Each line is `word`, padded with spaces to the length of the longest word, then
` : `, then the count right-aligned to the number of digits of the largest
count, then `\n`. With longest word `respectfully` (12) and largest count 126
(3 digits) the spec's examples are reproduced exactly:

```
a            :  49
respectfully :   1
the          : 126
```

`output_measure` visits the table once to find the two widths, then
`output_write_table` visits it again and formats each line with
`snprintf(buffer, capacity, "%-*s : %*lu\n", ...)`. Two traversals are cheaper
than storing the widths during insertion would be complicated for, and they
keep the trees free of output concerns. The line buffer is allocated once,
sized `longest_word + 3 + widest_count + 2`, and freed when the table has been
written. An empty table produces an empty file (both widths are 0, nothing is
written, the file is still created and truncated).

Alphabetical order falls out of visiting trees `'a'` to `'z'` and each tree
in order. Word comparison is `strcmp` on lowercase ASCII, which is
alphabetical for the letters-only words this program produces.

### 5.5 Writing

`Writer` buffers up to 4096 bytes and calls `io_write_all` when the buffer
fills and on `writer_flush`. `io_write_all` loops because `write` may write
fewer bytes than asked (a short write) and may fail with `EINTR`; it returns
`-1` only on a real error. `main` flushes before closing `output.txt`, checks
both results, and exits with `EXIT_FAILURE` if either failed, since an output
file that was silently truncated is the one outcome the program exists to
prevent.

`output.txt` is opened with `O_WRONLY | O_CREAT | O_TRUNC` and mode `0644`, as
the spec asks: created if missing, emptied if present, readable by everyone.

### 5.6 Errors

- A file that fails to open: `wordfreak: cannot open 'name': <strerror>\n` on
  fd 2 through `write`, then processing continues. The exit status stays
  `EXIT_SUCCESS` because the spec's required behaviour is to move on, and the
  output file is still produced from the inputs that did open.
- A `read` failure: `wordfreak: read error on 'name': <strerror>\n`, same policy.
- `output.txt` cannot be opened, or a write/close fails, or memory runs out:
  message on fd 2, `EXIT_FAILURE`.
- Messages are assembled with `snprintf` into a stack buffer and written with
  one `write`; the result of writing to fd 2 is deliberately ignored because
  there is nowhere left to report a failure to.

## 6. Decisions a reviewer could question

| Decision | Why |
| -------- | --- |
| Zero global variables | The rubric caps globals at 5 and says fewer is better; a `WordTable` on `main`'s stack passed by pointer costs nothing. |
| No prompt before reading stdin | The spec's own terminal transcript shows none; a prompt would also appear when stdin is a pipe. README explains the program waits for `^D`. |
| stdin is read even when files are given | The spec says "always standard in"; the video instructions use `echo "" \| ./wordfreak file` for exactly this reason. |
| Processing order stdin, argv files, `WORD_FREAK` | Counts are order-independent; this is the order the spec lists the sources in. |
| `WORD_FREAK` set but empty is ignored | An empty path cannot be opened; silently ignoring it matches `WORD_FREAK=` meaning "not set". |
| Explicit range checks instead of `isalpha` | Locale-independent, no signed-`char` UB, non-ASCII bytes are separators as the spec's `Isn’t` example needs. |
| Iterative insert, recursive traversal | Insert depth is unbounded by the caller's data order; traversal depth equals tree height and is bounded in practice (section 5.3). |
| Two traversals for output | Keeps width tracking out of the BST and table; the second pass is linear and the table fits in cache. |
| `snprintf` for line formatting | Explicitly allowed by the spec; the alternative hand-rolled padding is more code and more places to be off by one. |
| All five system calls confined to `io.c` | One file to show a grader for the "only open/close/read/write/lseek" rubric item, one place to audit. |
| `lseek` unused | Nothing needs to reposition; the spec permits it, it does not require it. |
| `-D_POSIX_C_SOURCE=200809L` | `open`, `read`, `write`, `close`, `ssize_t` are POSIX, invisible under plain `-std=c99`. |
| Exit status 0 after a skipped file | The rubric's "gracefully move on" means the run succeeded with what it had; the message on fd 2 tells the user what was skipped. |

## 7. Testing strategy

Unit tests (`test/unit/test_<module>.c`, one per module, linked against every
object except `main.o`) prove each pure function, including the tokeniser fed
the same text in slices of every size from 1 to 9 bytes and across an exact
4096-byte boundary, the BST under sorted and unsorted inserts, the hash for all
26 letters, and the formatter against the spec's own widths.

End-to-end tests (`test/e2e/`) run the built `wordfreak` in a scratch directory
and compare `output.txt` with an expected file for: stdin piped, stdin from a
here-string (what typing then `^D` delivers), argv files, the environment file,
all at once, a missing file skipped with a message, empty input, the spec's
`Isn’t that a POT4TO???` example, the spec's typed-in example, and an input
longer than one read chunk. A grep over `src/` for stdio I/O calls is also an
e2e test, so `make test` fails if one is ever introduced.

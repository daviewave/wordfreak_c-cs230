# Research notes: wordfreak

These notes record what was consulted before designing `wordfreak`, a word-frequency
counter whose every byte of I/O must pass through `open`, `close`, `read`, `write`
and `lseek`, with no stdio I/O functions. Each section names the sources actually
read, the practices this program adopts because of them, and the mistakes those
sources warn about and this code is written to avoid. Decisions that follow from
these notes are recorded in `docs/design.md`; the code carries only one-line
pointers.

## 1. POSIX `read`/`write` semantics

### Sources

- https://man7.org/linux/man-pages/man2/read.2.html
- https://man7.org/linux/man-pages/man2/write.2.html
- https://pubs.opengroup.org/onlinepubs/9699919799/functions/read.html
- https://pubs.opengroup.org/onlinepubs/9699919799/functions/write.html
- https://man7.org/linux/man-pages/man3/termios.3.html (canonical mode, `VEOF`)

### Adopted practices

- `read` returns the byte count on success, `0` for end of file, `-1` with `errno`
  on error. A count smaller than requested is normal, not an error: POSIX lists
  three causes (fewer bytes left in the file, a pipe/FIFO/terminal with fewer bytes
  available right now, a signal arriving after some bytes were copied). The reader
  therefore records exactly the returned count as its buffer length and never
  assumes the 4096-byte buffer was filled.
- `EINTR` is returned only when a signal interrupts the call before any data moved;
  the loop retries on `errno == EINTR` and treats every other `-1` as a hard error
  for that source. The program installs no signal handlers, so `EINTR` is nearly
  impossible here, but the retry costs one comparison and keeps the loop correct
  if a handler is ever added.
- `EAGAIN`/`EWOULDBLOCK` only arise on descriptors opened with `O_NONBLOCK`. Every
  descriptor this program uses is blocking (stdin is inherited as-is and never
  altered with `fcntl`), so `EAGAIN` is treated as an ordinary error, not retried.
- `write` may transfer fewer than `count` bytes (POSIX: disk full, file size limit,
  signal after partial transfer). Output goes through a `write_all` loop that
  advances a pointer by each return value until the whole buffer is out, retrying
  on `EINTR` and failing on any other `-1`. The man page is explicit that if some
  bytes were already written when the signal arrived, the call returns that count
  rather than `EINTR`, so the loop needs no special case for it.
- Terminal input in canonical mode (the default) is delivered one line at a time:
  a `read` returns at most one line, so the reader sees many short reads on a
  tty. `^D` at the start of a line makes `read` return `0`, which is the end-of-file
  signal the spec relies on. `^D` in the middle of a line only pushes the pending
  characters to the program without a newline; the next `^D` (or a newline plus
  `^D`) ends the stream. The reader stops at the first `0` return and never reads
  the descriptor again, because a terminal's EOF is not sticky: another `read`
  after `^D` would block waiting for more typing.

### Pitfalls avoided

- Treating a short read as EOF or as an error (it is neither); only `0` is EOF.
- Treating a short write as success, which silently truncates `output.txt`.
- Calling `read` again on a terminal after it returned `0`; the program would
  hang at the prompt instead of writing the output file.
- Checking `errno` after a non-negative return: `errno` is meaningful only when
  the call returned `-1`.
- Assuming a single `write` with the whole output is atomic or complete; it is
  only guaranteed non-interleaved for pipes up to `PIPE_BUF`, and we write a
  regular file anyway.

## 2. `open` flags and modes

### Sources

- https://man7.org/linux/man-pages/man2/open.2.html
- https://pubs.opengroup.org/onlinepubs/9699919799/functions/open.html
- https://man7.org/linux/man-pages/man2/umask.2.html
- https://man7.org/linux/man-pages/man7/feature_test_macros.7.html
- https://man7.org/linux/man-pages/man2/close.2.html

### Adopted practices

- Input files: `open(path, O_RDONLY)`. Output: `open("output.txt",
  O_WRONLY | O_CREAT | O_TRUNC, 0644)`. `O_TRUNC` empties an existing regular file
  (so an empty input produces an empty `output.txt`, as the spec requires) and is
  only defined together with a write access mode.
- The `mode` argument is mandatory with `O_CREAT`: `open` is variadic, and man7
  states that if it is omitted "arbitrary bytes from the stack will be applied as
  the file mode". `0644` asks for `rw-r--r--`; the kernel applies `mode & ~umask`,
  so with the common umask `022` the file is created exactly `0644`, and with a
  stricter umask the file is only tighter, never looser. The program does not call
  `umask()` itself: the user's mask is their policy.
- `errno` values worth a message on fd 2: `ENOENT` (no such file), `EACCES`
  (permission denied, including an unwritable directory for `output.txt`),
  `EISDIR`. On Linux `open(dir, O_RDONLY)` succeeds and the failure surfaces as
  `EISDIR` from the first `read`, so the per-source loop reports read errors
  with the file name too, then closes the descriptor and moves on. The message is
  built with `snprintf` into a stack buffer and `strerror(errno)` (a string
  function, not I/O) and emitted with `write(2, ...)`; `perror` is forbidden by
  the spec.
- A failed `open` of any argv or `WORD_FREAK` file is reported and skipped; the
  remaining sources and stdin are still processed, and the exit status stays
  `EXIT_SUCCESS` unless `output.txt` itself cannot be opened or written. A failed
  `open` of `output.txt` is the one fatal error.
- `close` is checked: the man page notes that write errors on NFS and quota
  systems may only be reported by `close`, and that on Linux a `close` that fails
  with `EINTR` has still released the descriptor, so it is never retried.
- The compile line carries `-D_POSIX_C_SOURCE=200809L`. Under `-std=c99` GCC
  defines `__STRICT_ANSI__`, which stops glibc from exposing anything beyond ISO C
  unless a feature macro asks for it; the POSIX macro restores the declarations in
  `<unistd.h>`, `<fcntl.h>` and `<sys/stat.h>` (`open`, `read`, `write`, `lseek`,
  `close`, `ssize_t`, `mode_t`, the `S_I*` constants). The man page requires the
  macro to be defined before the first `#include`, which `-D` guarantees without
  relying on include order in every file.

### Pitfalls avoided

- Omitting the third argument to `open` with `O_CREAT`, giving a random mode.
- Passing `0644` as the second argument (flags) by mistake; flags and mode are
  distinct arguments and `0644` is a plausible-looking flag value.
- Opening `output.txt` before the inputs are read: the output file is created only
  after counting, so an input named `output.txt` is still read intact.
- Assuming a directory argument fails at `open`; it fails at `read`.
- Opening with `O_WRONLY | O_CREAT` and no `O_TRUNC`, which leaves the tail of a
  longer previous run in the file.
- Leaking a descriptor on the error path: every `open` that succeeds is paired
  with exactly one `close`, including after a read error.

## 3. Binary search trees in C with clear ownership

### Sources

- https://en.wikipedia.org/wiki/Binary_search_tree
- https://en.wikipedia.org/wiki/Tree_traversal

### Adopted practices

- Node: `typedef struct BstNode { char *word; unsigned long count; struct BstNode
  *left, *right; } BstNode;`. The node owns its `word` (a heap copy made at insert
  time); the tokenizer keeps reusing its own buffer, so the tree never aliases
  tokenizer memory.
- Insert-or-increment is iterative with a pointer-to-pointer walk: start with
  `BstNode **link = &root`, loop while `*link` is non-NULL comparing with `strcmp`,
  on a match `(*link)->count++` and return, otherwise descend into
  `&(*link)->left` or `&(*link)->right`; on reaching NULL, allocate and store the
  new node through `*link`. Wikipedia's reference insertion is iterative; the
  pointer-to-pointer form removes the "empty tree" and "attach to parent" special
  cases that a trailing-parent-pointer version needs.
- In-order traversal (left, node, right) visits keys in non-decreasing order, so
  walking the 26 roots `'a'..'z'` in sequence yields the whole vocabulary sorted
  without any extra sort. Traversal takes a visitor `void (*visit)(const BstNode *,
  void *context)` plus a `void *context`; the same walk computes the column widths
  on the first pass and writes the lines on the second.
- Free is post-order (left, right, node): children are released before the parent
  so no freed pointer is ever dereferenced; the word is freed with its node.
- Recursion depth: a BST's height equals its length as a linked list when keys
  arrive in sorted order (a sorted dictionary file is the realistic worst case),
  and both recursive and explicit-stack traversals need space proportional to the
  height. The options are an iterative insert, an explicit stack for traversal, or
  recursive traversal with a documented bound. This program takes the first and
  the last: insertion, the hot path called once per token, never recurses; the
  two traversals and the free recurse once per node with small frames, and the
  bound (height is at most the number of distinct words sharing an initial
  letter, far below the depth an 8 MiB default stack can hold) is stated in
  `docs/design.md`. An explicit-stack traversal is the fallback if that bound is
  ever challenged.
- `unsigned long` for counts: the spec example reaches only three digits, but
  concatenating large books must not wrap, and `%lu` formats it directly.

### Pitfalls avoided

- Storing the tokenizer's buffer pointer in the node instead of a copy; the next
  token would overwrite every stored word.
- Comparing with `==` on pointers or with a case-sensitive mismatch; tokens are
  already lowercase, so plain `strcmp` is the correct and only ordering.
- Recursive insertion, which would make the per-token path's stack depth equal to
  the tree height.
- Freeing a node before its children, or forgetting to free `word`, which
  `gcc -fanalyzer` would flag and a reviewer would too since no sanitizer runs.
- Unbalanced trees are explicitly allowed by the spec; no balancing code.

## 4. Buffered I/O design without stdio

### Sources

- https://man7.org/linux/man-pages/man3/stat.3type.html (`st_blksize`)
- https://man7.org/linux/man-pages/man3/setvbuf.3.html (how stdio buffers)
- https://man7.org/linux/man-pages/man2/write.2.html
- https://man7.org/linux/man-pages/man2/close.2.html
- Stevens & Rago, *Advanced Programming in the UNIX Environment*, chapter 3,
  "I/O efficiency" (not fetched; cited from memory for the observation that
  throughput stops improving once the user buffer reaches the filesystem block
  size)

### Adopted practices

- Reader: `typedef struct { int fd; unsigned char buf[4096]; size_t len; size_t
  pos; } Reader;`. `reader_next_byte` returns the byte at `pos` and advances; when
  `pos == len` it calls `read(fd, buf, sizeof buf)` once, stores the returned
  count in `len`, resets `pos`, and reports EOF on `0` or error on `-1` through a
  distinct return code so the caller can tell the two apart.
- Writer: `typedef struct { int fd; unsigned char buf[4096]; size_t len; }
  Writer;`. `writer_append(bytes, n)` copies into `buf`, flushing whenever the
  buffer is full; `writer_flush` runs the `write_all` loop from section 1 over
  `buf[0..len)` and resets `len`. Lines are formatted with `snprintf` into a small
  stack buffer and appended, so one `write` carries many lines instead of one
  system call per line.
- 4096 bytes: `st_blksize` is "the preferred block size for efficient filesystem
  I/O" and is 4096 on the ext4/xfs filesystems and the x86-64 page size the course
  VM uses; stdio sizes its own buffers from the same field. Larger buffers buy
  nothing measurable for this workload and the fixed array keeps the structs free
  of a second allocation.
- The writer is flushed explicitly before `close`, and both the flush and the
  `close` return values are checked: `close` does not flush user-space buffers
  (it has no idea they exist), and a write error deferred to `close` would
  otherwise be lost. The program exits `EXIT_FAILURE` if either fails.
- Messages to fd 2 go through the same `write_all` loop so a short write cannot
  truncate them, but its return value is deliberately ignored: there is no further
  channel on which to report that stderr is broken.

### Pitfalls avoided

- Mixing stdio and raw descriptors (not possible here, since stdio I/O is banned,
  but it is why the reader and writer own their descriptors outright).
- A reader that returns the whole 4096-byte array to the tokenizer regardless of
  how much `read` filled; the tokenizer only ever sees `buf[0..len)`.
- Forgetting to flush, which leaves up to 4095 bytes of `output.txt` in memory.
- Using the writer's buffer as the `snprintf` target without checking the
  returned length against the remaining capacity, which would truncate a long
  word's line silently.
- Treating `write` to fd 2 like fd 1: an error there must never abort the run.

## 5. Tokenising a byte stream across buffer boundaries

### Sources

- https://man7.org/linux/man-pages/man3/isalpha.3.html (CAVEATS)
- https://en.cppreference.com/w/c/string/byte/isalpha
- ISO/IEC 9899:TC3 committee draft N1256, 7.4 paragraph 1 and 6.2.5 paragraph 15,
  https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf
- https://man7.org/linux/man-pages/man3/realloc.3.html

### Adopted practices

- The tokenizer is a state machine that owns a growable word buffer:
  `typedef struct { char *word; size_t len; size_t cap; } Tokenizer;`. It is fed
  one chunk at a time, `tokenizer_feed(tok, bytes, n, on_word, context)`, and
  keeps the partial word in `tok->word` between chunks, so a word that straddles
  a 4096-byte `read` boundary is assembled byte by byte and emitted only when the
  next non-letter byte arrives. `tokenizer_finish` emits a pending word when a
  source hits EOF; it is called once per input source, so a word ending one file
  never fuses with the word starting the next.
- Growth: capacity starts small (32 bytes) and doubles via `realloc` into a
  temporary pointer; only on success is `tok->word` replaced. The man page states
  a failed `realloc` returns NULL and leaves the original block untouched, so
  `p = realloc(p, n)` would leak the original block on failure. Before appending,
  the code guarantees `len + 2 <= cap` so the terminating `'\0'` always fits when
  the word is handed to the callback as a C string.
- Letter test and lowercasing use explicit ASCII range checks: `is_ascii_letter`
  returns true for `'A'..'Z'` and `'a'..'z'`, and lowercase is `c | 0x20` (or
  `c - 'A' + 'a'`) on an `unsigned char`. The spec defines words as ASCII letters
  and its hash `word[0] - 'a'` already presumes ASCII contiguity, so this makes
  the result independent of the locale and sidesteps the `<ctype.h>` contract
  entirely. Where `isalpha`/`tolower` would be used instead, the argument is
  always cast to `unsigned char` first.
- The `<ctype.h>` contract (C99 7.4p1): the argument "shall be representable as an
  `unsigned char` or shall equal the value of the macro `EOF`. If the argument has
  any other value, the behavior is undefined." C99 6.2.5p15 lets the
  implementation make plain `char` signed, and on x86-64 Linux it is, so a byte
  `>= 0x80` read into a `char` is negative; passing it to `isalpha` indexes
  glibc's table out of range. The man page's CAVEATS section says the same.
- Every non-letter byte is a separator, including digits, apostrophes, NUL, and
  all bytes `>= 0x80`. The spec's `"Isn’t"` uses the three-byte UTF-8 right
  single quotation mark (`E2 80 99`), so it yields `isn` and `t` exactly as the
  spec's example output requires.

### Pitfalls avoided

- Splitting a word at a chunk boundary, which would turn one `respectfully` into
  `respect` and `fully` whenever the boundary landed inside it; the unit tests
  feed the same text with chunk sizes 1, 2, 3 and 4096 and require identical
  counts.
- A fixed maximum word length; the buffer grows without bound and the only limit
  is memory.
- Calling `isalpha(c)` on a plain `char` that may be negative.
- Calling `setlocale` or relying on the current locale to decide what a letter
  is; the answer must be the same on every grader's machine.
- Forgetting the pending word at EOF, which drops the last word of every file
  that does not end in a newline (the spec's terminal example ends with `^D`
  rather than a newline).
- Emitting an empty word on consecutive separators; a word is emitted only when
  `len > 0`.

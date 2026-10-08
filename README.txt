wordfreak - word frequency counter (CS230 project 3)
=====================================================

Overview
--------
wordfreak reads every byte from standard input, from each file named on the
command line and from the file named by the WORD_FREAK environment variable,
splits the bytes into words (maximal runs of ASCII letters, lowercased), counts
each distinct word across all of those inputs, and writes output.txt in the
current directory: one "word : count" line per distinct word, alphabetically,
with the colons and the counts aligned. All I/O is done with the system calls
open(), close(), read() and write(); no stdio I/O function is used anywhere
(snprintf is used only to format text in memory, which the spec allows).

The program is split into six small modules, each with one responsibility:

  src/io.c      the ONLY file that calls open/close/read/write: a chunked
                reader (io_read_chunk), a write-all loop that survives short
                writes and EINTR (io_write_all), and a 4096-byte output
                buffer (Writer, writer_put, writer_flush).
  src/words.c   the tokenizer: turns a byte stream delivered in arbitrary
                chunks into lowercase words, carrying a partial word across
                chunk boundaries so a word split by a read() is never cut.
  src/bst.c     one binary search tree: insert-or-increment, in-order visit,
                free.
  src/table.c   the hashtable of 26 BSTs and the hash function
                (first letter minus 'a').
  src/output.c  column-width measurement and the "word : count" line format.
  src/main.c    plumbing: stdin, argv files, WORD_FREAK, output.txt, exit code.

Build and run
-------------
    make                        builds build/wordfreak (the submission bundle
                                from `make dist` builds ./wordfreak in place)
    ./wordfreak                 type words, finish with Ctrl-D, then read output.txt
    cat book.txt | ./wordfreak  standard input piped in
    echo "" | ./wordfreak a.txt b.txt            files as arguments
    echo "" | WORD_FREAK=a.txt ./wordfreak       file from the environment
    cat c.txt | WORD_FREAK=a.txt ./wordfreak b.txt   all three at once
    cat output.txt              the result

Standard input is always read, so when only files are wanted, feed it an
empty string (echo "" | ...) exactly as the spec suggests. There is no prompt:
the spec's transcript shows none, and a prompt would also appear when stdin
is a pipe. Other make targets: make test (unit and end-to-end tests),
make check (gcc -fanalyzer), make dist (flat Gradescope bundle), make clean.

Requirements map
----------------
Where each rubric item is satisfied (file : function).

Deliverables
  Source and header files ........ src/*.c, src/*.h (six modules listed above)
  Makefile ....................... Makefile; `make dist` writes the flat
                                   submission Makefile into dist/
  Executable named "wordfreak" ... Makefile target all -> build/wordfreak;
                                   dist/Makefile -> ./wordfreak
  README.txt ..................... this file
  Video link ..................... last line of this file

Output requirements
  stdin (piped or typed) ......... src/main.c : count_all_inputs reads
                                   descriptor 0 through read_words_from_descriptor
                                   until read() returns 0 (EOF / Ctrl-D)
  arguments ...................... src/main.c : count_argument_files -> count_file
  environment .................... src/main.c : count_environment_file
                                   (getenv("WORD_FREAK")) -> count_file
  all at once .................... src/main.c : count_all_inputs feeds all three
                                   sources into the same WordTable
  format ......................... src/output.c : output_measure (longest word,
                                   widest count), output_format_line
                                   ("%-*s : %*lu\n"), output_write_table;
                                   alphabetical order comes from
                                   src/table.c : table_visit_in_order
                                   (tree 'a' first, each tree in order)

Structure used
  Binary search trees ............ src/bst.h : struct BstNode {word, count,
                                   left, right}; src/bst.c : bst_insert
                                   (insert-or-increment), bst_visit_in_order
                                   (alphabetical walk), bst_free
  The "hash function", one BST
  per letter 'a'..'z' ............ src/table.h : WordTable.trees[26];
                                   src/table.c : table_hash returns
                                   first_letter - 'a'; table_add inserts into
                                   trees[table_hash(word[0])]
  Only open/close/read/write/
  lseek for I/O .................. src/io.c : io_open_for_reading (O_RDONLY),
                                   io_open_output_file
                                   (O_WRONLY|O_CREAT|O_TRUNC, 0644), io_close,
                                   io_read_chunk, io_write_all. No other file
                                   calls a system call; test/e2e/no_stdio.sh
                                   greps src/ to prove no stdio I/O call exists.
                                   lseek is permitted but not needed.

Design
  Minimal globals ................ zero global variables; the WordTable and
                                   Tokenizer live on main's stack and are
                                   passed by pointer
  Code organised ................. six modules, every function does one thing
                                   (see Overview)
  Data structures ................ BstNode, WordTable, Tokenizer, Writer,
                                   ColumnWidths, LineSink (src/output.c)
  Reasonably efficient ........... 4096-byte reads and buffered writes; one
                                   pass over the input; two linear passes over
                                   the table (widths, then lines)
  Error checking ................. src/main.c : count_file reports a file that
                                   fails to open on fd 2 with write() and moves
                                   on; read_words_from_descriptor reports read
                                   errors; write_output_file checks open,
                                   write, flush and close; src/io.c :
                                   io_write_all retries short writes and EINTR

Style and comments
  Consistent bracketing and indentation: K&R braces, four-space indent, in
  every file. Every function has a header comment (purpose, parameters and
  return where not obvious); non-obvious struct fields and constants are
  commented at their declaration; docs/design.md holds the rationale.

Design notes
------------
- Words: a byte is a letter only if it is 'A'..'Z' or 'a'..'z'; every other
  byte, including digits, apostrophes and UTF-8 bytes, separates words. So
  "Isn't that a POT4TO???" yields a, isn, pot, t, that, to, as the spec shows.
- Chunk boundaries: the tokenizer keeps the partial word in its own buffer
  between read() calls, so a word straddling two 4096-byte chunks is counted
  once (test/unit/test_words.c and the long_input e2e case prove it).
- The trees are not balanced (the spec says that is fine). Insertion is
  iterative so sorted input cannot overflow the stack.
- Output: output.txt is opened O_WRONLY|O_CREAT|O_TRUNC with mode 0644, so it
  is created if absent, emptied if present, and readable. Empty input gives
  an empty output.txt.
- Errors: a missing input file prints
  "wordfreak: cannot open 'name': No such file or directory" on fd 2 and the
  program continues; the exit status is 0 because the run completed with the
  inputs that could be read. Failure to create or write output.txt, or
  running out of memory, exits 1.

Tests: `make test` runs unit tests for every module (test/unit/) and
end-to-end cases (test/e2e/cases/) covering stdin piped, stdin typed (a
here-string), argument files, the environment file, all at once, a missing
file, a directory as argument, empty input, the spec's "Isn't that a
POT4TO???" example and an input longer than one read chunk.

Video: <VIDEO URL TO BE ADDED>

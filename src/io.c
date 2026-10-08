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
 * the buffer bypasses it (docs/design.md section 5.5). Returns 0 or -1. */
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

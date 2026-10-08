#ifndef IO_H
#define IO_H

#include <stddef.h>
#include <sys/types.h>

/* Bytes moved per read() and buffered per write(): one page, one disk block. */
#define IO_CHUNK_SIZE 4096

/* Output buffer in front of one file descriptor. */
typedef struct {
    int fd;                   /* destination descriptor */
    char data[IO_CHUNK_SIZE]; /* bytes waiting to be written */
    size_t length;            /* bytes buffered and not yet written */
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

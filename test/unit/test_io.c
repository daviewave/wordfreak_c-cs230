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

    char fresh[sizeof path + sizeof ".fresh"];
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

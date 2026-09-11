// C11 helpers linked into test/lang/run/stdlib/065 and 066: they hand std.io
// two descriptors whose behaviour a fort program cannot produce on its own but
// which stdlib.md 2.4 gives rules for -- one that accepts a short write and one
// that yields bytes and then fails.
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#include <sys/resource.h>

// A file whose size is capped at std_io_write_cap() bytes. Linux truncates a
// write that crosses RLIMIT_FSIZE to the room that is left and delivers the
// short count, then fails the next one with EFBIG and SIGXFSZ, which is
// ignored here so that the program lives to see the error. That is the one
// portable way to make write(2) transfer less than it was given without a
// second process draining the other end.
enum { WRITE_CAP = 4096, FILE_MODE = 0644 };

int64_t std_io_write_cap(void) {
    return (int64_t)WRITE_CAP;
}

int32_t std_io_capped_file(const char* path) {
    struct rlimit limit;
    limit.rlim_cur = (rlim_t)WRITE_CAP;
    limit.rlim_max = (rlim_t)WRITE_CAP;
    if (setrlimit(RLIMIT_FSIZE, &limit) != 0) {
        return -1;
    }
    if (signal(SIGXFSZ, SIG_IGN) == SIG_ERR) {
        return -1;
    }
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, FILE_MODE);
}

// The read end of a pipe holding n bytes, open for reading without blocking
// and with its write end still open: the first read returns the bytes, the
// second finds the pipe empty and fails with EAGAIN rather than reporting end
// of file, which is a read error arriving after a read has succeeded.
int32_t std_io_pipe_with_bytes(int64_t n) {
    int fds[2];
    if (pipe(fds) != 0) {
        return -1;
    }
    char byte = 'p';
    for (int64_t i = 0; i < n; i++) {
        if (write(fds[1], &byte, 1) != 1) {
            (void)close(fds[0]);
            (void)close(fds[1]);
            return -1;
        }
    }
    if (fcntl(fds[0], F_SETFL, O_NONBLOCK) != 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        return -1;
    }
    // The write end is deliberately left open: closing it would turn the
    // second read into end of file, which is a success and not the error this
    // helper exists to produce.
    return fds[0];
}

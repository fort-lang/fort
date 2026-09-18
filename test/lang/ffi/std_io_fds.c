// Provides file descriptors that test short writes and partial reads.
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#include <sys/resource.h>

// `std_io_capped_file` sets the process file-size limit and ignores `SIGXFSZ`.
// When both setup calls succeed, both global effects remain after the function returns.
// If `signal` fails after `setrlimit` succeeds, the limit persists and `SIGXFSZ` stays unchanged.
enum { WRITE_CAP = 4096, FILE_MODE = 0644 };

int64_t std_io_write_cap(void) {
    return (int64_t)WRITE_CAP;
}

// Returns -1 on failure.
// On success, the caller owns the returned descriptor and must close it.
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

// Returns -1 on failure and closes both descriptors.
// On success, the caller owns the nonblocking read descriptor and must close it.
// `n` must fit in the pipe before any read starts.
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
    // Keep the write descriptor open until process exit.
    // This makes the read after the buffered bytes fail with EAGAIN instead of returning EOF.
    return fds[0];
}

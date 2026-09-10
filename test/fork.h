// Runs a function in a forked child and captures its stderr and exit status,
// for tests of paths that end the process (fatal_oom, fatal_internal) or
// write to stderr directly. Under asan the child runs LeakSanitizer when it
// exits, so a forked function must not leave allocations behind.
#ifndef FORT_TEST_FORK_H
#define FORT_TEST_FORK_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

#include "common.h"

// The result of a run that could not be started or was killed, and the exit
// status of a child whose redirection failed.
enum { FORK_RUN_ABNORMAL = -1, FORK_RUN_SETUP_FAILED = 127 };

// Runs fn in a child with stderr redirected to a pipe; stores the child's
// stderr in err (NUL-terminated, truncated to err_size - 1 bytes) and returns
// its exit status, or FORK_RUN_ABNORMAL. A fn that returns exits with 0.
static int run_forked(void (*fn)(void), char* err, size_t err_size) {
    int fds[2];
    err[0] = '\0';
    if (pipe(fds) != 0) {
        return FORK_RUN_ABNORMAL;
    }
    (void)fflush(NULL); // nothing buffered is written twice
    const pid_t pid = fork();
    if (pid < 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        return FORK_RUN_ABNORMAL;
    }
    if (pid == 0) {
        (void)close(fds[0]);
        if (dup2(fds[1], STDERR_FILENO) < 0) {
            _exit(FORK_RUN_SETUP_FAILED);
        }
        (void)close(fds[1]);
        fn();
        (void)fflush(stderr);
        _exit(0);
    }
    (void)close(fds[1]);
    size_t used = 0;
    while (used + 1 < err_size) {
        const ssize_t got = read(fds[0], err + used, err_size - used - 1);
        if (got <= 0) {
            break;
        }
        used += (size_t)got;
    }
    err[used] = '\0';
    (void)close(fds[0]);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status)) {
        return FORK_RUN_ABNORMAL;
    }
    return WEXITSTATUS(status);
}

#endif

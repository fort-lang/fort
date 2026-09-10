// Helpers shared by the runtime suites (runtime_test.c,
// runtime_float_test.c): the runtime writes to a descriptor, so a test points
// one at a temporary file and compares the bytes that land there.
#ifndef FORT_TEST_RUNTIME_HELPERS_H
#define FORT_TEST_RUNTIME_HELPERS_H

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "fort_rt.h"

#include "test.h"

enum { CAPTURE_MAX = 4096 };

// A descriptor redirected to a temporary file: `fd` is the number the runtime
// writes to and `saved` its previous target, or -1 when `fd` is the temporary
// file's own descriptor.
typedef struct {
    int fd;
    int saved;
    FILE* file;
} capture_t;

// Captures fd, or a fresh descriptor of its own when fd is negative.
static inline capture_t capture_begin(int fd) {
    capture_t c;
    c.fd = fd;
    c.saved = -1;
    c.file = tmpfile();
    if (c.file == NULL) {
        return c;
    }
    if (fd < 0) {
        c.fd = fileno(c.file);
        return c;
    }
    c.saved = dup(fd);
    TEST_UNUSED(dup2(fileno(c.file), fd));
    return c;
}

// Reads everything written so far into buf, NUL-terminated, and returns its
// length.
static inline size_t capture_read(const capture_t* c, char* buf, size_t size) {
    TEST_UNUSED(fseek(c->file, 0, SEEK_SET));
    const size_t got = fread(buf, 1, size - 1, c->file);
    buf[got] = '\0';
    return got;
}

static inline void capture_end(capture_t* c) {
    if (c->saved >= 0) {
        TEST_UNUSED(dup2(c->saved, c->fd));
        TEST_UNUSED(close(c->saved));
    }
    if (c->file != NULL) {
        TEST_UNUSED(fclose(c->file));
    }
}

// Fails the test with both texts when they differ.
#define ASSERT_SAME_TEXT(actual, expected)                                                         \
    do {                                                                                           \
        const char* want = (expected);                                                             \
        if (strcmp((actual), want) != 0) {                                                         \
            TEST_UNUSED(fprintf(stderr,                                                            \
                                "%s:%d\n\ttext differs\n\tactual:   \"%s\"\n\texpected: \"%s\"\n", \
                                __FILE__,                                                          \
                                __LINE__,                                                          \
                                (actual),                                                          \
                                want));                                                            \
            TEST_FAIL();                                                                           \
        }                                                                                          \
    } while (0)

// The number of bytes written so far.
static inline long capture_size(const capture_t* c) {
    return lseek(c->fd, 0, SEEK_END);
}

#define ASSERT_SIZE(capture, n) TEST_ASSERT_EQ_INT64(capture_size(&(capture)), (int64_t)(n))

// Runs body with fd 1 captured, flushes, and returns the bytes it produced.
static inline size_t stdout_of(void (*body)(void), char* buf, size_t size) {
    capture_t c = capture_begin(1);
    body();
    fort_rt_flush_all();
    const size_t got = capture_read(&c, buf, size);
    capture_end(&c);
    return got;
}

#define ASSERT_STDOUT(body, expected)                                                              \
    do {                                                                                           \
        char actual[CAPTURE_MAX];                                                                  \
        TEST_UNUSED(stdout_of(body, actual, sizeof actual));                                       \
        ASSERT_SAME_TEXT(actual, expected);                                                        \
    } while (0)

#endif

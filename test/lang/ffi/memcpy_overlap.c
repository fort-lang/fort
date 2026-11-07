// Replaces memcpy in the program that links it: it copies as memcpy does and stops the run with
// SIGABRT when the two ranges overlap, which ISO C11 7.24.2.1 leaves undefined. The C libraries
// of both targets copy overlapping ranges in the order of memmove, so a call of memcpy with
// overlapping ranges gives the right bytes there; this helper makes such a call fail.
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

void* memcpy(void* restrict dst, const void* restrict src, size_t n) {
    const unsigned char* from = src;
    unsigned char* to = dst;
    if (n > 0 && from < to + n && to < from + n) {
        static const char message[] = "memcpy: the two ranges overlap\n";
        (void)write(2, message, sizeof message - 1);
        abort();
    }
    // volatile keeps the compiler from making this loop a call of memcpy
    volatile unsigned char* out = to;
    for (size_t i = 0; i < n; i++) {
        out[i] = from[i];
    }
    return dst;
}

// test/mac/open_tail_probe.c: model a Mac C open call with a variable mode tail.
// D9.8
#include <fcntl.h>

int fort_mac_open_tail(const char* path, int flags, mode_t mode) {
    return open(path, flags, mode);
}

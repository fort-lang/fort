// Models a Darwin C open call with a variable mode argument.
#include <fcntl.h>

int fort_darwin_open_tail(const char* path, int flags, mode_t mode) {
    return open(path, flags, mode);
}

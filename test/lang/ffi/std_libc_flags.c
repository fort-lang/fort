// C11 helper linked into test/lang/run/stdlib/005_libc_file.ft: it reports the
// open(2), lseek(2) and errno constants from the system headers, so that the
// literal values std.libc states for them (stdlib.md 2.2) are held against
// the platform rather than against themselves. One function per constant,
// because several of them share a value and a single switch over an index
// would be a pile of identical branches.
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

int32_t std_o_rdonly(void) {
    return O_RDONLY;
}

int32_t std_o_wronly(void) {
    return O_WRONLY;
}

int32_t std_o_rdwr(void) {
    return O_RDWR;
}

int32_t std_o_creat(void) {
    return O_CREAT;
}

int32_t std_o_trunc(void) {
    return O_TRUNC;
}

int32_t std_o_append(void) {
    return O_APPEND;
}

int32_t std_seek_set(void) {
    return SEEK_SET;
}

int32_t std_seek_cur(void) {
    return SEEK_CUR;
}

int32_t std_seek_end(void) {
    return SEEK_END;
}

int32_t std_enoent(void) {
    return ENOENT;
}

int32_t std_eintr(void) {
    return EINTR;
}

int32_t std_eacces(void) {
    return EACCES;
}

int32_t std_einval(void) {
    return EINVAL;
}

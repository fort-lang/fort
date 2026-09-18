// Maps the errno helper to Darwin libc for direct Darwin ABI tests.
extern int* __error(void);

int* __errno_location(void) {
    return __error();
}

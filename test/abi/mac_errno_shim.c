// The direct Mac ABI probe uses the Linux standard sources.
// This test symbol maps their errno call to the Mac C library.
// D9.8
extern int* __error(void);

int* __errno_location(void) {
    return __error();
}

// The direct darwin ABI probe uses the linux standard sources.
// This test symbol maps their errno call to the darwin C library.
// D9.8
extern int* __error(void);

int* __errno_location(void) {
    return __error();
}

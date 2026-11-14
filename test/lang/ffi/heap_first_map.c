// Makes the first mmap of the allocator of the runtime fail, before the program starts. A test
// links this file with heap_calls.c, which counts and refuses the calls. The C library runs the
// constructor below before `main`, so the first `new` of the process gets no address range, as
// when the address-space limit of the process is lower than one segment (ENOMEM).
#include <stdint.h>

// The kind of an mmap call in heap_calls.c, and the function that refuses a call of a kind.
enum { CALL_MAP = 1 };
void heap_calls_refuse(int32_t kind, uint64_t nth, uint64_t part);

__attribute__((constructor)) static void refuse_first_map(void) {
    heap_calls_refuse(CALL_MAP, 1, 0);
}

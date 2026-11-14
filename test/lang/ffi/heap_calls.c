// Stands between a fort test program and the 4 functions of the system that change a mapping:
// mmap, munmap, mprotect and madvise. A program that links this file calls the 4 functions
// below in place of the functions of the C library, because the linker takes a name from the
// program first. Only the allocator of the runtime (std.heap) calls these names in a fort
// program, so each count below is a count of the calls of the allocator.
//
// Each function counts its call, keeps the range of the last call of its kind, and can refuse a
// chosen call with ENOMEM, as the system does at a limit. Each other call goes on to the
// function of the C library (dlsym with RTLD_NEXT). The file also reads what a test cannot
// read in fort: a byte of storage that nothing can access (with no memory fault), and the
// memory that the process holds.
// The C library reads these two names: RTLD_NEXT needs them on Linux and MAP_ANON on macOS.
// NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming)
#define _GNU_SOURCE
// NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming)
#define _DARWIN_C_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif

// The kinds of a call. A call of mprotect has one of two kinds: it gives access to a range
// (CALL_ACCESS), or it takes each access away with PROT_NONE (CALL_NO_ACCESS).
enum { CALL_MAP = 1, CALL_UNMAP = 2, CALL_ACCESS = 3, CALL_NO_ACCESS = 4, CALL_ADVISE = 5 };
enum { KINDS = 6 };

// The first address that a pretended mapping gets. No system gives an address this high.
#define PRETEND_BASE ((uint64_t)1 << 60)

// The bytes of the buffer for one read of a file of /proc.
enum { PROC_CHUNK = 65536 };

// The base of a decimal number, and the bytes of a kibibyte.
enum { DECIMAL = 10, KIB = 1024 };

struct call_state {
    uint64_t count;   // the calls of this kind
    uint64_t order;   // the number of the last call of this kind among the calls of each kind
    uint64_t address; // the address of the last call; for mmap, its result
    uint64_t bytes;   // the length of the last call
    uint64_t refuse;  // counts down to the call that fails; 0 when no call fails
    uint64_t part;    // the bytes at the start of its range that the refused call changes
};

static struct call_state calls[KINDS];
static uint64_t calls_made;
static uint64_t calls_refused;
static int pretend;
static uint64_t pretend_next = PRETEND_BASE;
static int byte_pipe[2] = {-1, -1};

typedef void* (*map_call_t)(void*, size_t, int, int, int, off_t);
typedef int (*range_call_t)(void*, size_t);
typedef int (*mode_call_t)(void*, size_t, int);

// The function `name` of the C library, which stands after the program in the search order.
static void* next_function(const char* name) {
    return dlsym(RTLD_NEXT, name);
}

// Counts a call of `kind` for the range of `bytes` bytes at `address`.
static void count_call(int kind, uint64_t address, uint64_t bytes) {
    calls_made += 1;
    calls[kind].count += 1;
    calls[kind].order = calls_made;
    calls[kind].address = address;
    calls[kind].bytes = bytes;
}

// Whether this call of `kind` is the one that a test chose to fail. Sets errno when it is.
static int refuses(int kind) {
    if (calls[kind].refuse == 0) {
        return 0;
    }
    calls[kind].refuse -= 1;
    if (calls[kind].refuse != 0) {
        return 0;
    }
    calls_refused += 1;
    errno = ENOMEM;
    return 1;
}

// Whether `address` is the address of a pretended mapping, which the system does not know.
static int pretended(const void* address) {
    return (uint64_t)(uintptr_t)address >= PRETEND_BASE;
}

void* mmap(void* addr, size_t len, int prot, int flags, int fd, off_t offset) {
    if (refuses(CALL_MAP)) {
        count_call(CALL_MAP, 0, len);
        return MAP_FAILED;
    }
    void* got = NULL;
    if (pretend) {
        // An address that no mapping holds: the program never accesses it.
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        got = (void*)(uintptr_t)pretend_next;
        pretend_next += len;
    } else {
        map_call_t next = (map_call_t)next_function("mmap");
        got = next(addr, len, prot, flags, fd, offset);
    }
    count_call(CALL_MAP, (uint64_t)(uintptr_t)got, len);
    return got;
}

int munmap(void* addr, size_t len) {
    count_call(CALL_UNMAP, (uint64_t)(uintptr_t)addr, len);
    if (refuses(CALL_UNMAP)) {
        return -1;
    }
    if (pretended(addr)) {
        return 0;
    }
    range_call_t next = (range_call_t)next_function("munmap");
    return next(addr, len);
}

int mprotect(void* addr, size_t len, int prot) {
    int kind = prot == PROT_NONE ? CALL_NO_ACCESS : CALL_ACCESS;
    mode_call_t next = (mode_call_t)next_function("mprotect");
    count_call(kind, (uint64_t)(uintptr_t)addr, len);
    if (refuses(kind)) {
        // The system changes the map entries of a range one after the other, so a call that
        // fails can leave the start of its range changed.
        uint64_t part = calls[kind].part;
        if (part != 0 && part <= len && !pretended(addr)) {
            (void)next(addr, part, prot);
            errno = ENOMEM;
        }
        return -1;
    }
    if (pretended(addr)) {
        return 0;
    }
    return next(addr, len, prot);
}

int madvise(void* addr, size_t len, int advice) {
    count_call(CALL_ADVISE, (uint64_t)(uintptr_t)addr, len);
    if (refuses(CALL_ADVISE)) {
        return -1;
    }
    if (pretended(addr)) {
        return 0;
    }
    mode_call_t next = (mode_call_t)next_function("madvise");
    return next(addr, len, advice);
}

// The calls of `kind` since the start of the process, or 0 for a number that names no kind.
uint64_t heap_calls_count(int32_t kind) {
    return kind > 0 && kind < KINDS ? calls[kind].count : 0;
}

// The number of the last call of `kind` among the calls of each kind, from 1, or 0 when the
// process made no call of `kind`. Two calls with numbers n and n + 1 have no call between them.
uint64_t heap_calls_order(int32_t kind) {
    return kind > 0 && kind < KINDS ? calls[kind].order : 0;
}

// The address of the last call of `kind`: its first argument, and for mmap its result (0 for a
// refused mmap).
uint64_t heap_calls_address(int32_t kind) {
    return kind > 0 && kind < KINDS ? calls[kind].address : 0;
}

// The length of the last call of `kind`.
uint64_t heap_calls_bytes(int32_t kind) {
    return kind > 0 && kind < KINDS ? calls[kind].bytes : 0;
}

// The calls that this file refused since the start of the process.
uint64_t heap_calls_refused(void) {
    return calls_refused;
}

// Makes call `nth` of `kind` fail with ENOMEM, counted from the next call (1 is the next
// call), and makes no call fail for an `nth` of 0. A refused mprotect first changes `part`
// bytes at the start of its range, which must be a multiple of the page size, or no byte for a
// `part` of 0. Each other refused call changes nothing.
void heap_calls_refuse(int32_t kind, uint64_t nth, uint64_t part) {
    if (kind > 0 && kind < KINDS) {
        calls[kind].refuse = nth;
        calls[kind].part = part;
    }
}

// With `on` other than 0, each later mmap gives a pretended mapping: an address range from
// 2^60 up that the system does not know, so the call takes no address range of the process.
// A call of munmap, mprotect or madvise for such an address returns 0 and changes nothing. A
// program must not access a pretended mapping.
void heap_calls_pretend(int32_t on) {
    pretend = on != 0;
}

// The byte at `address`, or -1 when nothing can access it. The system reads the byte for
// write(2) and refuses an address that nothing can access, so no memory fault stops the test.
// Returns -2 when the pipe of the function fails.
int64_t heap_calls_byte(uint64_t address) {
    if (byte_pipe[0] < 0 && pipe(byte_pipe) != 0) {
        return -2;
    }
    // The test gives the address as a number; the system tests it before it reads the byte.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    if (write(byte_pipe[1], (const void*)(uintptr_t)address, 1) != 1) {
        return -1;
    }
    unsigned char got = 0;
    if (read(byte_pipe[0], &got, 1) != 1) {
        return -2;
    }
    return (int64_t)got;
}

// The memory that the process holds now, in bytes: the pages of the process that stand in the
// memory of the machine. Returns 0 when the system does not tell. Linux counts each page that
// the program stored into until the system drops it, also in a range with no access. macOS
// counts no page of a range with no access. The peak of getrusage is no measure for a test: on
// Linux it starts at the memory of the process that started the program.
uint64_t heap_calls_resident(void) {
#ifdef __APPLE__
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) !=
        KERN_SUCCESS) {
        return 0;
    }
    return (uint64_t)info.resident_size;
#else
    // The line `VmRSS:   1234 kB` of /proc/self/status.
    static const char VM_RSS_KEY[] = "VmRSS:";
    static char proc_chunk[PROC_CHUNK];
    int fd = open("/proc/self/status", O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    ssize_t got = read(fd, proc_chunk, sizeof proc_chunk - 1);
    (void)close(fd);
    if (got <= 0) {
        return 0;
    }
    proc_chunk[got] = '\0';
    const char* at = strstr(proc_chunk, VM_RSS_KEY);
    if (at == NULL) {
        return 0;
    }
    at += sizeof VM_RSS_KEY - 1;
    while (*at == ' ' || *at == '\t') {
        at += 1;
    }
    uint64_t kib = 0;
    while (*at >= '0' && *at <= '9') {
        kib = kib * DECIMAL + (uint64_t)(*at - '0');
        at += 1;
    }
    return kib * KIB;
#endif
}

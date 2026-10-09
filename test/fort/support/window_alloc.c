// Counts the bytes of the blocks that a window of a leak probe allocates and does not release.
//
// The fort runtime allocates with calloc and releases with free (std/rt.ft), so these two
// definitions see each block of fort code. While a window is open, calloc records each block
// and its requested size, and free takes the block out again. A block from before the window
// passes through free unchanged. The count reads no allocator state, so free chunks, the glibc
// tcache, the program break and the trim threshold do not move it. It holds calloc blocks only:
// a block from malloc or realloc, and an allocation inside libc that does not call this calloc,
// stay outside the count.
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __APPLE__
#include <malloc/malloc.h>
#else
extern void* window_system_calloc(size_t count, size_t size) __asm__("__libc_calloc");
extern void window_system_free(void* pointer) __asm__("__libc_free");
#endif

void* calloc(size_t count, size_t size);
void free(void* pointer);
void fort_window_alloc_begin(void);
uint64_t fort_window_alloc_end(void);

// One recorded block. Address 0 marks an empty slot.
typedef struct {
    uintptr_t address;
    uint64_t size;
} block_t;

enum { FIRST_CAPACITY_BITS = 16, WORD_BITS = 64 };
// 2^64 divided by the golden ratio: Fibonacci hashing keeps the high bits of the product.
static const uint64_t HASH_MULTIPLIER = 0x9E3779B97F4A7C15ULL;

static block_t* table;
static unsigned capacity_bits;
static size_t capacity;
static size_t used;
static uint64_t live_bytes;
static bool active;

static void* system_calloc(size_t count, size_t size) {
#ifdef __APPLE__
    return malloc_zone_calloc(malloc_default_zone(), count, size);
#else
    return window_system_calloc(count, size);
#endif
}

static void system_free(void* pointer) {
#ifdef __APPLE__
    malloc_zone_free(malloc_zone_from_ptr(pointer), pointer);
#else
    window_system_free(pointer);
#endif
}

static size_t home(uintptr_t address) {
    return (size_t)(((uint64_t)address * HASH_MULTIPLIER) >> (WORD_BITS - capacity_bits));
}

static void place(block_t block) {
    size_t slot = home(block.address);
    while (table[slot].address != 0) {
        slot = (slot + 1) & (capacity - 1);
    }
    table[slot] = block;
}

// The table comes from the system allocator, so the window does not count it.
static void grow(void) {
    block_t* old = table;
    size_t old_capacity = capacity;
    capacity_bits = old == NULL ? FIRST_CAPACITY_BITS : capacity_bits + 1;
    capacity = (size_t)1 << capacity_bits;
    table = system_calloc(capacity, sizeof(block_t));
    assert(table != NULL);
    for (size_t slot = 0; slot < old_capacity; ++slot) {
        if (old[slot].address != 0) {
            place(old[slot]);
        }
    }
    if (old != NULL) {
        system_free(old);
    }
}

static void record(void* pointer, uint64_t size) {
    if ((used + 1) * 2 > capacity) {
        grow();
    }
    place((block_t){(uintptr_t)pointer, size});
    ++used;
    live_bytes += size;
}

// Backward-shift deletion: each later block of the run moves into the hole when the hole lies
// between its home slot and its slot, so no probe sequence has a gap.
static void forget(void* pointer) {
    if (used == 0) {
        return;
    }
    size_t mask = capacity - 1;
    uintptr_t address = (uintptr_t)pointer;
    size_t hole = home(address);
    while (table[hole].address != address) {
        if (table[hole].address == 0) {
            return;
        }
        hole = (hole + 1) & mask;
    }
    live_bytes -= table[hole].size;
    --used;
    size_t next = (hole + 1) & mask;
    while (table[next].address != 0) {
        size_t wanted = home(table[next].address);
        if (((hole - wanted) & mask) < ((next - wanted) & mask)) {
            table[hole] = table[next];
            hole = next;
        }
        next = (next + 1) & mask;
    }
    table[hole] = (block_t){0, 0};
}

void* calloc(size_t count, size_t size) {
    void* pointer = system_calloc(count, size);
    if (active && pointer != NULL) {
        record(pointer, (uint64_t)count * size);
    }
    return pointer;
}

void free(void* pointer) {
    if (pointer == NULL) {
        return;
    }
    if (active) {
        forget(pointer);
    }
    system_free(pointer);
}

void fort_window_alloc_begin(void) {
    assert(!active && used == 0);
    live_bytes = 0;
    active = true;
}

// Closes the window and forgets its blocks, so a later free of one passes through.
uint64_t fort_window_alloc_end(void) {
    assert(active);
    active = false;
    uint64_t kept = live_bytes;
    if (table != NULL) {
        system_free(table);
    }
    table = NULL;
    capacity_bits = 0;
    capacity = 0;
    used = 0;
    live_bytes = 0;
    return kept;
}

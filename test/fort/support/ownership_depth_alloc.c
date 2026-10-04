// Counts test-local calloc blocks and their actual releases.
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __APPLE__
#include <malloc/malloc.h>
#else
extern void* depth_system_calloc(size_t count, size_t size) __asm__("__libc_calloc");
extern void depth_system_free(void* pointer) __asm__("__libc_free");
#endif

enum { BLOCK_LIMIT = 4096 };
static void* blocks[BLOCK_LIMIT];
static uint64_t created;
static uint64_t released;
static uint64_t live;
static bool active;

void* calloc(size_t count, size_t size);
void free(void* pointer);
void fort_depth_alloc_begin(void);
uint64_t fort_depth_alloc_end(void);
uint64_t fort_depth_alloc_created(void);
uint64_t fort_depth_alloc_released(void);

void* calloc(size_t count, size_t size) {
#ifdef __APPLE__
    void* pointer = malloc_zone_calloc(malloc_default_zone(), count, size);
#else
    void* pointer = depth_system_calloc(count, size);
#endif
    if (active && pointer != NULL) {
        size_t slot = 0;
        while (slot < BLOCK_LIMIT && blocks[slot] != NULL) {
            ++slot;
        }
        assert(slot < BLOCK_LIMIT);
        blocks[slot] = pointer;
        ++created;
        ++live;
    }
    return pointer;
}

void free(void* pointer) {
    if (pointer == NULL) {
        return;
    }
    for (size_t slot = 0; slot < BLOCK_LIMIT; ++slot) {
        if (blocks[slot] == pointer) {
            blocks[slot] = NULL;
            ++released;
            --live;
            break;
        }
    }
#ifdef __APPLE__
    malloc_zone_free(malloc_zone_from_ptr(pointer), pointer);
#else
    depth_system_free(pointer);
#endif
}

void fort_depth_alloc_begin(void) {
    assert(!active && live == 0);
    created = 0;
    released = 0;
    active = true;
}

uint64_t fort_depth_alloc_end(void) {
    assert(active);
    active = false;
    return live;
}

uint64_t fort_depth_alloc_created(void) {
    return created;
}
uint64_t fort_depth_alloc_released(void) {
    return released;
}

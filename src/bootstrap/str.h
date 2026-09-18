// Provides checked allocation, string views, string buffers, and string pools.
// The allocation helpers end the process when allocation fails.
#ifndef FORT_STR_H
#define FORT_STR_H

#include <stdbool.h>
#include <stdint.h>

// ---- fatal errors and allocation ---------------------------------------------

// The exit status of an internal error.
enum { FATAL_EXIT_STATUS = 2 };

// Prints "fort: error: out of memory" to stderr and exits with status 2. Every
// allocation failure in the compiler ends here.
_Noreturn void fatal_oom(void);

// Prints "fort: error: internal error: <what>" to stderr and exits with status
// 2: a violated precondition inside the compiler.
_Noreturn void fatal_internal(const char* what);

// Zeroed storage of `size` bytes (one byte when `size` is 0), never NULL;
// released with mem_free.
void* mem_alloc(uint64_t size);
void mem_free(void* p);

// The sum and the product of two sizes. An overflow is an out-of-memory exit,
// so every size computation in the compiler has one failure path.
uint64_t mem_add(uint64_t a, uint64_t b);
uint64_t mem_mul(uint64_t a, uint64_t b);

// The capacity a container of capacity `cap` grows to in order to hold
// `need` elements: the largest of MEM_MIN_CAP, twice `cap` and `need`
enum { MEM_MIN_CAP = 16 };
uint64_t mem_grown_cap(uint64_t cap, uint64_t need);

// ---- views -------------------------------------------------------

typedef struct {
    const char* ptr;
    uint64_t len;
} str_t;

// The view of a NUL-terminated string, without the terminator; NULL gives
// the zero view.
str_t str_from_cstr(const char* s);

// Returns a view of `len` bytes at `ptr`. A source extent is a range. A span is the fort type `T@`.
str_t str_from_range(const char* ptr, uint64_t len);

// Byte-wise equality.
bool str_eq(str_t a, str_t b);

// Lexicographic order by unsigned byte value, a proper prefix first: -1, 0
// or 1.
int str_cmp(str_t a, str_t b);

// 64-bit FNV-1a over the bytes, fixed by standard library contract: the empty string
// hashes to the offset basis 14695981039346656037.
uint64_t str_hash(str_t s);

// Whether `s` begins with `prefix`. An empty prefix always matches.
bool str_starts_with(str_t s, str_t prefix);

// The position of the first `ch` in `s`, or -1.
int64_t str_index_of(str_t s, char ch);

// Returns a heap copy of `s` with a trailing NUL byte. When `s` has no NUL, `.ptr` is also a C
// string. Release it with str_del.
str_t str_dup(str_t s);

// Releases a copy made by str_dup or sb_take. The zero view is a no-op. Must
// not be applied to any other view.
void str_del(str_t s);

// ---- string pool -----------------------------------------------------------------

// Bytes per block of the pool. A string longer than that gets its own block.
enum { STR_POOL_BLOCK_SIZE = 4096 };

// A pool owns copies of strings, each NUL-terminated, in blocks that are
// released all at once. Zero-initialized storage is a valid empty pool.
typedef struct {
    char** blocks;      // every block the pool owns
    uint64_t block_len; // blocks in use
    uint64_t block_cap; // slots in blocks
    char* cur;          // the block small strings are packed into, or NULL
    uint64_t used;      // bytes used in cur
    uint64_t cap;       // size of cur
} str_pool_t;

void str_pool_init(str_pool_t* p);

// Releases every block. The pool is empty and usable afterwards.
void str_pool_free(str_pool_t* p);

// Returns a pool-owned copy of `s` that is stable until str_pool_free. A trailing NUL makes `.ptr`
// a C string when `s` has no NUL. Copies are not deduplicated: two calls with equal input give two
// views.
str_t str_pool_intern(str_pool_t* p, str_t s);

// ---- growable byte buffer ---------------------------------------

// The smallest non-zero capacity; growth doubles from there.
enum { SB_MIN_CAP = MEM_MIN_CAP };

// Zero-initialized storage is a valid empty buffer.
typedef struct {
    char* data;   // storage the buffer owns, or NULL
    uint64_t len; // bytes in use
    uint64_t cap; // bytes allocated
} sb_t;

void sb_init(sb_t* b);

// Releases the storage. The buffer is empty and usable afterwards.
void sb_free(sb_t* b);

// Ensures room for `extra` more bytes. Growth moves the contents to fresh
// storage: every earlier sb_view or sb_cstr result dies.
void sb_reserve(sb_t* b, uint64_t extra);

void sb_push(sb_t* b, char ch);

// Appends a NUL-terminated string without its terminator.
void sb_append(sb_t* b, const char* cstr);

// Appends the bytes of a view, which must not alias the buffer's storage.
void sb_append_str(sb_t* b, str_t s);

// Appends the decimal text of a number, `-` first for a negative one.
void sb_append_u64(sb_t* b, uint64_t v);
void sb_append_i64(sb_t* b, int64_t v);

// Sets the length to 0 and keeps the storage.
void sb_clear(sb_t* b);

// The contents as a view, valid until the next mutating call.
str_t sb_view(const sb_t* b);

// The contents followed by a NUL byte (not counted in len), valid until the
// next mutating call.
const char* sb_cstr(sb_t* b);

// An exact copy of the contents as by str_dup (released with str_del); the
// buffer is empty afterwards and keeps its storage.
str_t sb_take(sb_t* b);

#endif

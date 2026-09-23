// Implements memory, string, buffer, and string-pool operations.
#include "str.h"

#include <stdio.h>
#include <stdlib.h>

// ---- fatal errors and allocation ---------------------------------------------

_Noreturn void fatal_oom(void) {
    (void)fputs("fort: error: out of memory\n", stderr);
    exit(FATAL_EXIT_STATUS);
}

_Noreturn void fatal_internal(const char* what) {
    (void)fputs("fort: error: internal error: ", stderr);
    (void)fputs(what, stderr);
    (void)fputc('\n', stderr);
    exit(FATAL_EXIT_STATUS);
}

void* mem_alloc(uint64_t size) {
    // calloc(0) may return NULL; a one-byte block never does
    void* p = calloc(1, size == 0U ? 1U : (size_t)size);
    if (p == NULL) {
        fatal_oom();
    }
    return p;
}

void mem_free(void* p) {
    free(p);
}

uint64_t mem_add(uint64_t a, uint64_t b) {
    if (b > UINT64_MAX - a) {
        fatal_oom();
    }
    return a + b;
}

uint64_t mem_mul(uint64_t a, uint64_t b) {
    if (a != 0 && b > UINT64_MAX / a) {
        fatal_oom();
    }
    return a * b;
}

uint64_t mem_grown_cap(uint64_t cap, uint64_t need) {
    uint64_t grown = MEM_MIN_CAP;
    if (cap > grown / 2U) {
        grown = mem_add(cap, cap);
    }
    if (need > grown) {
        grown = need;
    }
    return grown;
}

// ---- views ---------------------------------------------------------------------------

static const uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325ULL;
static const uint64_t FNV_PRIME = 0x100000001b3ULL;

str_t str_from_cstr(const char* s) {
    str_t r;
    r.ptr = s;
    r.len = 0;
    if (s != NULL) {
        while (s[r.len] != '\0') {
            r.len++;
        }
    }
    return r;
}

str_t str_from_range(const char* ptr, uint64_t len) {
    str_t r;
    r.ptr = ptr;
    r.len = len;
    return r;
}

bool str_eq(str_t a, str_t b) {
    if (a.len != b.len) {
        return false;
    }
    for (uint64_t i = 0; i < a.len; i++) {
        if (a.ptr[i] != b.ptr[i]) {
            return false;
        }
    }
    return true;
}

int str_cmp(str_t a, str_t b) {
    const uint64_t n = a.len < b.len ? a.len : b.len;
    for (uint64_t i = 0; i < n; i++) {
        const unsigned char x = (unsigned char)a.ptr[i];
        const unsigned char y = (unsigned char)b.ptr[i];
        if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    if (a.len == b.len) {
        return 0;
    }
    return a.len < b.len ? -1 : 1;
}

uint64_t str_hash(str_t s) {
    uint64_t h = FNV_OFFSET_BASIS;
    for (uint64_t i = 0; i < s.len; i++) {
        h ^= (uint64_t)(unsigned char)s.ptr[i];
        h *= FNV_PRIME; // unsigned: wraps, as `*%` does
    }
    return h;
}

bool str_starts_with(str_t s, str_t prefix) {
    if (prefix.len > s.len) {
        return false;
    }
    return str_eq(str_from_range(s.ptr, prefix.len), prefix);
}

int64_t str_index_of(str_t s, char ch) {
    for (uint64_t i = 0; i < s.len; i++) {
        if (s.ptr[i] == ch) {
            return (int64_t)i;
        }
    }
    return -1;
}

// Copies `len` bytes from `src` to `dst`; the ranges must not overlap.
static void copy_bytes(char* dst, const char* src, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

str_t str_dup(str_t s) {
    char* copy = mem_alloc(mem_add(s.len, 1U));
    copy_bytes(copy, s.ptr, s.len);
    copy[s.len] = '\0';
    return str_from_range(copy, s.len);
}

void str_del(str_t s) {
    // The copy was made by mem_alloc, so dropping const is giving back what
    // str_dup handed out.
    mem_free((char*)s.ptr);
}

// ---- string pool -----------------------------------------------------------------

enum { STR_POOL_MIN_BLOCKS = 16 };

void str_pool_init(str_pool_t* p) {
    p->blocks = NULL;
    p->block_len = 0;
    p->block_cap = 0;
    p->cur = NULL;
    p->used = 0;
    p->cap = 0;
}

void str_pool_free(str_pool_t* p) {
    for (uint64_t i = 0; i < p->block_len; i++) {
        mem_free(p->blocks[i]);
    }
    mem_free((void*)p->blocks);
    str_pool_init(p);
}

// Appends a fresh block of `size` bytes to the pool's block list.
static char* str_pool_add_block(str_pool_t* p, uint64_t size) {
    if (p->block_len == p->block_cap) {
        const uint64_t cap = p->block_cap == 0 ? STR_POOL_MIN_BLOCKS : p->block_cap * 2U;
        char** bigger = (char**)mem_alloc(mem_mul(cap, sizeof(char*)));
        for (uint64_t i = 0; i < p->block_len; i++) {
            bigger[i] = p->blocks[i];
        }
        mem_free((void*)p->blocks);
        p->blocks = bigger;
        p->block_cap = cap;
    }
    char* block = mem_alloc(size);
    p->blocks[p->block_len] = block;
    p->block_len++;
    return block;
}

str_t str_pool_intern(str_pool_t* p, str_t s) {
    const uint64_t need = mem_add(s.len, 1U);
    char* dst = NULL;
    if (need > STR_POOL_BLOCK_SIZE) {
        // A string that does not fit a block gets a block of its own; the
        // current block stays current.
        dst = str_pool_add_block(p, need);
    } else {
        if (p->cur == NULL || p->used + need > p->cap) {
            p->cur = str_pool_add_block(p, STR_POOL_BLOCK_SIZE);
            p->used = 0;
            p->cap = STR_POOL_BLOCK_SIZE;
        }
        dst = p->cur + p->used;
        p->used += need;
    }
    copy_bytes(dst, s.ptr, s.len);
    dst[s.len] = '\0';
    return str_from_range(dst, s.len);
}

// ---- growable byte buffer ---------------------------------------------------------

void sb_init(sb_t* b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void sb_free(sb_t* b) {
    mem_free(b->data);
    sb_init(b);
}

void sb_reserve(sb_t* b, uint64_t extra) {
    const uint64_t need = mem_add(b->len, extra);
    if (need <= b->cap) {
        return;
    }
    const uint64_t cap = mem_grown_cap(b->cap, need);
    char* bigger = mem_alloc(cap);
    copy_bytes(bigger, b->data, b->len);
    mem_free(b->data);
    b->data = bigger;
    b->cap = cap;
}

void sb_push(sb_t* b, char ch) {
    sb_reserve(b, 1U);
    b->data[b->len] = ch;
    b->len++;
}

void sb_append(sb_t* b, const char* cstr) {
    sb_append_str(b, str_from_cstr(cstr));
}

void sb_append_str(sb_t* b, str_t s) {
    if (s.len == 0) {
        return; // also keeps the empty buffer's NULL storage untouched
    }
    sb_reserve(b, s.len);
    copy_bytes(b->data + b->len, s.ptr, s.len);
    b->len += s.len;
}

// The most digits a 64-bit number has in decimal.
enum { DECIMAL_DIGITS_MAX = 20, DECIMAL_BASE = 10 };

void sb_append_u64(sb_t* b, uint64_t v) {
    char digits[DECIMAL_DIGITS_MAX];
    uint64_t n = 0;
    do {
        digits[n] = (char)('0' + (int)(v % DECIMAL_BASE));
        n++;
        v /= DECIMAL_BASE;
    } while (v != 0);
    sb_reserve(b, n);
    while (n > 0) {
        n--;
        b->data[b->len] = digits[n];
        b->len++;
    }
}

void sb_append_i64(sb_t* b, int64_t v) {
    if (v < 0) {
        sb_push(b, '-');
        // The magnitude in unsigned arithmetic, so that INT64_MIN works.
        sb_append_u64(b, (uint64_t)0 - (uint64_t)v);
    } else {
        sb_append_u64(b, (uint64_t)v);
    }
}

void sb_clear(sb_t* b) {
    b->len = 0;
}

str_t sb_view(const sb_t* b) {
    return str_from_range(b->data, b->len);
}

const char* sb_cstr(sb_t* b) {
    sb_reserve(b, 1U);
    b->data[b->len] = '\0';
    return b->data;
}

str_t sb_take(sb_t* b) {
    const str_t copy = str_dup(sb_view(b));
    b->len = 0;
    return copy;
}

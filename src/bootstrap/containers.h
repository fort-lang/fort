// Provides pointer and integer vectors and a map from string views to integers.
// All allocation failures call fatal_oom().
#ifndef FORT_CONTAINERS_H
#define FORT_CONTAINERS_H

#include <stdbool.h>
#include <stdint.h>

#include "str.h"

// The first non-zero capacity of a vector; growth doubles from there.
enum { VEC_MIN_CAP = MEM_MIN_CAP };

// ---- vectors ----------------------------------------------------

// A vector of borrowed pointers: it owns its slots and never a pointee.
typedef struct {
    void** items; // the slots, or NULL
    uint64_t len; // slots in use
    uint64_t cap; // slots allocated
} ptrvec_t;

void ptrvec_init(ptrvec_t* v);

// Releases the slots; the vector is empty and usable afterwards.
void ptrvec_free(ptrvec_t* v);

// Ensures room for `extra` more elements.
void ptrvec_reserve(ptrvec_t* v, uint64_t extra);

void ptrvec_push(ptrvec_t* v, void* p);

// Removes and returns the last element; an internal error when empty.
void* ptrvec_pop(ptrvec_t* v);

typedef struct {
    int64_t* items;
    uint64_t len;
    uint64_t cap;
} intvec_t;

void intvec_init(intvec_t* v);
void intvec_free(intvec_t* v);
void intvec_reserve(intvec_t* v, uint64_t extra);
void intvec_push(intvec_t* v, int64_t x);
int64_t intvec_pop(intvec_t* v);

// ---- string map ------------------------------------------------

// The first non-zero capacity of a map; a rebuild doubles from there.
enum { STRMAP_MIN_CAP = MEM_MIN_CAP };

enum { SLOT_EMPTY = 0, SLOT_FULL = 1, SLOT_DEAD = 2 };

typedef struct {
    str_t key; // a borrowed view; the map never owns a key
    int64_t val;
    uint64_t hash; // str_hash(key)
    uint8_t state; // SLOT_EMPTY, SLOT_FULL or SLOT_DEAD
} strmap_entry_t;

// Open addressing with linear probing, FNV-1a and tombstones. The caller
// keeps every key's bytes alive and unchanged while its entry exists.
// Iteration is a walk over entries[0..cap) taking the SLOT_FULL slots.
typedef struct {
    strmap_entry_t* entries; // the table, or NULL
    uint64_t cap;            // 0 or a power of two
    uint64_t live;           // entries in state SLOT_FULL
    uint64_t used;           // live plus tombstones
} strmap_t;

void strmap_init(strmap_t* m);

// Releases the table; the map is empty and usable afterwards. Keys are
// untouched.
void strmap_free(strmap_t* m);

// Inserts `key` with `val`, or replaces the value of an existing key; true
// when the key was new.
bool strmap_put(strmap_t* m, str_t key, int64_t val);

// True and `*out = val` when present; false with `*out` unchanged otherwise.
bool strmap_get(const strmap_t* m, str_t key, int64_t* out);

bool strmap_has(const strmap_t* m, str_t key);

// Removes the entry of `key`; true when it was present.
bool strmap_remove(strmap_t* m, str_t key);

uint64_t strmap_count(const strmap_t* m);

#endif

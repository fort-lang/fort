/* Vectors and the string map; see containers.h. */
#include "containers.h"

#include <stddef.h>

/* ---- vectors ------------------------------------------------------------------ */

void ptrvec_init(ptrvec_t* v) {
    v->items = NULL;
    v->len = 0;
    v->cap = 0;
}

void ptrvec_free(ptrvec_t* v) {
    mem_free((void*)v->items);
    ptrvec_init(v);
}

void ptrvec_reserve(ptrvec_t* v, uint64_t extra) {
    const uint64_t need = mem_add(v->len, extra);
    if (need <= v->cap) {
        return;
    }
    const uint64_t cap = mem_grown_cap(v->cap, need);
    void** bigger = (void**)mem_alloc(mem_mul(cap, sizeof(void*)));
    for (uint64_t i = 0; i < v->len; i++) {
        bigger[i] = v->items[i];
    }
    mem_free((void*)v->items);
    v->items = bigger;
    v->cap = cap;
}

void ptrvec_push(ptrvec_t* v, void* p) {
    ptrvec_reserve(v, 1U);
    v->items[v->len] = p;
    v->len++;
}

void* ptrvec_pop(ptrvec_t* v) {
    if (v->len == 0) {
        fatal_internal("ptrvec_pop: empty");
    }
    v->len--;
    return v->items[v->len];
}

void intvec_init(intvec_t* v) {
    v->items = NULL;
    v->len = 0;
    v->cap = 0;
}

void intvec_free(intvec_t* v) {
    mem_free(v->items);
    intvec_init(v);
}

void intvec_reserve(intvec_t* v, uint64_t extra) {
    const uint64_t need = mem_add(v->len, extra);
    if (need <= v->cap) {
        return;
    }
    const uint64_t cap = mem_grown_cap(v->cap, need);
    int64_t* bigger = mem_alloc(mem_mul(cap, sizeof(int64_t)));
    for (uint64_t i = 0; i < v->len; i++) {
        bigger[i] = v->items[i];
    }
    mem_free(v->items);
    v->items = bigger;
    v->cap = cap;
}

void intvec_push(intvec_t* v, int64_t x) {
    intvec_reserve(v, 1U);
    v->items[v->len] = x;
    v->len++;
}

int64_t intvec_pop(intvec_t* v) {
    if (v->len == 0) {
        fatal_internal("intvec_pop: empty");
    }
    v->len--;
    return v->items[v->len];
}

/* ---- string map --------------------------------------------------------------- */

void strmap_init(strmap_t* m) {
    m->entries = NULL;
    m->cap = 0;
    m->live = 0;
    m->used = 0;
}

void strmap_free(strmap_t* m) {
    mem_free(m->entries);
    strmap_init(m);
}

/* The slot holding `key`, or `cap` when absent: the probe starts at
 * hash & (cap - 1), steps by one, wraps, and stops at the first empty slot
 * (stdlib.md 2.8). */
static uint64_t strmap_find(const strmap_t* m, str_t key, uint64_t hash) {
    if (m->cap == 0) {
        return m->cap;
    }
    const uint64_t mask = m->cap - 1U;
    uint64_t i = hash & mask;
    while (true) {
        const strmap_entry_t* e = &m->entries[i];
        if (e->state == SLOT_EMPTY) {
            return m->cap;
        }
        if (e->state == SLOT_FULL && e->hash == hash && str_eq(e->key, key)) {
            return i;
        }
        i = (i + 1U) & mask;
    }
}

/* Stores an entry known to be absent into a table with an empty slot, at
 * the first tombstone of its probe or else at the empty slot ending it. */
static void strmap_place(strmap_t* m, str_t key, int64_t val, uint64_t hash) {
    const uint64_t mask = m->cap - 1U;
    uint64_t i = hash & mask;
    uint64_t dead = m->cap;
    while (m->entries[i].state != SLOT_EMPTY) {
        if (m->entries[i].state == SLOT_DEAD && dead == m->cap) {
            dead = i;
        }
        i = (i + 1U) & mask;
    }
    if (dead != m->cap) {
        i = dead; /* a tombstone becomes live: used is unchanged */
    } else {
        m->used++;
    }
    strmap_entry_t* e = &m->entries[i];
    e->key = key;
    e->val = val;
    e->hash = hash;
    e->state = SLOT_FULL;
    m->live++;
}

/* Moves the live entries into a fresh table whose capacity starts at
 * STRMAP_MIN_CAP and doubles until (live + 1) * 2 <= cap; tombstones
 * disappear (stdlib.md 2.8). */
static void strmap_rebuild(strmap_t* m) {
    uint64_t cap = STRMAP_MIN_CAP;
    while (mem_mul(mem_add(m->live, 1U), 2U) > cap) {
        cap = mem_mul(cap, 2U);
    }
    strmap_t fresh;
    strmap_init(&fresh);
    fresh.entries = mem_alloc(mem_mul(cap, sizeof(strmap_entry_t)));
    fresh.cap = cap;
    for (uint64_t i = 0; i < m->cap; i++) {
        const strmap_entry_t* e = &m->entries[i];
        if (e->state == SLOT_FULL) {
            strmap_place(&fresh, e->key, e->val, e->hash);
        }
    }
    mem_free(m->entries);
    *m = fresh;
}

bool strmap_put(strmap_t* m, str_t key, int64_t val) {
    if (m->cap == 0 || (m->used + 1U) * 4U > m->cap * 3U) {
        strmap_rebuild(m);
    }
    const uint64_t hash = str_hash(key);
    const uint64_t i = strmap_find(m, key, hash);
    if (i != m->cap) {
        m->entries[i].val = val;
        return false;
    }
    strmap_place(m, key, val, hash);
    return true;
}

bool strmap_get(const strmap_t* m, str_t key, int64_t* out) {
    const uint64_t i = strmap_find(m, key, str_hash(key));
    if (i == m->cap) {
        return false;
    }
    *out = m->entries[i].val;
    return true;
}

bool strmap_has(const strmap_t* m, str_t key) {
    return strmap_find(m, key, str_hash(key)) != m->cap;
}

bool strmap_remove(strmap_t* m, str_t key) {
    const uint64_t i = strmap_find(m, key, str_hash(key));
    if (i == m->cap) {
        return false;
    }
    m->entries[i].state = SLOT_DEAD;
    m->live--;
    return true;
}

uint64_t strmap_count(const strmap_t* m) {
    return m->live;
}

/* Unit tests of containers.h: the vectors of stdlib.md 2.7 and the string
 * map of stdlib.md 2.8, including the scenario of
 * test/lang/programs/hashmap.ft. */
#include "containers.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "fork.h"

#include "test.h"

enum { ERR_MAX = 256 };

/* The vector growth steps up to 4096 elements: 16, 32, ..., 4096. */
enum { GROWTH_STEPS = 9, GROWTH_ELEMENTS = 4096 };

/* Sizes of the bulk tests. */
enum { MANY_KEYS = 5000, KEY_MAX = 32, COLLISION_KEYS = 12 };

/* The words of test/lang/programs/hashmap.ft; a brace initializer with
 * commas cannot sit inside a TEST body. */
static const char* const WORDS[] = {"alpha",
                                    "bravo",
                                    "charlie",
                                    "delta",
                                    "echo",
                                    "foxtrot",
                                    "golf",
                                    "hotel",
                                    "india",
                                    "juliet",
                                    "kilo"};
enum { WORD_COUNT = 11 };

static str_t s(const char* text) {
    return str_from_cstr(text);
}

/* Formats "k<n>" into buf and returns its view. */
static str_t key_of(char* buf, size_t size, int n) {
    TEST_UNUSED(snprintf(buf, size, "k%d", n));
    return str_from_cstr(buf);
}

/* A pool of generated keys whose bytes outlive the map that borrows them. */
typedef struct {
    char* storage;
    str_t keys[MANY_KEYS];
} key_set_t;

static void key_set_init(key_set_t* ks) {
    ks->storage = mem_alloc((uint64_t)MANY_KEYS * KEY_MAX);
    for (int i = 0; i < MANY_KEYS; i++) {
        ks->keys[i] = key_of(ks->storage + ((size_t)i * KEY_MAX), KEY_MAX, i);
    }
}

static void key_set_free(key_set_t* ks) {
    mem_free(ks->storage);
    ks->storage = NULL;
}

/* The number of SLOT_FULL slots, walked in table order. */
static uint64_t count_full(const strmap_t* m) {
    uint64_t n = 0;
    for (uint64_t i = 0; i < m->cap; i++) {
        if (m->entries[i].state == SLOT_FULL) {
            n++;
        }
    }
    return n;
}

/* The number of SLOT_DEAD slots. */
static uint64_t count_dead(const strmap_t* m) {
    uint64_t n = 0;
    for (uint64_t i = 0; i < m->cap; i++) {
        if (m->entries[i].state == SLOT_DEAD) {
            n++;
        }
    }
    return n;
}

/* The sum of the values of the live entries. */
static int64_t sum_values(const strmap_t* m) {
    int64_t total = 0;
    for (uint64_t i = 0; i < m->cap; i++) {
        if (m->entries[i].state == SLOT_FULL) {
            total += m->entries[i].val;
        }
    }
    return total;
}

/* The slot index of a present key, or cap. */
static uint64_t slot_of(const strmap_t* m, str_t key) {
    for (uint64_t i = 0; i < m->cap; i++) {
        if (m->entries[i].state == SLOT_FULL && str_eq(m->entries[i].key, key)) {
            return i;
        }
    }
    return m->cap;
}

/* ---- ptrvec ------------------------------------------------------------------- */

TEST(ptrvec_init_is_the_empty_vector, {
    ptrvec_t v;
    ptrvec_init(&v);
    TEST_ASSERT_NULL(v.items);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)0);
    ptrvec_free(&v);
})

TEST(ptrvec_push_stores_pointers_in_order, {
    ptrvec_t v;
    ptrvec_init(&v);
    int a = 1;
    int b = 2;
    int c = 3;
    ptrvec_push(&v, &a);
    ptrvec_push(&v, &b);
    ptrvec_push(&v, &c);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)3);
    TEST_ASSERT_TRUE(v.items[0] == &a);
    TEST_ASSERT_TRUE(v.items[1] == &b);
    TEST_ASSERT_TRUE(v.items[2] == &c);
    ptrvec_free(&v);
})

TEST(ptrvec_first_push_allocates_sixteen_slots, {
    ptrvec_t v;
    ptrvec_init(&v);
    int a = 1;
    ptrvec_push(&v, &a);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)VEC_MIN_CAP);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)16);
    ptrvec_free(&v);
})

TEST(ptrvec_pop_returns_the_last_element_first, {
    ptrvec_t v;
    ptrvec_init(&v);
    int a = 1;
    int b = 2;
    ptrvec_push(&v, &a);
    ptrvec_push(&v, &b);
    TEST_ASSERT_TRUE(ptrvec_pop(&v) == &b);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)1);
    TEST_ASSERT_TRUE(ptrvec_pop(&v) == &a);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)16);
    ptrvec_free(&v);
})

TEST(ptrvec_stores_null_pointers, {
    ptrvec_t v;
    ptrvec_init(&v);
    ptrvec_push(&v, NULL);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)1);
    TEST_ASSERT_NULL(v.items[0]);
    TEST_ASSERT_NULL(ptrvec_pop(&v));
    ptrvec_free(&v);
})

TEST(ptrvec_grows_by_doubling_across_several_steps, {
    ptrvec_t v;
    ptrvec_init(&v);
    int cells[GROWTH_ELEMENTS];
    uint64_t expected_cap = 0;
    int doublings = 0;
    for (int i = 0; i < GROWTH_ELEMENTS; i++) {
        cells[i] = i;
        ptrvec_push(&v, &cells[i]);
        if ((uint64_t)i + 1 > expected_cap) {
            expected_cap = expected_cap == 0 ? VEC_MIN_CAP : expected_cap * 2;
            doublings++;
        }
        TEST_ASSERT_EQ_UINT64(v.cap, expected_cap);
    }
    TEST_ASSERT_EQ_INT32(doublings, GROWTH_STEPS);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)GROWTH_ELEMENTS);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)GROWTH_ELEMENTS);
    for (int i = 0; i < GROWTH_ELEMENTS; i++) {
        TEST_ASSERT_TRUE(v.items[i] == &cells[i]);
    }
    ptrvec_free(&v);
})

TEST(ptrvec_reserve_is_a_no_op_when_there_is_room, {
    ptrvec_t v;
    ptrvec_init(&v);
    int a = 1;
    ptrvec_push(&v, &a);
    void** items = v.items;
    ptrvec_reserve(&v, 15);
    TEST_ASSERT_TRUE(v.items == items);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)16);
    ptrvec_reserve(&v, 0);
    TEST_ASSERT_TRUE(v.items == items);
    ptrvec_free(&v);
})

TEST(ptrvec_reserve_takes_the_exact_need_when_it_beats_doubling, {
    ptrvec_t v;
    ptrvec_init(&v);
    ptrvec_reserve(&v, 100);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)100);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)0);
    TEST_ASSERT_NONNULL(v.items);
    ptrvec_reserve(&v, 101);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)200);
    ptrvec_free(&v);
})

TEST(ptrvec_reserve_of_zero_on_the_empty_vector_allocates_nothing, {
    ptrvec_t v;
    ptrvec_init(&v);
    ptrvec_reserve(&v, 0);
    TEST_ASSERT_NULL(v.items);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)0);
    ptrvec_free(&v);
})

TEST(ptrvec_growth_keeps_the_elements, {
    ptrvec_t v;
    ptrvec_init(&v);
    int cells[17];
    for (int i = 0; i < 17; i++) {
        cells[i] = i;
        ptrvec_push(&v, &cells[i]);
    }
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)32);
    for (int i = 16; i >= 0; i--) {
        TEST_ASSERT_TRUE(ptrvec_pop(&v) == &cells[i]);
    }
    ptrvec_free(&v);
})

TEST(ptrvec_free_empties_and_the_vector_is_reusable, {
    ptrvec_t v;
    ptrvec_init(&v);
    int a = 1;
    ptrvec_push(&v, &a);
    ptrvec_free(&v);
    TEST_ASSERT_NULL(v.items);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)0);
    ptrvec_free(&v);
    ptrvec_push(&v, &a);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)1);
    ptrvec_free(&v);
})

TEST(zero_initialized_ptrvec_is_valid, {
    ptrvec_t v;
    TEST_UNUSED(memset(&v, 0, sizeof v));
    int a = 1;
    ptrvec_push(&v, &a);
    TEST_ASSERT_TRUE(ptrvec_pop(&v) == &a);
    ptrvec_free(&v);
})

static void pop_empty_ptrvec(void) {
    ptrvec_t v;
    ptrvec_init(&v);
    TEST_UNUSED(ptrvec_pop(&v));
}

TEST(ptrvec_pop_of_an_empty_vector_is_an_internal_error, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(pop_empty_ptrvec, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: ptrvec_pop: empty\n");
})

static void ptrvec_reserve_overflowing_count(void) {
    ptrvec_t v;
    ptrvec_init(&v);
    ptrvec_push(&v, NULL);
    ptrvec_reserve(&v, UINT64_MAX); /* len + extra overflows */
}

static void ptrvec_reserve_overflowing_bytes(void) {
    ptrvec_t v;
    ptrvec_init(&v);
    ptrvec_reserve(&v, UINT64_MAX / sizeof(void*) + 1); /* count * 8 overflows */
}

TEST(ptrvec_reserve_overflow_is_out_of_memory, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(ptrvec_reserve_overflowing_count, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: out of memory\n");
    TEST_ASSERT_EQ_INT32(run_forked(ptrvec_reserve_overflowing_bytes, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: out of memory\n");
})

/* ---- intvec ------------------------------------------------------------------- */

TEST(intvec_init_is_the_empty_vector, {
    intvec_t v;
    intvec_init(&v);
    TEST_ASSERT_NULL(v.items);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)0);
    intvec_free(&v);
})

TEST(intvec_push_stores_values_in_order, {
    intvec_t v;
    intvec_init(&v);
    intvec_push(&v, 10);
    intvec_push(&v, -20);
    intvec_push(&v, INT64_MAX);
    intvec_push(&v, INT64_MIN);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)4);
    TEST_ASSERT_EQ_INT64(v.items[0], (int64_t)10);
    TEST_ASSERT_EQ_INT64(v.items[1], (int64_t)-20);
    TEST_ASSERT_EQ_INT64(v.items[2], INT64_MAX);
    TEST_ASSERT_EQ_INT64(v.items[3], INT64_MIN);
    intvec_free(&v);
})

TEST(intvec_first_push_allocates_sixteen_slots, {
    intvec_t v;
    intvec_init(&v);
    intvec_push(&v, 1);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)16);
    intvec_free(&v);
})

TEST(intvec_pop_returns_the_last_element_first, {
    intvec_t v;
    intvec_init(&v);
    intvec_push(&v, 1);
    intvec_push(&v, 2);
    intvec_push(&v, 3);
    TEST_ASSERT_EQ_INT64(intvec_pop(&v), (int64_t)3);
    TEST_ASSERT_EQ_INT64(intvec_pop(&v), (int64_t)2);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)1);
    TEST_ASSERT_EQ_INT64(intvec_pop(&v), (int64_t)1);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)0);
    intvec_free(&v);
})

TEST(intvec_grows_by_doubling_across_several_steps, {
    intvec_t v;
    intvec_init(&v);
    uint64_t expected_cap = 0;
    int doublings = 0;
    for (int i = 0; i < GROWTH_ELEMENTS; i++) {
        intvec_push(&v, (int64_t)i * 3);
        if ((uint64_t)i + 1 > expected_cap) {
            expected_cap = expected_cap == 0 ? VEC_MIN_CAP : expected_cap * 2;
            doublings++;
        }
        TEST_ASSERT_EQ_UINT64(v.cap, expected_cap);
    }
    TEST_ASSERT_EQ_INT32(doublings, GROWTH_STEPS);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)GROWTH_ELEMENTS);
    for (int i = 0; i < GROWTH_ELEMENTS; i++) {
        TEST_ASSERT_EQ_INT64(v.items[i], (int64_t)i * 3);
    }
    intvec_free(&v);
})

TEST(intvec_reserve_is_a_no_op_when_there_is_room, {
    intvec_t v;
    intvec_init(&v);
    intvec_push(&v, 1);
    int64_t* items = v.items;
    intvec_reserve(&v, 15);
    TEST_ASSERT_TRUE(v.items == items);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)16);
    intvec_free(&v);
})

TEST(intvec_reserve_takes_the_exact_need_when_it_beats_doubling, {
    intvec_t v;
    intvec_init(&v);
    intvec_push(&v, 7);
    intvec_reserve(&v, 999);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)1000);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)1);
    TEST_ASSERT_EQ_INT64(v.items[0], (int64_t)7);
    intvec_free(&v);
})

TEST(intvec_growth_keeps_the_elements, {
    intvec_t v;
    intvec_init(&v);
    for (int i = 0; i < 33; i++) {
        intvec_push(&v, i);
    }
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)64);
    for (int i = 32; i >= 0; i--) {
        TEST_ASSERT_EQ_INT64(intvec_pop(&v), (int64_t)i);
    }
    intvec_free(&v);
})

TEST(intvec_free_empties_and_the_vector_is_reusable, {
    intvec_t v;
    intvec_init(&v);
    intvec_push(&v, 1);
    intvec_free(&v);
    TEST_ASSERT_NULL(v.items);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)0);
    intvec_free(&v);
    intvec_push(&v, 2);
    TEST_ASSERT_EQ_INT64(intvec_pop(&v), (int64_t)2);
    intvec_free(&v);
})

TEST(zero_initialized_intvec_is_valid, {
    intvec_t v;
    TEST_UNUSED(memset(&v, 0, sizeof v));
    intvec_push(&v, 5);
    TEST_ASSERT_EQ_INT64(intvec_pop(&v), (int64_t)5);
    intvec_free(&v);
})

static void pop_empty_intvec(void) {
    intvec_t v;
    intvec_init(&v);
    TEST_UNUSED(intvec_pop(&v));
}

TEST(intvec_pop_of_an_empty_vector_is_an_internal_error, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(pop_empty_intvec, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: intvec_pop: empty\n");
})

static void intvec_reserve_overflowing_count(void) {
    intvec_t v;
    intvec_init(&v);
    intvec_push(&v, 1);
    intvec_reserve(&v, UINT64_MAX);
}

static void intvec_reserve_overflowing_bytes(void) {
    intvec_t v;
    intvec_init(&v);
    intvec_reserve(&v, UINT64_MAX / sizeof(int64_t) + 1);
}

TEST(intvec_reserve_overflow_is_out_of_memory, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(intvec_reserve_overflowing_count, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: out of memory\n");
    TEST_ASSERT_EQ_INT32(run_forked(intvec_reserve_overflowing_bytes, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: out of memory\n");
})

/* ---- strmap: basics ----------------------------------------------------------- */

TEST(strmap_init_is_the_empty_map, {
    strmap_t m;
    strmap_init(&m);
    TEST_ASSERT_NULL(m.entries);
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(m.live, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)0);
    strmap_free(&m);
})

TEST(strmap_lookups_on_the_empty_map_find_nothing, {
    strmap_t m;
    strmap_init(&m);
    int64_t v = 77;
    TEST_ASSERT_FALSE(strmap_get(&m, s("a"), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)77);
    TEST_ASSERT_FALSE(strmap_has(&m, s("a")));
    TEST_ASSERT_FALSE(strmap_remove(&m, s("a")));
    TEST_ASSERT_FALSE(strmap_has(&m, s("")));
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)0);
    strmap_free(&m);
})

TEST(strmap_first_put_builds_a_table_of_sixteen, {
    strmap_t m;
    strmap_init(&m);
    TEST_ASSERT_TRUE(strmap_put(&m, s("a"), 1));
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)STRMAP_MIN_CAP);
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(m.live, (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)1);
    strmap_free(&m);
})

TEST(strmap_put_then_get_returns_the_value, {
    strmap_t m;
    strmap_init(&m);
    TEST_ASSERT_TRUE(strmap_put(&m, s("answer"), 42));
    int64_t v = 0;
    TEST_ASSERT_TRUE(strmap_get(&m, s("answer"), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)42);
    TEST_ASSERT_TRUE(strmap_has(&m, s("answer")));
    TEST_ASSERT_FALSE(strmap_has(&m, s("answe")));
    TEST_ASSERT_FALSE(strmap_has(&m, s("answers")));
    strmap_free(&m);
})

TEST(strmap_put_of_an_existing_key_replaces_and_returns_false, {
    strmap_t m;
    strmap_init(&m);
    TEST_ASSERT_TRUE(strmap_put(&m, s("k"), 1));
    TEST_ASSERT_FALSE(strmap_put(&m, s("k"), 2));
    TEST_ASSERT_FALSE(strmap_put(&m, s("k"), 3));
    int64_t v = 0;
    TEST_ASSERT_TRUE(strmap_get(&m, s("k"), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)3);
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)1);
    strmap_free(&m);
})

TEST(strmap_get_of_an_absent_key_leaves_out_unchanged, {
    strmap_t m;
    strmap_init(&m);
    TEST_ASSERT_TRUE(strmap_put(&m, s("present"), 1));
    int64_t v = -5;
    TEST_ASSERT_FALSE(strmap_get(&m, s("absent"), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)-5);
    strmap_free(&m);
})

TEST(strmap_stores_negative_and_extreme_values, {
    strmap_t m;
    strmap_init(&m);
    TEST_ASSERT_TRUE(strmap_put(&m, s("min"), INT64_MIN));
    TEST_ASSERT_TRUE(strmap_put(&m, s("max"), INT64_MAX));
    TEST_ASSERT_TRUE(strmap_put(&m, s("neg"), -1));
    int64_t v = 0;
    TEST_ASSERT_TRUE(strmap_get(&m, s("min"), &v));
    TEST_ASSERT_EQ_INT64(v, INT64_MIN);
    TEST_ASSERT_TRUE(strmap_get(&m, s("max"), &v));
    TEST_ASSERT_EQ_INT64(v, INT64_MAX);
    TEST_ASSERT_TRUE(strmap_get(&m, s("neg"), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)-1);
    strmap_free(&m);
})

TEST(strmap_accepts_the_empty_key, {
    strmap_t m;
    strmap_init(&m);
    TEST_ASSERT_TRUE(strmap_put(&m, s(""), 9));
    TEST_ASSERT_TRUE(strmap_has(&m, s("")));
    TEST_ASSERT_TRUE(strmap_has(&m, str_from_cstr(NULL)));
    int64_t v = 0;
    TEST_ASSERT_TRUE(strmap_get(&m, str_from_cstr(NULL), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)9);
    TEST_ASSERT_FALSE(strmap_put(&m, str_from_cstr(NULL), 10));
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)1);
    strmap_free(&m);
})

TEST(strmap_keys_are_borrowed_views, {
    strmap_t m;
    strmap_init(&m);
    char text[] = "borrowed";
    TEST_ASSERT_TRUE(strmap_put(&m, str_from_cstr(text), 1));
    const uint64_t i = slot_of(&m, s("borrowed"));
    TEST_ASSERT_TRUE(i < m.cap);
    TEST_ASSERT_TRUE(m.entries[i].key.ptr == text);
    TEST_ASSERT_EQ_UINT64(m.entries[i].hash, str_hash(s("borrowed")));
    TEST_ASSERT_EQ_INT32(m.entries[i].state, SLOT_FULL);
    strmap_free(&m);
})

TEST(strmap_key_equality_is_by_bytes_not_pointer, {
    strmap_t m;
    strmap_init(&m);
    char a[] = "same";
    char b[] = "same";
    TEST_ASSERT_TRUE(strmap_put(&m, str_from_cstr(a), 1));
    TEST_ASSERT_FALSE(strmap_put(&m, str_from_cstr(b), 2));
    int64_t v = 0;
    TEST_ASSERT_TRUE(strmap_get(&m, str_from_cstr(b), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)2);
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)1);
    strmap_free(&m);
})

TEST(strmap_distinguishes_keys_with_embedded_nul, {
    strmap_t m;
    strmap_init(&m);
    const char a[] = "x\0a";
    const char b[] = "x\0b";
    TEST_ASSERT_TRUE(strmap_put(&m, str_from_span(a, 3), 1));
    TEST_ASSERT_TRUE(strmap_put(&m, str_from_span(b, 3), 2));
    TEST_ASSERT_TRUE(strmap_put(&m, str_from_span(a, 1), 3));
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)3);
    int64_t v = 0;
    TEST_ASSERT_TRUE(strmap_get(&m, str_from_span(b, 3), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)2);
    TEST_ASSERT_TRUE(strmap_get(&m, s("x"), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)3);
    strmap_free(&m);
})

TEST(strmap_free_empties_and_the_map_is_reusable, {
    strmap_t m;
    strmap_init(&m);
    TEST_ASSERT_TRUE(strmap_put(&m, s("a"), 1));
    strmap_free(&m);
    TEST_ASSERT_NULL(m.entries);
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(m.live, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)0);
    TEST_ASSERT_FALSE(strmap_has(&m, s("a")));
    strmap_free(&m);
    TEST_ASSERT_TRUE(strmap_put(&m, s("a"), 2));
    int64_t v = 0;
    TEST_ASSERT_TRUE(strmap_get(&m, s("a"), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)2);
    strmap_free(&m);
})

TEST(zero_initialized_strmap_is_valid, {
    strmap_t m;
    TEST_UNUSED(memset(&m, 0, sizeof m));
    TEST_ASSERT_FALSE(strmap_has(&m, s("a")));
    TEST_ASSERT_TRUE(strmap_put(&m, s("a"), 1));
    TEST_ASSERT_TRUE(strmap_has(&m, s("a")));
    strmap_free(&m);
})

TEST(strmap_entry_layout_matches_stdlib, {
    /* StrMapEntry: key 0, val 16, hash 24, state 32, 40 bytes (stdlib.md
     * 2.8); StrMap is 32 bytes. */
    TEST_ASSERT_EQ_SIZE(sizeof(strmap_entry_t), (size_t)40);
    TEST_ASSERT_EQ_SIZE(offsetof(strmap_entry_t, val), (size_t)16);
    TEST_ASSERT_EQ_SIZE(offsetof(strmap_entry_t, hash), (size_t)24);
    TEST_ASSERT_EQ_SIZE(offsetof(strmap_entry_t, state), (size_t)32);
    TEST_ASSERT_EQ_SIZE(sizeof(strmap_t), (size_t)32);
    TEST_ASSERT_EQ_SIZE(sizeof(ptrvec_t), (size_t)24);
    TEST_ASSERT_EQ_SIZE(sizeof(intvec_t), (size_t)24);
})

/* ---- strmap: removal and tombstones ---------------------------------------- */

TEST(strmap_remove_returns_true_once_and_forgets_the_key, {
    strmap_t m;
    strmap_init(&m);
    TEST_ASSERT_TRUE(strmap_put(&m, s("a"), 1));
    TEST_ASSERT_TRUE(strmap_put(&m, s("b"), 2));
    TEST_ASSERT_TRUE(strmap_remove(&m, s("a")));
    TEST_ASSERT_FALSE(strmap_remove(&m, s("a")));
    TEST_ASSERT_FALSE(strmap_has(&m, s("a")));
    TEST_ASSERT_TRUE(strmap_has(&m, s("b")));
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)1);
    int64_t v = 0;
    TEST_ASSERT_FALSE(strmap_get(&m, s("a"), &v));
    strmap_free(&m);
})

TEST(strmap_remove_leaves_a_tombstone_and_used_unchanged, {
    strmap_t m;
    strmap_init(&m);
    TEST_ASSERT_TRUE(strmap_put(&m, s("a"), 1));
    TEST_ASSERT_TRUE(strmap_put(&m, s("b"), 2));
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)2);
    TEST_ASSERT_TRUE(strmap_remove(&m, s("a")));
    TEST_ASSERT_EQ_UINT64(m.live, (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)2);
    TEST_ASSERT_EQ_UINT64(count_full(&m), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(count_dead(&m), (uint64_t)1);
    strmap_free(&m);
})

TEST(strmap_reinserting_a_removed_key_reuses_its_tombstone, {
    strmap_t m;
    strmap_init(&m);
    TEST_ASSERT_TRUE(strmap_put(&m, s("a"), 1));
    const uint64_t slot = slot_of(&m, s("a"));
    TEST_ASSERT_TRUE(strmap_remove(&m, s("a")));
    TEST_ASSERT_EQ_INT32(m.entries[slot].state, SLOT_DEAD);
    TEST_ASSERT_TRUE(strmap_put(&m, s("a"), 2));
    TEST_ASSERT_EQ_UINT64(slot_of(&m, s("a")), slot);
    TEST_ASSERT_EQ_INT32(m.entries[slot].state, SLOT_FULL);
    TEST_ASSERT_EQ_UINT64(m.live, (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(count_dead(&m), (uint64_t)0);
    int64_t v = 0;
    TEST_ASSERT_TRUE(strmap_get(&m, s("a"), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)2);
    strmap_free(&m);
})

TEST(strmap_remove_every_key_leaves_only_tombstones, {
    strmap_t m;
    strmap_init(&m);
    char bufs[10][KEY_MAX];
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_TRUE(strmap_put(&m, key_of(bufs[i], KEY_MAX, i), i));
    }
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_TRUE(strmap_remove(&m, key_of(bufs[i], KEY_MAX, i)));
    }
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)10);
    TEST_ASSERT_EQ_UINT64(count_dead(&m), (uint64_t)10);
    TEST_ASSERT_EQ_UINT64(count_full(&m), (uint64_t)0);
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_FALSE(strmap_has(&m, key_of(bufs[i], KEY_MAX, i)));
    }
    strmap_free(&m);
})

TEST(strmap_rebuild_drops_tombstones, {
    /* Ten keys inserted and removed leave used = 10 in a table of 16. The
     * third further insert sees (12 + 1) * 4 > 16 * 3 and rebuilds: the
     * capacity stays 16 because (2 + 1) * 2 <= 16, and used drops to live. */
    strmap_t m;
    strmap_init(&m);
    char bufs[13][KEY_MAX];
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_TRUE(strmap_put(&m, key_of(bufs[i], KEY_MAX, i), i));
    }
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_TRUE(strmap_remove(&m, key_of(bufs[i], KEY_MAX, i)));
    }
    TEST_ASSERT_TRUE(strmap_put(&m, key_of(bufs[10], KEY_MAX, 10), 10));
    TEST_ASSERT_TRUE(strmap_put(&m, key_of(bufs[11], KEY_MAX, 11), 11));
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)12);
    TEST_ASSERT_TRUE(strmap_put(&m, key_of(bufs[12], KEY_MAX, 12), 12));
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(m.live, (uint64_t)3);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)3);
    TEST_ASSERT_EQ_UINT64(count_dead(&m), (uint64_t)0);
    for (int i = 10; i < 13; i++) {
        int64_t v = 0;
        TEST_ASSERT_TRUE(strmap_get(&m, key_of(bufs[i], KEY_MAX, i), &v));
        TEST_ASSERT_EQ_INT64(v, (int64_t)i);
    }
    strmap_free(&m);
})

TEST(strmap_grows_at_three_quarters_load, {
    /* The 13th insert sees (12 + 1) * 4 = 52 > 48 and rebuilds to 32. */
    strmap_t m;
    strmap_init(&m);
    char bufs[13][KEY_MAX];
    for (int i = 0; i < 12; i++) {
        TEST_ASSERT_TRUE(strmap_put(&m, key_of(bufs[i], KEY_MAX, i), i));
        TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)16);
    }
    TEST_ASSERT_TRUE(strmap_put(&m, key_of(bufs[12], KEY_MAX, 12), 12));
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)32);
    TEST_ASSERT_EQ_UINT64(m.live, (uint64_t)13);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)13);
    for (int i = 0; i < 13; i++) {
        int64_t v = 0;
        TEST_ASSERT_TRUE(strmap_get(&m, key_of(bufs[i], KEY_MAX, i), &v));
        TEST_ASSERT_EQ_INT64(v, (int64_t)i);
    }
    strmap_free(&m);
})

TEST(strmap_replace_runs_the_load_check_first, {
    strmap_t m;
    strmap_init(&m);
    char bufs[12][KEY_MAX];
    for (int i = 0; i < 12; i++) {
        TEST_ASSERT_TRUE(strmap_put(&m, key_of(bufs[i], KEY_MAX, i), i));
    }
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)12);
    /* A replace at used = 12 still runs the load check first, which
     * rebuilds at the same capacity (stdlib.md 2.8: before an insert). */
    TEST_ASSERT_FALSE(strmap_put(&m, key_of(bufs[0], KEY_MAX, 0), 100));
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)32);
    TEST_ASSERT_EQ_UINT64(m.live, (uint64_t)12);
    int64_t v = 0;
    TEST_ASSERT_TRUE(strmap_get(&m, key_of(bufs[0], KEY_MAX, 0), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)100);
    strmap_free(&m);
})

/* ---- strmap: collisions --------------------------------------------------------- */

/* Fills keys with COLLISION_KEYS generated names whose hashes all land on
 * slot 0 of a table of 16, deterministically; returns the number found. */
static int collision_keys(char bufs[][KEY_MAX], str_t* keys) {
    int found = 0;
    for (int i = 0; found < COLLISION_KEYS; i++) {
        char buf[KEY_MAX];
        TEST_UNUSED(snprintf(buf, sizeof buf, "c%d", i));
        if ((str_hash(str_from_cstr(buf)) & (STRMAP_MIN_CAP - 1U)) == 0) {
            TEST_UNUSED(snprintf(bufs[found], KEY_MAX, "%s", buf));
            keys[found] = str_from_cstr(bufs[found]);
            found++;
        }
    }
    return found;
}

TEST(collision_set_is_deterministic, {
    char bufs[COLLISION_KEYS][KEY_MAX];
    str_t keys[COLLISION_KEYS];
    TEST_ASSERT_EQ_INT32(collision_keys(bufs, keys), COLLISION_KEYS);
    TEST_ASSERT_EQ_STR(bufs[0], "c2");
    TEST_ASSERT_EQ_STR(bufs[1], "c19");
    TEST_ASSERT_EQ_STR(bufs[11], "c183");
    for (int i = 0; i < COLLISION_KEYS; i++) {
        TEST_ASSERT_EQ_UINT64(str_hash(keys[i]) & 15U, (uint64_t)0);
    }
})

TEST(colliding_keys_probe_into_consecutive_slots, {
    strmap_t m;
    strmap_init(&m);
    char bufs[COLLISION_KEYS][KEY_MAX];
    str_t keys[COLLISION_KEYS];
    TEST_UNUSED(collision_keys(bufs, keys));
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_TRUE(strmap_put(&m, keys[i], i));
        TEST_ASSERT_EQ_UINT64(slot_of(&m, keys[i]), (uint64_t)i);
    }
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)16);
    for (int i = 0; i < 8; i++) {
        int64_t v = -1;
        TEST_ASSERT_TRUE(strmap_get(&m, keys[i], &v));
        TEST_ASSERT_EQ_INT64(v, (int64_t)i);
    }
    strmap_free(&m);
})

TEST(lookups_skip_tombstones_in_the_probe_chain, {
    strmap_t m;
    strmap_init(&m);
    char bufs[COLLISION_KEYS][KEY_MAX];
    str_t keys[COLLISION_KEYS];
    TEST_UNUSED(collision_keys(bufs, keys));
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_TRUE(strmap_put(&m, keys[i], i));
    }
    TEST_ASSERT_TRUE(strmap_remove(&m, keys[0]));
    TEST_ASSERT_TRUE(strmap_remove(&m, keys[3]));
    TEST_ASSERT_TRUE(strmap_remove(&m, keys[4]));
    for (int i = 0; i < 8; i++) {
        const bool removed = i == 0 || i == 3 || i == 4;
        int64_t v = -1;
        TEST_ASSERT_TRUE(strmap_get(&m, keys[i], &v) == !removed);
        if (!removed) {
            TEST_ASSERT_EQ_INT64(v, (int64_t)i);
        }
    }
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)5);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)8);
    strmap_free(&m);
})

TEST(insert_reuses_the_first_tombstone_of_the_chain, {
    strmap_t m;
    strmap_init(&m);
    char bufs[COLLISION_KEYS][KEY_MAX];
    str_t keys[COLLISION_KEYS];
    TEST_UNUSED(collision_keys(bufs, keys));
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_TRUE(strmap_put(&m, keys[i], i));
    }
    TEST_ASSERT_TRUE(strmap_remove(&m, keys[2]));
    TEST_ASSERT_TRUE(strmap_remove(&m, keys[5]));
    /* A new colliding key lands on the first tombstone, slot 2, not slot 5
     * and not the empty slot 8. */
    TEST_ASSERT_TRUE(strmap_put(&m, keys[8], 8));
    TEST_ASSERT_EQ_UINT64(slot_of(&m, keys[8]), (uint64_t)2);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(m.live, (uint64_t)7);
    /* The next one takes slot 5; the one after that the empty slot 8. */
    TEST_ASSERT_TRUE(strmap_put(&m, keys[9], 9));
    TEST_ASSERT_EQ_UINT64(slot_of(&m, keys[9]), (uint64_t)5);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)8);
    TEST_ASSERT_TRUE(strmap_put(&m, keys[10], 10));
    TEST_ASSERT_EQ_UINT64(slot_of(&m, keys[10]), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)9);
    TEST_ASSERT_EQ_UINT64(m.live, (uint64_t)9);
    TEST_ASSERT_EQ_UINT64(count_dead(&m), (uint64_t)0);
    strmap_free(&m);
})

TEST(a_removed_key_past_a_tombstone_is_reinserted_at_the_first_tombstone, {
    strmap_t m;
    strmap_init(&m);
    char bufs[COLLISION_KEYS][KEY_MAX];
    str_t keys[COLLISION_KEYS];
    TEST_UNUSED(collision_keys(bufs, keys));
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_TRUE(strmap_put(&m, keys[i], i));
    }
    TEST_ASSERT_TRUE(strmap_remove(&m, keys[1]));
    TEST_ASSERT_TRUE(strmap_remove(&m, keys[4]));
    TEST_ASSERT_TRUE(strmap_put(&m, keys[4], 40));
    TEST_ASSERT_EQ_UINT64(slot_of(&m, keys[4]), (uint64_t)1);
    TEST_ASSERT_EQ_INT32(m.entries[4].state, SLOT_DEAD);
    int64_t v = 0;
    TEST_ASSERT_TRUE(strmap_get(&m, keys[4], &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)40);
    TEST_ASSERT_EQ_UINT64(m.live, (uint64_t)5);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)6);
    strmap_free(&m);
})

TEST(colliding_keys_survive_a_rebuild, {
    strmap_t m;
    strmap_init(&m);
    char bufs[COLLISION_KEYS][KEY_MAX];
    str_t keys[COLLISION_KEYS];
    TEST_UNUSED(collision_keys(bufs, keys));
    for (int i = 0; i < COLLISION_KEYS; i++) {
        TEST_ASSERT_TRUE(strmap_put(&m, keys[i], (int64_t)i * 10));
    }
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)16);
    char extra[KEY_MAX];
    TEST_ASSERT_TRUE(strmap_put(&m, key_of(extra, sizeof extra, 0), -1));
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)32);
    for (int i = 0; i < COLLISION_KEYS; i++) {
        int64_t v = 0;
        TEST_ASSERT_TRUE(strmap_get(&m, keys[i], &v));
        TEST_ASSERT_EQ_INT64(v, (int64_t)i * 10);
    }
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)COLLISION_KEYS + 1);
    strmap_free(&m);
})

/* ---- strmap: bulk ------------------------------------------------------------- */

TEST(thousands_of_keys_insert_and_read_back, {
    key_set_t ks;
    key_set_init(&ks);
    strmap_t m;
    strmap_init(&m);
    for (int i = 0; i < MANY_KEYS; i++) {
        TEST_ASSERT_TRUE(strmap_put(&m, ks.keys[i], (int64_t)i * 2));
        TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)i + 1);
    }
    for (int i = 0; i < MANY_KEYS; i++) {
        int64_t v = -1;
        TEST_ASSERT_TRUE(strmap_get(&m, ks.keys[i], &v));
        TEST_ASSERT_EQ_INT64(v, (int64_t)i * 2);
    }
    TEST_ASSERT_EQ_UINT64(count_full(&m), (uint64_t)MANY_KEYS);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)MANY_KEYS);
    /* 5000 live entries need (5000 + 1) * 4 <= cap * 3: cap = 8192. */
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)8192);
    char absent[KEY_MAX];
    TEST_ASSERT_FALSE(strmap_has(&m, key_of(absent, sizeof absent, MANY_KEYS)));
    TEST_ASSERT_FALSE(strmap_has(&m, s("k")));
    strmap_free(&m);
    key_set_free(&ks);
})

TEST(thousands_of_keys_overwrite_in_place, {
    key_set_t ks;
    key_set_init(&ks);
    strmap_t m;
    strmap_init(&m);
    for (int i = 0; i < MANY_KEYS; i++) {
        TEST_ASSERT_TRUE(strmap_put(&m, ks.keys[i], i));
    }
    const uint64_t cap = m.cap;
    for (int i = 0; i < MANY_KEYS; i++) {
        TEST_ASSERT_FALSE(strmap_put(&m, ks.keys[i], -(int64_t)i));
    }
    TEST_ASSERT_EQ_UINT64(m.cap, cap);
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)MANY_KEYS);
    for (int i = 0; i < MANY_KEYS; i++) {
        int64_t v = 1;
        TEST_ASSERT_TRUE(strmap_get(&m, ks.keys[i], &v));
        TEST_ASSERT_EQ_INT64(v, -(int64_t)i);
    }
    strmap_free(&m);
    key_set_free(&ks);
})

TEST(thousands_of_keys_remove_half_then_reinsert, {
    key_set_t ks;
    key_set_init(&ks);
    strmap_t m;
    strmap_init(&m);
    for (int i = 0; i < MANY_KEYS; i++) {
        TEST_ASSERT_TRUE(strmap_put(&m, ks.keys[i], i));
    }
    for (int i = 0; i < MANY_KEYS; i += 2) {
        TEST_ASSERT_TRUE(strmap_remove(&m, ks.keys[i]));
    }
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)MANY_KEYS / 2);
    TEST_ASSERT_EQ_UINT64(m.used, (uint64_t)MANY_KEYS);
    TEST_ASSERT_EQ_UINT64(count_dead(&m), (uint64_t)MANY_KEYS / 2);
    for (int i = 0; i < MANY_KEYS; i++) {
        int64_t v = -1;
        const bool present = strmap_get(&m, ks.keys[i], &v);
        TEST_ASSERT_TRUE(present == (i % 2 == 1));
        if (present) {
            TEST_ASSERT_EQ_INT64(v, (int64_t)i);
        }
    }
    for (int i = 0; i < MANY_KEYS; i += 2) {
        TEST_ASSERT_TRUE(strmap_put(&m, ks.keys[i], i + 1));
    }
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)MANY_KEYS);
    for (int i = 0; i < MANY_KEYS; i++) {
        int64_t v = -1;
        TEST_ASSERT_TRUE(strmap_get(&m, ks.keys[i], &v));
        TEST_ASSERT_EQ_INT64(v, (int64_t)(i % 2 == 0 ? i + 1 : i));
    }
    strmap_free(&m);
    key_set_free(&ks);
})

TEST(churn_of_inserts_and_removes_keeps_the_table_bounded, {
    /* Repeatedly filling and emptying a small working set must not let
     * tombstones grow the table: rebuilds keep cap at 16 for 4 live keys. */
    strmap_t m;
    strmap_init(&m);
    char bufs[8][KEY_MAX];
    for (int round = 0; round < 1000; round++) {
        for (int i = 0; i < 4; i++) {
            TEST_ASSERT_TRUE(strmap_put(&m, key_of(bufs[i], KEY_MAX, i), round));
        }
        for (int i = 0; i < 4; i++) {
            TEST_ASSERT_TRUE(strmap_remove(&m, key_of(bufs[i], KEY_MAX, i)));
        }
        TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)0);
        TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)16);
        TEST_ASSERT_TRUE(m.used <= 12);
    }
    strmap_free(&m);
})

TEST(iteration_in_table_order_visits_each_live_entry_once, {
    key_set_t ks;
    key_set_init(&ks);
    strmap_t m;
    strmap_init(&m);
    int64_t expected = 0;
    for (int i = 0; i < 1000; i++) {
        TEST_ASSERT_TRUE(strmap_put(&m, ks.keys[i], i));
        expected += i;
    }
    TEST_ASSERT_EQ_INT64(sum_values(&m), expected);
    for (int i = 0; i < 1000; i += 3) {
        TEST_ASSERT_TRUE(strmap_remove(&m, ks.keys[i]));
        expected -= i;
    }
    TEST_ASSERT_EQ_INT64(sum_values(&m), expected);
    TEST_ASSERT_EQ_UINT64(count_full(&m), strmap_count(&m));
    strmap_free(&m);
    key_set_free(&ks);
})

TEST(the_hashmap_ft_scenario, {
    /* The observable sequence of test/lang/programs/hashmap.ft, with the
     * counts that do not depend on that program's table policy. */
    strmap_t m;
    strmap_init(&m);
    for (int i = 0; i < WORD_COUNT; i++) {
        TEST_ASSERT_TRUE(strmap_put(&m, s(WORDS[i]), (int64_t)i * 10));
    }
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)11);
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)16);
    int found = 0;
    int missing = 0;
    int64_t v = 0;
    for (int i = 0; i < WORD_COUNT; i++) {
        if (strmap_get(&m, s(WORDS[i]), &v)) {
            found++;
        } else {
            missing++;
        }
    }
    TEST_ASSERT_EQ_INT32(found, 11);
    TEST_ASSERT_EQ_INT32(missing, 0);
    TEST_ASSERT_TRUE(strmap_get(&m, s("alpha"), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)0);
    TEST_ASSERT_TRUE(strmap_get(&m, s("delta"), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)30);
    TEST_ASSERT_TRUE(strmap_get(&m, s("kilo"), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)100);
    TEST_ASSERT_TRUE(strmap_remove(&m, s("delta")));
    TEST_ASSERT_TRUE(strmap_remove(&m, s("echo")));
    TEST_ASSERT_TRUE(strmap_remove(&m, s("kilo")));
    TEST_ASSERT_FALSE(strmap_remove(&m, s("zulu")));
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(count_dead(&m), (uint64_t)3);
    TEST_ASSERT_FALSE(strmap_get(&m, s("delta"), &v));
    TEST_ASSERT_FALSE(strmap_get(&m, s("zulu"), &v));
    TEST_ASSERT_TRUE(strmap_get(&m, s("juliet"), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)90);
    TEST_ASSERT_TRUE(strmap_put(&m, s("delta"), 300));
    TEST_ASSERT_FALSE(strmap_put(&m, s("alpha"), 5));
    TEST_ASSERT_TRUE(strmap_get(&m, s("delta"), &v));
    TEST_ASSERT_EQ_INT64(v, (int64_t)300);
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)9);
    TEST_ASSERT_EQ_UINT64(count_dead(&m), (uint64_t)2);
    TEST_ASSERT_EQ_INT64(sum_values(&m), (int64_t)685);
    strmap_free(&m);
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(strmap_count(&m), (uint64_t)0);
})

int main(int argc, char** argv) {
    TEST_INIT("containers", argc, argv);

    TEST_RUN(ptrvec_init_is_the_empty_vector);
    TEST_RUN(ptrvec_push_stores_pointers_in_order);
    TEST_RUN(ptrvec_first_push_allocates_sixteen_slots);
    TEST_RUN(ptrvec_pop_returns_the_last_element_first);
    TEST_RUN(ptrvec_stores_null_pointers);
    TEST_RUN(ptrvec_grows_by_doubling_across_several_steps);
    TEST_RUN(ptrvec_reserve_is_a_no_op_when_there_is_room);
    TEST_RUN(ptrvec_reserve_takes_the_exact_need_when_it_beats_doubling);
    TEST_RUN(ptrvec_reserve_of_zero_on_the_empty_vector_allocates_nothing);
    TEST_RUN(ptrvec_growth_keeps_the_elements);
    TEST_RUN(ptrvec_free_empties_and_the_vector_is_reusable);
    TEST_RUN(zero_initialized_ptrvec_is_valid);
    TEST_RUN(ptrvec_pop_of_an_empty_vector_is_an_internal_error);
    TEST_RUN(ptrvec_reserve_overflow_is_out_of_memory);

    TEST_RUN(intvec_init_is_the_empty_vector);
    TEST_RUN(intvec_push_stores_values_in_order);
    TEST_RUN(intvec_first_push_allocates_sixteen_slots);
    TEST_RUN(intvec_pop_returns_the_last_element_first);
    TEST_RUN(intvec_grows_by_doubling_across_several_steps);
    TEST_RUN(intvec_reserve_is_a_no_op_when_there_is_room);
    TEST_RUN(intvec_reserve_takes_the_exact_need_when_it_beats_doubling);
    TEST_RUN(intvec_growth_keeps_the_elements);
    TEST_RUN(intvec_free_empties_and_the_vector_is_reusable);
    TEST_RUN(zero_initialized_intvec_is_valid);
    TEST_RUN(intvec_pop_of_an_empty_vector_is_an_internal_error);
    TEST_RUN(intvec_reserve_overflow_is_out_of_memory);

    TEST_RUN(strmap_init_is_the_empty_map);
    TEST_RUN(strmap_lookups_on_the_empty_map_find_nothing);
    TEST_RUN(strmap_first_put_builds_a_table_of_sixteen);
    TEST_RUN(strmap_put_then_get_returns_the_value);
    TEST_RUN(strmap_put_of_an_existing_key_replaces_and_returns_false);
    TEST_RUN(strmap_get_of_an_absent_key_leaves_out_unchanged);
    TEST_RUN(strmap_stores_negative_and_extreme_values);
    TEST_RUN(strmap_accepts_the_empty_key);
    TEST_RUN(strmap_keys_are_borrowed_views);
    TEST_RUN(strmap_key_equality_is_by_bytes_not_pointer);
    TEST_RUN(strmap_distinguishes_keys_with_embedded_nul);
    TEST_RUN(strmap_free_empties_and_the_map_is_reusable);
    TEST_RUN(zero_initialized_strmap_is_valid);
    TEST_RUN(strmap_entry_layout_matches_stdlib);

    TEST_RUN(strmap_remove_returns_true_once_and_forgets_the_key);
    TEST_RUN(strmap_remove_leaves_a_tombstone_and_used_unchanged);
    TEST_RUN(strmap_reinserting_a_removed_key_reuses_its_tombstone);
    TEST_RUN(strmap_remove_every_key_leaves_only_tombstones);
    TEST_RUN(strmap_rebuild_drops_tombstones);
    TEST_RUN(strmap_grows_at_three_quarters_load);
    TEST_RUN(strmap_replace_runs_the_load_check_first);

    TEST_RUN(collision_set_is_deterministic);
    TEST_RUN(colliding_keys_probe_into_consecutive_slots);
    TEST_RUN(lookups_skip_tombstones_in_the_probe_chain);
    TEST_RUN(insert_reuses_the_first_tombstone_of_the_chain);
    TEST_RUN(a_removed_key_past_a_tombstone_is_reinserted_at_the_first_tombstone);
    TEST_RUN(colliding_keys_survive_a_rebuild);

    TEST_RUN(thousands_of_keys_insert_and_read_back);
    TEST_RUN(thousands_of_keys_overwrite_in_place);
    TEST_RUN(thousands_of_keys_remove_half_then_reinsert);
    TEST_RUN(churn_of_inserts_and_removes_keeps_the_table_bounded);
    TEST_RUN(iteration_in_table_order_visits_each_live_entry_once);
    TEST_RUN(the_hashmap_ft_scenario);

    TEST_EXIT();
}

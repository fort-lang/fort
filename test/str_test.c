// Unit tests of str.h: views, the FNV-1a hash of stdlib.md 2.5, owned
// copies and the string pool. The fatal exits and the allocation helpers
// have their own suite, mem_test.c, and the byte buffer sb_test.c.
#include "str.h"

#include <stdint.h>
#include <stdlib.h>

#include "fork.h"

#include "test.h"

enum { ERR_MAX = 256 };

// The FNV-1a vectors of stdlib.md 2.5 and test/lang/programs/hashmap.ft.
static const uint64_t FNV_EMPTY = 14695981039346656037ULL;
static const uint64_t FNV_A = 12638187200555641996ULL;
static const uint64_t FNV_AB = 620445648566982762ULL;
static const uint64_t FNV_FOOBAR = 9625390261332436968ULL;
static const uint64_t FNV_BYTE_FF = 12638352127299873646ULL;
static const uint64_t FNV_BYTE_00 = 12638153115695167455ULL;
static const uint64_t FNV_HELLO_WORLD = 8618312879776256743ULL;

// Single high bytes, as C strings; brace initializers with commas cannot sit
// inside a TEST body.
static const char BYTE_FF[] = {(char)0xFFU, '\0'};
static const char BYTE_80[] = {(char)0x80U, '\0'};

static str_t s(const char* text) {
    return str_from_cstr(text);
}

// Whether the view holds exactly the given text.
static bool view_is(str_t v, const char* text) {
    return str_eq(v, str_from_cstr(text));
}

// ---- constructors ---------------------------------------------------------------

TEST(from_cstr_measures_up_to_the_terminator, {
    const char* text = "hello";
    const str_t v = str_from_cstr(text);
    TEST_ASSERT_TRUE(v.ptr == text);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)5);
})

TEST(from_cstr_of_empty_string_has_length_zero_and_a_pointer, {
    const char* text = "";
    const str_t v = str_from_cstr(text);
    TEST_ASSERT_TRUE(v.ptr == text);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)0);
})

TEST(from_cstr_of_null_is_the_zero_view, {
    const str_t v = str_from_cstr(NULL);
    TEST_ASSERT_NULL(v.ptr);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)0);
})

TEST(from_cstr_stops_at_the_first_nul, {
    const char text[] = "ab\0cd";
    const str_t v = str_from_cstr(text);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)2);
})

TEST(from_span_keeps_pointer_and_length_as_given, {
    const char text[] = "ab\0cd";
    const str_t v = str_from_span(text, sizeof text - 1);
    TEST_ASSERT_TRUE(v.ptr == text);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)5);
    TEST_ASSERT_EQ_CHAR(v.ptr[2], '\0');
    TEST_ASSERT_EQ_CHAR(v.ptr[4], 'd');
})

TEST(from_span_of_a_substring_is_a_view_into_the_source, {
    const char* text = "hello world";
    const str_t world = str_from_span(text + 6, 5);
    TEST_ASSERT_TRUE(view_is(world, "world"));
    TEST_ASSERT_TRUE(world.ptr == text + 6);
})

// ---- equality ---------------------------------------------------------------------

TEST(eq_compares_bytes_and_length, {
    TEST_ASSERT_TRUE(str_eq(s("abc"), s("abc")));
    TEST_ASSERT_FALSE(str_eq(s("abc"), s("abd")));
    TEST_ASSERT_FALSE(str_eq(s("abc"), s("ab")));
    TEST_ASSERT_FALSE(str_eq(s("ab"), s("abc")));
    TEST_ASSERT_FALSE(str_eq(s("abc"), s("")));
})

TEST(eq_of_two_empty_views_holds_whatever_their_pointers, {
    TEST_ASSERT_TRUE(str_eq(s(""), s("")));
    TEST_ASSERT_TRUE(str_eq(str_from_cstr(NULL), s("")));
    TEST_ASSERT_TRUE(str_eq(str_from_cstr(NULL), str_from_cstr(NULL)));
})

TEST(eq_does_not_depend_on_the_pointer, {
    char a[] = "same";
    char b[] = "same";
    TEST_ASSERT_TRUE(a != b);
    TEST_ASSERT_TRUE(str_eq(str_from_cstr(a), str_from_cstr(b)));
})

TEST(eq_sees_embedded_nul_bytes, {
    const char a[] = "a\0b";
    const char b[] = "a\0c";
    TEST_ASSERT_FALSE(str_eq(str_from_span(a, 3), str_from_span(b, 3)));
    TEST_ASSERT_TRUE(str_eq(str_from_span(a, 2), str_from_span(b, 2)));
})

TEST(eq_is_symmetric_for_differing_lengths, {
    TEST_ASSERT_FALSE(str_eq(s("a"), s("aa")));
    TEST_ASSERT_FALSE(str_eq(s("aa"), s("a")));
})

// ---- order -------------------------------------------------------------------------

TEST(cmp_of_equal_strings_is_zero, {
    TEST_ASSERT_EQ_INT32(str_cmp(s("abc"), s("abc")), 0);
    TEST_ASSERT_EQ_INT32(str_cmp(s(""), s("")), 0);
    TEST_ASSERT_EQ_INT32(str_cmp(str_from_cstr(NULL), s("")), 0);
})

TEST(cmp_orders_by_the_first_differing_byte, {
    TEST_ASSERT_EQ_INT32(str_cmp(s("abc"), s("abd")), -1);
    TEST_ASSERT_EQ_INT32(str_cmp(s("abd"), s("abc")), 1);
    TEST_ASSERT_EQ_INT32(str_cmp(s("b"), s("a")), 1);
    TEST_ASSERT_EQ_INT32(str_cmp(s("a"), s("b")), -1);
})

TEST(cmp_puts_a_proper_prefix_first, {
    TEST_ASSERT_EQ_INT32(str_cmp(s("ab"), s("abc")), -1);
    TEST_ASSERT_EQ_INT32(str_cmp(s("abc"), s("ab")), 1);
    TEST_ASSERT_EQ_INT32(str_cmp(s(""), s("a")), -1);
    TEST_ASSERT_EQ_INT32(str_cmp(s("a"), s("")), 1);
})

TEST(cmp_uses_unsigned_byte_order, {
    // 0xFF sorts after 'a' as an unsigned byte; as a signed char it would
    // sort before.
    TEST_ASSERT_EQ_INT32(str_cmp(str_from_cstr(BYTE_FF), s("a")), 1);
    TEST_ASSERT_EQ_INT32(str_cmp(s("a"), str_from_cstr(BYTE_FF)), -1);
    TEST_ASSERT_EQ_INT32(str_cmp(str_from_cstr(BYTE_80), str_from_cstr(BYTE_FF)), -1);
})

TEST(cmp_only_returns_minus_one_zero_or_one, {
    TEST_ASSERT_EQ_INT32(str_cmp(s("a"), s("z")), -1);
    TEST_ASSERT_EQ_INT32(str_cmp(s("z"), s("a")), 1);
    TEST_ASSERT_EQ_INT32(str_cmp(s("a"), s("zzzzzzzz")), -1);
})

TEST(cmp_sees_embedded_nul_bytes, {
    const char a[] = "a\0b";
    const char b[] = "a\0c";
    TEST_ASSERT_EQ_INT32(str_cmp(str_from_span(a, 3), str_from_span(b, 3)), -1);
    TEST_ASSERT_EQ_INT32(str_cmp(str_from_span(a, 3), str_from_span(a, 3)), 0);
})

// ---- hash ---------------------------------------------------------------------------

TEST(hash_of_the_empty_string_is_the_offset_basis, {
    TEST_ASSERT_EQ_UINT64(str_hash(s("")), FNV_EMPTY);
    TEST_ASSERT_EQ_UINT64(str_hash(str_from_cstr(NULL)), FNV_EMPTY);
    TEST_ASSERT_EQ_UINT64(str_hash(s("")), (uint64_t)0xcbf29ce484222325ULL);
})

TEST(hash_of_a_matches_stdlib_and_hashmap_ft, {
    TEST_ASSERT_EQ_UINT64(str_hash(s("a")), FNV_A);
    TEST_ASSERT_EQ_UINT64(str_hash(s("a")), (uint64_t)0xaf63dc4c8601ec8cULL);
})

TEST(hash_matches_further_reference_vectors, {
    TEST_ASSERT_EQ_UINT64(str_hash(s("ab")), FNV_AB);
    TEST_ASSERT_EQ_UINT64(str_hash(s("foobar")), FNV_FOOBAR);
    TEST_ASSERT_EQ_UINT64(str_hash(s("hello world")), FNV_HELLO_WORLD);
})

TEST(hash_treats_bytes_as_unsigned,
     { TEST_ASSERT_EQ_UINT64(str_hash(str_from_cstr(BYTE_FF)), FNV_BYTE_FF); })

TEST(hash_covers_nul_bytes_inside_the_view, {
    TEST_ASSERT_EQ_UINT64(str_hash(str_from_span("", 1)), FNV_BYTE_00);
    TEST_ASSERT_TRUE(str_hash(str_from_span("", 1)) != FNV_EMPTY);
})

TEST(hash_is_a_step_by_step_fnv1a, {
    // One step from the hash of "a" gives the hash of "ab".
    uint64_t h = str_hash(s("a"));
    h ^= (uint64_t)'b';
    h *= 0x100000001b3ULL;
    TEST_ASSERT_EQ_UINT64(h, str_hash(s("ab")));
})

TEST(hash_depends_only_on_the_bytes, {
    char a[] = "same text";
    char b[] = "same text";
    TEST_ASSERT_EQ_UINT64(str_hash(str_from_cstr(a)), str_hash(str_from_cstr(b)));
    // A span in the middle of a longer text hashes like the text alone.
    TEST_ASSERT_EQ_UINT64(str_hash(str_from_span(&"xxsame textxx"[2], 9)),
                          str_hash(str_from_cstr("same text")));
})

TEST(hash_differs_for_differing_inputs, {
    TEST_ASSERT_TRUE(str_hash(s("a")) != str_hash(s("b")));
    TEST_ASSERT_TRUE(str_hash(s("ab")) != str_hash(s("ba")));
    TEST_ASSERT_TRUE(str_hash(s("a")) != str_hash(s("aa")));
})

// ---- starts_with and index_of -----------------------------------------------------

TEST(starts_with_accepts_an_empty_prefix, {
    TEST_ASSERT_TRUE(str_starts_with(s("abc"), s("")));
    TEST_ASSERT_TRUE(str_starts_with(s(""), s("")));
    TEST_ASSERT_TRUE(str_starts_with(str_from_cstr(NULL), s("")));
})

TEST(starts_with_matches_a_proper_prefix_and_the_whole, {
    TEST_ASSERT_TRUE(str_starts_with(s("abc"), s("a")));
    TEST_ASSERT_TRUE(str_starts_with(s("abc"), s("ab")));
    TEST_ASSERT_TRUE(str_starts_with(s("abc"), s("abc")));
})

TEST(starts_with_rejects_a_longer_or_differing_prefix, {
    TEST_ASSERT_FALSE(str_starts_with(s("abc"), s("abcd")));
    TEST_ASSERT_FALSE(str_starts_with(s("abc"), s("b")));
    TEST_ASSERT_FALSE(str_starts_with(s("abc"), s("abd")));
    TEST_ASSERT_FALSE(str_starts_with(s(""), s("a")));
})

TEST(index_of_finds_the_first_occurrence, {
    TEST_ASSERT_EQ_INT64(str_index_of(s("abcabc"), 'a'), (int64_t)0);
    TEST_ASSERT_EQ_INT64(str_index_of(s("abcabc"), 'b'), (int64_t)1);
    TEST_ASSERT_EQ_INT64(str_index_of(s("abcabc"), 'c'), (int64_t)2);
})

TEST(index_of_returns_minus_one_when_absent, {
    TEST_ASSERT_EQ_INT64(str_index_of(s("abc"), 'z'), (int64_t)-1);
    TEST_ASSERT_EQ_INT64(str_index_of(s(""), 'a'), (int64_t)-1);
    TEST_ASSERT_EQ_INT64(str_index_of(str_from_cstr(NULL), 'a'), (int64_t)-1);
})

TEST(index_of_can_find_a_nul_byte_inside_the_view, {
    const char text[] = "ab\0cd";
    TEST_ASSERT_EQ_INT64(str_index_of(str_from_span(text, 5), '\0'), (int64_t)2);
    TEST_ASSERT_EQ_INT64(str_index_of(str_from_span(text, 2), '\0'), (int64_t)-1);
})

TEST(index_of_stays_inside_the_view, {
    const char* text = "abcdef";
    TEST_ASSERT_EQ_INT64(str_index_of(str_from_span(text, 3), 'd'), (int64_t)-1);
    TEST_ASSERT_EQ_INT64(str_index_of(str_from_span(text, 4), 'd'), (int64_t)3);
})

// ---- dup and del -----------------------------------------------------------------

TEST(dup_makes_an_independent_nul_terminated_copy, {
    char text[] = "copy me";
    const str_t copy = str_dup(str_from_cstr(text));
    TEST_ASSERT_TRUE(copy.ptr != text);
    TEST_ASSERT_EQ_UINT64(copy.len, (uint64_t)7);
    TEST_ASSERT_TRUE(view_is(copy, "copy me"));
    TEST_ASSERT_EQ_CHAR(copy.ptr[copy.len], '\0');
    text[0] = 'X';
    TEST_ASSERT_EQ_CHAR(copy.ptr[0], 'c');
    str_del(copy);
})

TEST(dup_of_the_empty_string_is_a_non_null_empty_copy, {
    const str_t copy = str_dup(s(""));
    TEST_ASSERT_NONNULL(copy.ptr);
    TEST_ASSERT_EQ_UINT64(copy.len, (uint64_t)0);
    TEST_ASSERT_EQ_CHAR(copy.ptr[0], '\0');
    str_del(copy);
})

TEST(dup_of_the_zero_view_is_a_non_null_empty_copy, {
    const str_t copy = str_dup(str_from_cstr(NULL));
    TEST_ASSERT_NONNULL(copy.ptr);
    TEST_ASSERT_EQ_UINT64(copy.len, (uint64_t)0);
    str_del(copy);
})

TEST(dup_copies_embedded_nul_bytes, {
    const char text[] = "a\0b";
    const str_t copy = str_dup(str_from_span(text, 3));
    TEST_ASSERT_EQ_UINT64(copy.len, (uint64_t)3);
    TEST_ASSERT_EQ_CHAR(copy.ptr[1], '\0');
    TEST_ASSERT_EQ_CHAR(copy.ptr[2], 'b');
    TEST_ASSERT_EQ_CHAR(copy.ptr[3], '\0');
    str_del(copy);
})

TEST(del_of_the_zero_view_is_a_no_op, { str_del(str_from_cstr(NULL)); })

static void dup_of_the_largest_view(void) {
    // len + 1 for the terminator overflows before anything is allocated.
    TEST_UNUSED(str_dup(str_from_span("", UINT64_MAX)));
}

TEST(dup_of_a_view_whose_length_plus_one_overflows_is_out_of_memory, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(dup_of_the_largest_view, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: out of memory\n");
})

// ---- string pool -----------------------------------------------------------------

TEST(pool_init_is_the_empty_pool, {
    str_pool_t p;
    str_pool_init(&p);
    TEST_ASSERT_NULL(p.blocks);
    TEST_ASSERT_NULL(p.cur);
    TEST_ASSERT_EQ_UINT64(p.block_len, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(p.block_cap, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(p.used, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(p.cap, (uint64_t)0);
    str_pool_free(&p);
})

TEST(pool_free_of_an_empty_pool_is_harmless, {
    str_pool_t p;
    str_pool_init(&p);
    str_pool_free(&p);
    str_pool_free(&p);
    TEST_ASSERT_EQ_UINT64(p.block_len, (uint64_t)0);
})

TEST(pool_intern_copies_and_nul_terminates, {
    str_pool_t p;
    str_pool_init(&p);
    char text[] = "interned";
    const str_t v = str_pool_intern(&p, str_from_cstr(text));
    TEST_ASSERT_TRUE(v.ptr != text);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)8);
    TEST_ASSERT_TRUE(view_is(v, "interned"));
    TEST_ASSERT_EQ_CHAR(v.ptr[v.len], '\0');
    text[0] = 'X';
    TEST_ASSERT_EQ_CHAR(v.ptr[0], 'i');
    str_pool_free(&p);
})

TEST(pool_first_intern_opens_one_block, {
    str_pool_t p;
    str_pool_init(&p);
    TEST_UNUSED(str_pool_intern(&p, s("abc")));
    TEST_ASSERT_EQ_UINT64(p.block_len, (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(p.cap, (uint64_t)STR_POOL_BLOCK_SIZE);
    TEST_ASSERT_EQ_UINT64(p.used, (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(p.block_cap, (uint64_t)16);
    str_pool_free(&p);
})

TEST(pool_packs_strings_into_one_block, {
    str_pool_t p;
    str_pool_init(&p);
    const str_t a = str_pool_intern(&p, s("ab"));
    const str_t b = str_pool_intern(&p, s("cde"));
    TEST_ASSERT_TRUE(b.ptr == a.ptr + 3);
    TEST_ASSERT_EQ_UINT64(p.used, (uint64_t)7);
    TEST_ASSERT_EQ_UINT64(p.block_len, (uint64_t)1);
    str_pool_free(&p);
})

TEST(pool_intern_of_empty_string_takes_one_byte, {
    str_pool_t p;
    str_pool_init(&p);
    const str_t v = str_pool_intern(&p, s(""));
    TEST_ASSERT_NONNULL(v.ptr);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)0);
    TEST_ASSERT_EQ_CHAR(v.ptr[0], '\0');
    TEST_ASSERT_EQ_UINT64(p.used, (uint64_t)1);
    str_pool_free(&p);
})

TEST(pool_does_not_deduplicate, {
    str_pool_t p;
    str_pool_init(&p);
    const str_t a = str_pool_intern(&p, s("dup"));
    const str_t b = str_pool_intern(&p, s("dup"));
    TEST_ASSERT_TRUE(a.ptr != b.ptr);
    TEST_ASSERT_TRUE(str_eq(a, b));
    str_pool_free(&p);
})

TEST(pool_views_stay_stable_across_many_interns, {
    str_pool_t p;
    str_pool_init(&p);
    const str_t first = str_pool_intern(&p, s("first"));
    const char* first_ptr = first.ptr;
    const int count = 20000;
    for (int i = 0; i < count; i++) {
        TEST_UNUSED(str_pool_intern(&p, s("some more text in the pool")));
    }
    TEST_ASSERT_TRUE(first.ptr == first_ptr);
    TEST_ASSERT_TRUE(view_is(first, "first"));
    TEST_ASSERT_TRUE(p.block_len > 100);
    TEST_ASSERT_TRUE(p.block_cap >= p.block_len);
    str_pool_free(&p);
})

TEST(pool_opens_a_new_block_when_the_current_one_is_full, {
    str_pool_t p;
    str_pool_init(&p);
    // A string of STR_POOL_BLOCK_SIZE - 1 bytes fills a block exactly with
    // its terminator.
    char* big = mem_alloc(STR_POOL_BLOCK_SIZE);
    for (int i = 0; i < STR_POOL_BLOCK_SIZE - 1; i++) {
        big[i] = 'x';
    }
    const str_t a = str_pool_intern(&p, str_from_cstr(big));
    TEST_ASSERT_EQ_UINT64(p.block_len, (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(p.used, (uint64_t)STR_POOL_BLOCK_SIZE);
    const str_t b = str_pool_intern(&p, s("y"));
    TEST_ASSERT_EQ_UINT64(p.block_len, (uint64_t)2);
    TEST_ASSERT_EQ_UINT64(p.used, (uint64_t)2);
    TEST_ASSERT_TRUE(str_eq(a, str_from_cstr(big)));
    TEST_ASSERT_TRUE(view_is(b, "y"));
    mem_free(big);
    str_pool_free(&p);
})

TEST(pool_gives_an_oversized_string_its_own_block, {
    str_pool_t p;
    str_pool_init(&p);
    const str_t small = str_pool_intern(&p, s("small"));
    const uint64_t big_len = (uint64_t)STR_POOL_BLOCK_SIZE * 3U;
    char* big = mem_alloc(big_len + 1);
    for (uint64_t i = 0; i < big_len; i++) {
        big[i] = (char)('a' + (int)(i % 26));
    }
    const str_t v = str_pool_intern(&p, str_from_span(big, big_len));
    TEST_ASSERT_EQ_UINT64(v.len, big_len);
    TEST_ASSERT_TRUE(str_eq(v, str_from_span(big, big_len)));
    TEST_ASSERT_EQ_CHAR(v.ptr[big_len], '\0');
    TEST_ASSERT_EQ_UINT64(p.block_len, (uint64_t)2);
    // The small block stays current: the next small string lands after
    // "small".
    TEST_ASSERT_TRUE(p.cur == p.blocks[0]);
    TEST_ASSERT_EQ_UINT64(p.used, (uint64_t)6);
    const str_t next = str_pool_intern(&p, s("next"));
    TEST_ASSERT_TRUE(next.ptr == small.ptr + 6);
    TEST_ASSERT_EQ_UINT64(p.block_len, (uint64_t)2);
    mem_free(big);
    str_pool_free(&p);
})

TEST(pool_oversized_string_as_the_first_intern_does_not_become_current, {
    str_pool_t p;
    str_pool_init(&p);
    const uint64_t big_len = STR_POOL_BLOCK_SIZE + 1;
    char* big = mem_alloc(big_len + 1);
    for (uint64_t i = 0; i < big_len; i++) {
        big[i] = 'z';
    }
    const str_t v = str_pool_intern(&p, str_from_span(big, big_len));
    TEST_ASSERT_EQ_UINT64(p.block_len, (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(p.cap, (uint64_t)0);
    TEST_ASSERT_NULL(p.cur);
    const str_t small = str_pool_intern(&p, s("s"));
    TEST_ASSERT_EQ_UINT64(p.block_len, (uint64_t)2);
    TEST_ASSERT_EQ_UINT64(p.used, (uint64_t)2);
    TEST_ASSERT_TRUE(str_eq(v, str_from_span(big, big_len)));
    TEST_ASSERT_TRUE(view_is(small, "s"));
    mem_free(big);
    str_pool_free(&p);
})

TEST(pool_block_list_grows_past_sixteen_blocks, {
    str_pool_t p;
    str_pool_init(&p);
    const uint64_t big_len = STR_POOL_BLOCK_SIZE + 1;
    char* big = mem_alloc(big_len + 1);
    for (uint64_t i = 0; i < big_len; i++) {
        big[i] = 'q';
    }
    const int blocks = 40;
    str_t views[40];
    for (int i = 0; i < blocks; i++) {
        views[i] = str_pool_intern(&p, str_from_span(big, big_len));
    }
    TEST_ASSERT_EQ_UINT64(p.block_len, (uint64_t)blocks);
    TEST_ASSERT_EQ_UINT64(p.block_cap, (uint64_t)64);
    for (int i = 0; i < blocks; i++) {
        TEST_ASSERT_TRUE(str_eq(views[i], str_from_span(big, big_len)));
    }
    mem_free(big);
    str_pool_free(&p);
})

TEST(pool_free_empties_and_the_pool_is_reusable, {
    str_pool_t p;
    str_pool_init(&p);
    TEST_UNUSED(str_pool_intern(&p, s("one")));
    str_pool_free(&p);
    TEST_ASSERT_NULL(p.blocks);
    TEST_ASSERT_EQ_UINT64(p.block_len, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(p.used, (uint64_t)0);
    const str_t v = str_pool_intern(&p, s("two"));
    TEST_ASSERT_TRUE(view_is(v, "two"));
    TEST_ASSERT_EQ_UINT64(p.block_len, (uint64_t)1);
    str_pool_free(&p);
})

TEST(zero_initialized_pool_is_valid, {
    str_pool_t p;
    TEST_UNUSED(memset(&p, 0, sizeof p));
    const str_t v = str_pool_intern(&p, s("zero"));
    TEST_ASSERT_TRUE(view_is(v, "zero"));
    str_pool_free(&p);
})

int main(int argc, char** argv) {
    TEST_INIT("str", argc, argv);

    TEST_RUN(from_cstr_measures_up_to_the_terminator);
    TEST_RUN(from_cstr_of_empty_string_has_length_zero_and_a_pointer);
    TEST_RUN(from_cstr_of_null_is_the_zero_view);
    TEST_RUN(from_cstr_stops_at_the_first_nul);
    TEST_RUN(from_span_keeps_pointer_and_length_as_given);
    TEST_RUN(from_span_of_a_substring_is_a_view_into_the_source);

    TEST_RUN(eq_compares_bytes_and_length);
    TEST_RUN(eq_of_two_empty_views_holds_whatever_their_pointers);
    TEST_RUN(eq_does_not_depend_on_the_pointer);
    TEST_RUN(eq_sees_embedded_nul_bytes);
    TEST_RUN(eq_is_symmetric_for_differing_lengths);

    TEST_RUN(cmp_of_equal_strings_is_zero);
    TEST_RUN(cmp_orders_by_the_first_differing_byte);
    TEST_RUN(cmp_puts_a_proper_prefix_first);
    TEST_RUN(cmp_uses_unsigned_byte_order);
    TEST_RUN(cmp_only_returns_minus_one_zero_or_one);
    TEST_RUN(cmp_sees_embedded_nul_bytes);

    TEST_RUN(hash_of_the_empty_string_is_the_offset_basis);
    TEST_RUN(hash_of_a_matches_stdlib_and_hashmap_ft);
    TEST_RUN(hash_matches_further_reference_vectors);
    TEST_RUN(hash_treats_bytes_as_unsigned);
    TEST_RUN(hash_covers_nul_bytes_inside_the_view);
    TEST_RUN(hash_is_a_step_by_step_fnv1a);
    TEST_RUN(hash_depends_only_on_the_bytes);
    TEST_RUN(hash_differs_for_differing_inputs);

    TEST_RUN(starts_with_accepts_an_empty_prefix);
    TEST_RUN(starts_with_matches_a_proper_prefix_and_the_whole);
    TEST_RUN(starts_with_rejects_a_longer_or_differing_prefix);
    TEST_RUN(index_of_finds_the_first_occurrence);
    TEST_RUN(index_of_returns_minus_one_when_absent);
    TEST_RUN(index_of_can_find_a_nul_byte_inside_the_view);
    TEST_RUN(index_of_stays_inside_the_view);

    TEST_RUN(dup_makes_an_independent_nul_terminated_copy);
    TEST_RUN(dup_of_the_empty_string_is_a_non_null_empty_copy);
    TEST_RUN(dup_of_the_zero_view_is_a_non_null_empty_copy);
    TEST_RUN(dup_copies_embedded_nul_bytes);
    TEST_RUN(del_of_the_zero_view_is_a_no_op);
    TEST_RUN(dup_of_a_view_whose_length_plus_one_overflows_is_out_of_memory);

    TEST_RUN(pool_init_is_the_empty_pool);
    TEST_RUN(pool_free_of_an_empty_pool_is_harmless);
    TEST_RUN(pool_intern_copies_and_nul_terminates);
    TEST_RUN(pool_first_intern_opens_one_block);
    TEST_RUN(pool_packs_strings_into_one_block);
    TEST_RUN(pool_intern_of_empty_string_takes_one_byte);
    TEST_RUN(pool_does_not_deduplicate);
    TEST_RUN(pool_views_stay_stable_across_many_interns);
    TEST_RUN(pool_opens_a_new_block_when_the_current_one_is_full);
    TEST_RUN(pool_gives_an_oversized_string_its_own_block);
    TEST_RUN(pool_oversized_string_as_the_first_intern_does_not_become_current);
    TEST_RUN(pool_block_list_grows_past_sixteen_blocks);
    TEST_RUN(pool_free_empties_and_the_pool_is_reusable);
    TEST_RUN(zero_initialized_pool_is_valid);

    TEST_EXIT();
}

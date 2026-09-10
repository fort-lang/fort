// Unit tests of the byte buffer sb_t of str.h, which mirrors std::strbuf
// (stdlib.md 2.6): growth, appends, decimal formatting, views and copies.
#include <stdint.h>
#include <stdlib.h>

#include "fork.h"
#include "str.h"

#include "test.h"

enum { ERR_MAX = 256 };

static str_t s(const char* text) {
    return str_from_cstr(text);
}

// Whether the view holds exactly the given text.
static bool view_is(str_t v, const char* text) {
    return str_eq(v, str_from_cstr(text));
}

TEST(sb_init_is_the_empty_buffer, {
    sb_t b;
    sb_init(&b);
    TEST_ASSERT_NULL(b.data);
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)0);
    sb_free(&b);
})

TEST(sb_view_of_the_empty_buffer_is_the_zero_view, {
    sb_t b;
    sb_init(&b);
    const str_t v = sb_view(&b);
    TEST_ASSERT_NULL(v.ptr);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)0);
    TEST_ASSERT_TRUE(str_eq(v, s("")));
    sb_free(&b);
})

TEST(sb_push_appends_one_byte, {
    sb_t b;
    sb_init(&b);
    sb_push(&b, 'a');
    sb_push(&b, 'b');
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)2);
    TEST_ASSERT_TRUE(view_is(sb_view(&b), "ab"));
    sb_free(&b);
})

TEST(sb_first_push_allocates_the_minimum_capacity, {
    sb_t b;
    sb_init(&b);
    sb_push(&b, 'a');
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)SB_MIN_CAP);
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)16);
    sb_free(&b);
})

TEST(sb_grows_by_doubling_across_several_steps, {
    sb_t b;
    sb_init(&b);
    uint64_t expected_cap = 0;
    for (int i = 0; i < 1000; i++) {
        sb_push(&b, (char)('a' + (i % 26)));
        if ((uint64_t)i + 1 > expected_cap) {
            expected_cap = expected_cap == 0 ? SB_MIN_CAP : expected_cap * 2;
        }
        TEST_ASSERT_EQ_UINT64(b.cap, expected_cap);
    }
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)1000);
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)1024);
    for (int i = 0; i < 1000; i++) {
        TEST_ASSERT_EQ_CHAR(b.data[i], (char)('a' + (i % 26)));
    }
    sb_free(&b);
})

TEST(sb_reserve_is_a_no_op_when_there_is_room, {
    sb_t b;
    sb_init(&b);
    sb_push(&b, 'a');
    char* data = b.data;
    sb_reserve(&b, 15);
    TEST_ASSERT_TRUE(b.data == data);
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)16);
    sb_reserve(&b, 0);
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)16);
    sb_free(&b);
})

TEST(sb_reserve_on_the_empty_buffer_takes_the_minimum, {
    sb_t b;
    sb_init(&b);
    sb_reserve(&b, 1);
    TEST_ASSERT_NONNULL(b.data);
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)0);
    sb_free(&b);
})

TEST(sb_reserve_of_zero_on_the_empty_buffer_allocates_nothing, {
    sb_t b;
    sb_init(&b);
    sb_reserve(&b, 0);
    TEST_ASSERT_NULL(b.data);
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)0);
    sb_free(&b);
})

TEST(sb_reserve_takes_the_exact_need_when_it_beats_doubling, {
    sb_t b;
    sb_init(&b);
    sb_push(&b, 'a');
    sb_reserve(&b, 999);
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)1000);
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)1);
    TEST_ASSERT_EQ_CHAR(b.data[0], 'a');
    sb_free(&b);
})

TEST(sb_reserve_doubles_when_that_beats_the_need, {
    sb_t b;
    sb_init(&b);
    sb_reserve(&b, 100);
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)100);
    sb_reserve(&b, 101);
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)200);
    sb_free(&b);
})

static void reserve_past_the_end_of_memory(void) {
    sb_t b;
    sb_init(&b);
    sb_push(&b, 'a');
    sb_reserve(&b, UINT64_MAX); // len + extra overflows before any allocation
}

TEST(sb_reserve_overflow_is_out_of_memory, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(reserve_past_the_end_of_memory, err, sizeof err), 2);
    TEST_ASSERT_EQ_STR(err, "fort: error: out of memory\n");
})

TEST(sb_growth_moves_the_contents, {
    sb_t b;
    sb_init(&b);
    sb_append(&b, "0123456789abcdef");
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)16);
    sb_push(&b, 'g');
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)32);
    TEST_ASSERT_TRUE(view_is(sb_view(&b), "0123456789abcdefg"));
    sb_free(&b);
})

TEST(sb_append_adds_a_c_string_without_its_terminator, {
    sb_t b;
    sb_init(&b);
    sb_append(&b, "hello");
    sb_append(&b, ", ");
    sb_append(&b, "world");
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)12);
    TEST_ASSERT_TRUE(view_is(sb_view(&b), "hello, world"));
    sb_free(&b);
})

TEST(sb_append_of_the_empty_string_changes_nothing, {
    sb_t b;
    sb_init(&b);
    sb_append(&b, "");
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)0);
    TEST_ASSERT_NULL(b.data);
    sb_append(&b, "x");
    sb_append(&b, "");
    TEST_ASSERT_TRUE(view_is(sb_view(&b), "x"));
    sb_free(&b);
})

TEST(sb_append_str_adds_the_bytes_of_a_view, {
    sb_t b;
    sb_init(&b);
    const char text[] = "a\0b";
    sb_append_str(&b, str_from_span(text, 3));
    sb_append_str(&b, s("c"));
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)4);
    TEST_ASSERT_EQ_CHAR(b.data[0], 'a');
    TEST_ASSERT_EQ_CHAR(b.data[1], '\0');
    TEST_ASSERT_EQ_CHAR(b.data[2], 'b');
    TEST_ASSERT_EQ_CHAR(b.data[3], 'c');
    sb_free(&b);
})

TEST(sb_append_str_of_the_zero_view_changes_nothing, {
    sb_t b;
    sb_init(&b);
    sb_append_str(&b, str_from_cstr(NULL));
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)0);
    TEST_ASSERT_NULL(b.data);
    sb_free(&b);
})

TEST(sb_append_str_of_a_long_view_grows_in_one_step, {
    sb_t b;
    sb_init(&b);
    char* text = mem_alloc(5000);
    for (int i = 0; i < 5000; i++) {
        text[i] = 'k';
    }
    sb_append_str(&b, str_from_span(text, 5000));
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)5000);
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)5000);
    TEST_ASSERT_TRUE(str_eq(sb_view(&b), str_from_span(text, 5000)));
    mem_free(text);
    sb_free(&b);
})

TEST(sb_append_u64_writes_decimal_digits, {
    sb_t b;
    sb_init(&b);
    sb_append_u64(&b, 0);
    sb_push(&b, ' ');
    sb_append_u64(&b, 9);
    sb_push(&b, ' ');
    sb_append_u64(&b, 10);
    sb_push(&b, ' ');
    sb_append_u64(&b, 12345);
    sb_push(&b, ' ');
    sb_append_u64(&b, 1000000);
    TEST_ASSERT_EQ_STR(sb_cstr(&b), "0 9 10 12345 1000000");
    sb_free(&b);
})

TEST(sb_append_u64_handles_the_maximum, {
    sb_t b;
    sb_init(&b);
    sb_append_u64(&b, UINT64_MAX);
    TEST_ASSERT_EQ_STR(sb_cstr(&b), "18446744073709551615");
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)20);
    sb_free(&b);
})

TEST(sb_append_u64_handles_powers_of_ten_and_neighbors, {
    sb_t b;
    sb_init(&b);
    sb_append_u64(&b, 99);
    sb_push(&b, ' ');
    sb_append_u64(&b, 100);
    sb_push(&b, ' ');
    sb_append_u64(&b, 101);
    sb_push(&b, ' ');
    sb_append_u64(&b, 9999999999ULL);
    sb_push(&b, ' ');
    sb_append_u64(&b, 10000000000ULL);
    TEST_ASSERT_EQ_STR(sb_cstr(&b), "99 100 101 9999999999 10000000000");
    sb_free(&b);
})

TEST(sb_append_i64_writes_sign_and_digits, {
    sb_t b;
    sb_init(&b);
    sb_append_i64(&b, 0);
    sb_push(&b, ' ');
    sb_append_i64(&b, -1);
    sb_push(&b, ' ');
    sb_append_i64(&b, 42);
    sb_push(&b, ' ');
    sb_append_i64(&b, -42);
    sb_push(&b, ' ');
    sb_append_i64(&b, -1000000);
    TEST_ASSERT_EQ_STR(sb_cstr(&b), "0 -1 42 -42 -1000000");
    sb_free(&b);
})

TEST(sb_append_i64_handles_the_extremes, {
    sb_t b;
    sb_init(&b);
    sb_append_i64(&b, INT64_MIN);
    sb_push(&b, ' ');
    sb_append_i64(&b, INT64_MAX);
    sb_push(&b, ' ');
    sb_append_i64(&b, INT64_MIN + 1);
    TEST_ASSERT_EQ_STR(sb_cstr(&b),
                       "-9223372036854775808 9223372036854775807 -9223372036854775807");
    sb_free(&b);
})

TEST(sb_append_i64_matches_append_u64_for_non_negatives, {
    sb_t a;
    sb_t b;
    sb_init(&a);
    sb_init(&b);
    for (int64_t v = 0; v < 2000; v += 7) {
        sb_append_i64(&a, v);
        sb_append_u64(&b, (uint64_t)v);
        sb_push(&a, ',');
        sb_push(&b, ',');
    }
    TEST_ASSERT_TRUE(str_eq(sb_view(&a), sb_view(&b)));
    sb_free(&a);
    sb_free(&b);
})

TEST(sb_clear_keeps_the_storage, {
    sb_t b;
    sb_init(&b);
    sb_append(&b, "some text");
    char* data = b.data;
    sb_clear(&b);
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)16);
    TEST_ASSERT_TRUE(b.data == data);
    sb_append(&b, "new");
    TEST_ASSERT_TRUE(view_is(sb_view(&b), "new"));
    TEST_ASSERT_TRUE(b.data == data);
    sb_free(&b);
})

TEST(sb_view_aliases_the_contents, {
    sb_t b;
    sb_init(&b);
    sb_append(&b, "alias");
    const str_t v = sb_view(&b);
    TEST_ASSERT_TRUE(v.ptr == b.data);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)5);
    TEST_ASSERT_TRUE(view_is(v, "alias"));
    sb_free(&b);
})

TEST(sb_cstr_terminates_without_counting_the_nul, {
    sb_t b;
    sb_init(&b);
    sb_append(&b, "text");
    const char* c = sb_cstr(&b);
    TEST_ASSERT_TRUE(c == b.data);
    TEST_ASSERT_EQ_STR(c, "text");
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)4);
    sb_push(&b, '!');
    TEST_ASSERT_EQ_STR(sb_cstr(&b), "text!");
    sb_free(&b);
})

TEST(sb_cstr_of_the_empty_buffer_is_an_empty_string, {
    sb_t b;
    sb_init(&b);
    TEST_ASSERT_EQ_STR(sb_cstr(&b), "");
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)16);
    sb_free(&b);
})

TEST(sb_cstr_grows_when_the_buffer_is_exactly_full, {
    sb_t b;
    sb_init(&b);
    sb_append(&b, "0123456789abcdef");
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)16);
    TEST_ASSERT_EQ_STR(sb_cstr(&b), "0123456789abcdef");
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)32);
    sb_free(&b);
})

TEST(sb_take_returns_an_exact_copy_and_empties_the_buffer, {
    sb_t b;
    sb_init(&b);
    sb_append(&b, "taken");
    char* data = b.data;
    const str_t copy = sb_take(&b);
    TEST_ASSERT_TRUE(copy.ptr != data);
    TEST_ASSERT_EQ_UINT64(copy.len, (uint64_t)5);
    TEST_ASSERT_TRUE(view_is(copy, "taken"));
    TEST_ASSERT_EQ_CHAR(copy.ptr[5], '\0');
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)0);
    TEST_ASSERT_TRUE(b.data == data);
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)16);
    str_del(copy);
    sb_free(&b);
})

TEST(sb_take_copy_survives_later_buffer_use, {
    sb_t b;
    sb_init(&b);
    sb_append(&b, "first");
    const str_t copy = sb_take(&b);
    sb_append(&b, "second, longer than the first one by far");
    TEST_ASSERT_TRUE(view_is(copy, "first"));
    str_del(copy);
    sb_free(&b);
})

TEST(sb_take_of_the_empty_buffer_is_a_non_null_empty_copy, {
    sb_t b;
    sb_init(&b);
    const str_t copy = sb_take(&b);
    TEST_ASSERT_NONNULL(copy.ptr);
    TEST_ASSERT_EQ_UINT64(copy.len, (uint64_t)0);
    TEST_ASSERT_EQ_CHAR(copy.ptr[0], '\0');
    str_del(copy);
    sb_free(&b);
})

TEST(sb_free_empties_and_the_buffer_is_reusable, {
    sb_t b;
    sb_init(&b);
    sb_append(&b, "before");
    sb_free(&b);
    TEST_ASSERT_NULL(b.data);
    TEST_ASSERT_EQ_UINT64(b.len, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(b.cap, (uint64_t)0);
    sb_free(&b);
    sb_append(&b, "after");
    TEST_ASSERT_TRUE(view_is(sb_view(&b), "after"));
    sb_free(&b);
})

TEST(zero_initialized_buffer_is_valid, {
    sb_t b;
    TEST_UNUSED(memset(&b, 0, sizeof b));
    sb_append(&b, "zero");
    TEST_ASSERT_TRUE(view_is(sb_view(&b), "zero"));
    sb_free(&b);
})

TEST(sb_builds_a_mixed_line, {
    sb_t b;
    sb_init(&b);
    sb_append(&b, "    mov ");
    sb_append_str(&b, s("rax"));
    sb_append(&b, ", ");
    sb_append_i64(&b, -42);
    sb_push(&b, '\n');
    TEST_ASSERT_EQ_STR(sb_cstr(&b), "    mov rax, -42\n");
    sb_free(&b);
})

int main(int argc, char** argv) {
    TEST_INIT("sb", argc, argv);

    TEST_RUN(sb_init_is_the_empty_buffer);
    TEST_RUN(sb_view_of_the_empty_buffer_is_the_zero_view);
    TEST_RUN(sb_push_appends_one_byte);
    TEST_RUN(sb_first_push_allocates_the_minimum_capacity);
    TEST_RUN(sb_grows_by_doubling_across_several_steps);
    TEST_RUN(sb_reserve_is_a_no_op_when_there_is_room);
    TEST_RUN(sb_reserve_on_the_empty_buffer_takes_the_minimum);
    TEST_RUN(sb_reserve_of_zero_on_the_empty_buffer_allocates_nothing);
    TEST_RUN(sb_reserve_takes_the_exact_need_when_it_beats_doubling);
    TEST_RUN(sb_reserve_doubles_when_that_beats_the_need);
    TEST_RUN(sb_reserve_overflow_is_out_of_memory);
    TEST_RUN(sb_growth_moves_the_contents);
    TEST_RUN(sb_append_adds_a_c_string_without_its_terminator);
    TEST_RUN(sb_append_of_the_empty_string_changes_nothing);
    TEST_RUN(sb_append_str_adds_the_bytes_of_a_view);
    TEST_RUN(sb_append_str_of_the_zero_view_changes_nothing);
    TEST_RUN(sb_append_str_of_a_long_view_grows_in_one_step);
    TEST_RUN(sb_append_u64_writes_decimal_digits);
    TEST_RUN(sb_append_u64_handles_the_maximum);
    TEST_RUN(sb_append_u64_handles_powers_of_ten_and_neighbors);
    TEST_RUN(sb_append_i64_writes_sign_and_digits);
    TEST_RUN(sb_append_i64_handles_the_extremes);
    TEST_RUN(sb_append_i64_matches_append_u64_for_non_negatives);
    TEST_RUN(sb_clear_keeps_the_storage);
    TEST_RUN(sb_view_aliases_the_contents);
    TEST_RUN(sb_cstr_terminates_without_counting_the_nul);
    TEST_RUN(sb_cstr_of_the_empty_buffer_is_an_empty_string);
    TEST_RUN(sb_cstr_grows_when_the_buffer_is_exactly_full);
    TEST_RUN(sb_take_returns_an_exact_copy_and_empties_the_buffer);
    TEST_RUN(sb_take_copy_survives_later_buffer_use);
    TEST_RUN(sb_take_of_the_empty_buffer_is_a_non_null_empty_copy);
    TEST_RUN(sb_free_empties_and_the_buffer_is_reusable);
    TEST_RUN(zero_initialized_buffer_is_valid);
    TEST_RUN(sb_builds_a_mixed_line);

    TEST_EXIT();
}

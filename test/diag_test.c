// Tests diagnostic text, error counts, capture, and message construction.
#include "diag.h"

#include <stdint.h>
#include <stdlib.h>

#include "fork.h"

#include "test.h"

enum { ERR_MAX = 1024 };

// Positions for helpers that run outside a TEST body.
enum { USE_LINE = 7, USE_COL = 5, DECL_LINE = 3, DECL_COL = 9 };

// Every test that captures starts from a clean count and an empty sink and
// restores stderr at the end.
static sb_t sink;

static void begin_capture(void) {
    sb_init(&sink);
    diag_reset();
    diag_capture(&sink);
}

static void end_capture(void) {
    diag_capture(NULL);
    sb_free(&sink);
    diag_reset();
}

// Returns the current captured text.
// A diagnostic write or `end_capture` invalidates the result.
static const char* captured(void) {
    return sb_cstr(&sink);
}

// ---- loc ---------------------------------------------------------------------------

TEST(loc_make_fills_every_field, {
    const loc_t loc = loc_make("main.ft", 7, 5);
    TEST_ASSERT_EQ_STR(loc.file, "main.ft");
    TEST_ASSERT_EQ_INT32((int32_t)loc.line, 7);
    TEST_ASSERT_EQ_INT32((int32_t)loc.col, 5);
})

// A position with no extent yet is the empty range at that position.
TEST(loc_make_is_the_empty_range_at_its_position, {
    const loc_t loc = loc_make("main.ft", 7, 5);
    TEST_ASSERT_EQ_INT32((int32_t)loc.end_line, 7);
    TEST_ASSERT_EQ_INT32((int32_t)loc.end_col, 5);
})

TEST(loc_holds_the_largest_line_and_column, {
    const loc_t loc = loc_make("f", UINT32_MAX, UINT32_MAX);
    TEST_ASSERT_EQ_UINT64((uint64_t)loc.line, (uint64_t)UINT32_MAX);
    TEST_ASSERT_EQ_UINT64((uint64_t)loc.col, (uint64_t)UINT32_MAX);
    TEST_ASSERT_EQ_UINT64((uint64_t)loc.end_line, (uint64_t)UINT32_MAX);
    TEST_ASSERT_EQ_UINT64((uint64_t)loc.end_col, (uint64_t)UINT32_MAX);
})

TEST(loc_range_fills_every_field, {
    const loc_t loc = loc_range("main.ft", 7, 5, 9, 12);
    TEST_ASSERT_EQ_STR(loc.file, "main.ft");
    TEST_ASSERT_EQ_INT32((int32_t)loc.line, 7);
    TEST_ASSERT_EQ_INT32((int32_t)loc.col, 5);
    TEST_ASSERT_EQ_INT32((int32_t)loc.end_line, 9);
    TEST_ASSERT_EQ_INT32((int32_t)loc.end_col, 12);
})

// The end is exclusive, so a token of n bytes ends n columns further on.
TEST(a_token_range_ends_one_past_its_last_byte, {
    const loc_t loc = loc_range("t.ft", 1, 5, 1, 5 + 3);
    TEST_ASSERT_EQ_INT32((int32_t)(loc.end_col - loc.col), 3);
})

// ---- comparing and extending ranges ----------------------------------------------

TEST(starts_compare_the_line_first, {
    TEST_ASSERT_TRUE(loc_starts_at_or_before(loc_make("a.ft", 1, 100), loc_make("a.ft", 2, 1)));
    TEST_ASSERT_FALSE(loc_starts_at_or_before(loc_make("a.ft", 2, 1), loc_make("a.ft", 1, 100)));
})

TEST(starts_compare_the_column_within_a_line, {
    TEST_ASSERT_TRUE(loc_starts_at_or_before(loc_make("a.ft", 3, 4), loc_make("a.ft", 3, 5)));
    TEST_ASSERT_FALSE(loc_starts_at_or_before(loc_make("a.ft", 3, 5), loc_make("a.ft", 3, 4)));
})

TEST(a_start_is_at_or_before_itself,
     { TEST_ASSERT_TRUE(loc_starts_at_or_before(loc_make("a.ft", 3, 4), loc_make("a.ft", 3, 4))); })

// Ends compare the same way, and the start plays no part in it.
TEST(ends_compare_independently_of_the_starts, {
    const loc_t early = loc_range("a.ft", 1, 1, 1, 9);
    const loc_t late = loc_range("a.ft", 5, 1, 5, 2);
    TEST_ASSERT_TRUE(loc_ends_at_or_before(early, late));
    TEST_ASSERT_FALSE(loc_ends_at_or_before(late, early));
    TEST_ASSERT_TRUE(loc_ends_at_or_before(early, early));
})

TEST(a_range_is_ordered_when_its_end_is_not_before_its_start, {
    TEST_ASSERT_TRUE(loc_is_ordered(loc_range("a.ft", 1, 1, 3, 2)));
    TEST_ASSERT_TRUE(loc_is_ordered(loc_make("a.ft", 4, 7)));
    TEST_ASSERT_FALSE(loc_is_ordered(loc_range("a.ft", 3, 2, 1, 1)));
})

TEST(loc_extend_moves_the_end_and_keeps_the_start, {
    const loc_t joined = loc_extend(loc_range("a.ft", 1, 5, 1, 6), loc_range("a.ft", 1, 9, 1, 12));
    TEST_ASSERT_EQ_INT32((int32_t)joined.line, 1);
    TEST_ASSERT_EQ_INT32((int32_t)joined.col, 5);
    TEST_ASSERT_EQ_INT32((int32_t)joined.end_line, 1);
    TEST_ASSERT_EQ_INT32((int32_t)joined.end_col, 12);
})

TEST(loc_extend_spans_lines, {
    const loc_t joined = loc_extend(loc_range("a.ft", 2, 1, 2, 3), loc_range("a.ft", 8, 1, 8, 2));
    TEST_ASSERT_EQ_INT32((int32_t)joined.line, 2);
    TEST_ASSERT_EQ_INT32((int32_t)joined.col, 1);
    TEST_ASSERT_EQ_INT32((int32_t)joined.end_line, 8);
    TEST_ASSERT_EQ_INT32((int32_t)joined.end_col, 2);
})

// Joining never shrinks a range, so a range already covering the second one
// comes back unchanged. Extending never moves the start, so a range that
// already covers the other one comes back unchanged: a start further left is
// not adopted.
TEST(loc_extend_keeps_the_later_end_and_never_widens_leftwards, {
    const loc_t right = loc_range("a.ft", 1, 9, 1, 12);
    const loc_t extended = loc_extend(right, loc_range("a.ft", 1, 1, 1, 4));
    TEST_ASSERT_EQ_INT32((int32_t)extended.col, 9);
    TEST_ASSERT_EQ_INT32((int32_t)extended.end_col, 12);
})

TEST(loc_extend_keeps_the_later_end, {
    const loc_t wide = loc_range("a.ft", 1, 1, 5, 2);
    const loc_t joined = loc_extend(wide, loc_range("a.ft", 2, 1, 2, 4));
    TEST_ASSERT_EQ_INT32((int32_t)joined.end_line, 5);
    TEST_ASSERT_EQ_INT32((int32_t)joined.end_col, 2);
})

// Extending again with a token the range already covers changes nothing, so a
// node finished at several levels keeps one range.
TEST(loc_extend_is_idempotent, {
    const loc_t a = loc_range("a.ft", 1, 1, 1, 8);
    const loc_t once = loc_extend(a, loc_range("a.ft", 1, 5, 1, 8));
    const loc_t twice = loc_extend(once, loc_range("a.ft", 1, 5, 1, 8));
    TEST_ASSERT_EQ_INT32((int32_t)twice.line, (int32_t)once.line);
    TEST_ASSERT_EQ_INT32((int32_t)twice.col, (int32_t)once.col);
    TEST_ASSERT_EQ_INT32((int32_t)twice.end_line, (int32_t)once.end_line);
    TEST_ASSERT_EQ_INT32((int32_t)twice.end_col, (int32_t)once.end_col);
})

TEST(loc_extend_with_an_empty_range_at_the_same_end_changes_nothing, {
    const loc_t a = loc_range("a.ft", 1, 1, 1, 4);
    const loc_t joined = loc_extend(a, loc_make("a.ft", 1, 4));
    TEST_ASSERT_EQ_INT32((int32_t)joined.col, 1);
    TEST_ASSERT_EQ_INT32((int32_t)joined.end_col, 4);
})

TEST(loc_extend_keeps_the_file_of_the_first, {
    const loc_t joined = loc_extend(loc_make("a.ft", 1, 1), loc_range("b.ft", 1, 2, 1, 3));
    TEST_ASSERT_EQ_STR(joined.file, "a.ft");
})

TEST(extending_an_empty_range_with_a_token_gives_the_token, {
    const loc_t joined = loc_extend(loc_make("a.ft", 4, 7), loc_range("a.ft", 4, 7, 4, 10));
    TEST_ASSERT_EQ_INT32((int32_t)joined.col, 7);
    TEST_ASSERT_EQ_INT32((int32_t)joined.end_col, 10);
})

// ---- errors and notes ------------------------------------------------------------

TEST(error_prints_the_toolchain_format, {
    begin_capture();
    diag_error(loc_make("main.ft", 7, 5), "cannot assign to immutable 'x'");
    TEST_ASSERT_EQ_STR(captured(), "main.ft:7:5: error: cannot assign to immutable 'x'\n");
    end_capture();
})

TEST(note_prints_the_toolchain_format, {
    begin_capture();
    diag_note(loc_make("main.ft", 3, 9), "'x' declared here");
    TEST_ASSERT_EQ_STR(captured(), "main.ft:3:9: note: 'x' declared here\n");
    end_capture();
})

TEST(error_then_note_are_consecutive_lines, {
    begin_capture();
    diag_error(loc_make("main.ft", 7, 5), "cannot assign to immutable 'x'");
    diag_note(loc_make("main.ft", 3, 9), "'x' declared here");
    diag_error(loc_make("util.ft", 12, 23), "expected ';'");
    TEST_ASSERT_EQ_STR(captured(),
                       "main.ft:7:5: error: cannot assign to immutable 'x'\n"
                       "main.ft:3:9: note: 'x' declared here\n"
                       "util.ft:12:23: error: expected ';'\n");
    end_capture();
})

TEST(errors_are_counted_and_notes_are_not, {
    begin_capture();
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)0);
    diag_error(loc_make("a.ft", 1, 1), "one");
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)1);
    diag_note(loc_make("a.ft", 1, 1), "a note");
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)1);
    diag_error(loc_make("a.ft", 2, 1), "two");
    diag_error(loc_make("a.ft", 3, 1), "three");
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)3);
    end_capture();
})

// The per-file count is the budget the lexer and the parser share: it starts
// at each diag_begin_file and the global count keeps running.
TEST(the_file_count_starts_at_each_file, {
    begin_capture();
    TEST_ASSERT_EQ_UINT64(diag_file_count(), (uint64_t)0);
    diag_begin_file();
    diag_error(loc_make("a.ft", 1, 1), "one");
    diag_error(loc_make("a.ft", 2, 1), "two");
    diag_note(loc_make("a.ft", 2, 1), "a note");
    TEST_ASSERT_EQ_UINT64(diag_file_count(), (uint64_t)2);
    diag_begin_file();
    TEST_ASSERT_EQ_UINT64(diag_file_count(), (uint64_t)0);
    diag_error(loc_make("b.ft", 1, 1), "three");
    TEST_ASSERT_EQ_UINT64(diag_file_count(), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)3);
    end_capture();
})

// A muted probe reads another file, so what it reports leaves the budget of the file that asked for
// it where it was.
TEST(a_muted_probe_spends_no_budget, {
    begin_capture();
    diag_begin_file();
    diag_error(loc_make("a.ft", 1, 1), "one");
    diag_mute();
    diag_begin_file();
    diag_error(loc_make("b.ft", 1, 1), "probed");
    TEST_UNUSED(diag_unmute());
    TEST_ASSERT_EQ_UINT64(diag_file_count(), (uint64_t)1);
    end_capture();
})

// diag_reset clears the file count with the global one.
TEST(reset_clears_the_file_count, {
    begin_capture();
    diag_begin_file();
    diag_error(loc_make("a.ft", 1, 1), "one");
    diag_reset();
    TEST_ASSERT_EQ_UINT64(diag_file_count(), (uint64_t)0);
    end_capture();
})

TEST(reset_clears_the_count, {
    begin_capture();
    diag_error(loc_make("a.ft", 1, 1), "one");
    diag_error(loc_make("a.ft", 1, 1), "two");
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)2);
    diag_reset();
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)0);
    diag_error(loc_make("a.ft", 1, 1), "three");
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)1);
    end_capture();
})

TEST(reset_does_not_touch_captured_text, {
    begin_capture();
    diag_error(loc_make("a.ft", 1, 1), "kept");
    diag_reset();
    TEST_ASSERT_EQ_STR(captured(), "a.ft:1:1: error: kept\n");
    end_capture();
})

TEST(positionless_errors_use_line_one_column_one, {
    begin_capture();
    diag_error(loc_make("main.ft", 1, 1), "missing 'main'");
    TEST_ASSERT_EQ_STR(captured(), "main.ft:1:1: error: missing 'main'\n");
    end_capture();
})

// The text form prints the start only, so a range prints exactly what a bare
// position printed before ranges existed.
TEST(a_range_prints_only_its_start, {
    begin_capture();
    diag_error(loc_range("main.ft", 7, 5, 7, 6), "cannot assign to immutable 'x'");
    diag_note(loc_range("main.ft", 3, 9, 5, 2), "'x' declared here");
    diag_error(loc_range("util.ft", 12, 23, 12, 24), "expected ';'");
    TEST_ASSERT_EQ_STR(captured(),
                       "main.ft:7:5: error: cannot assign to immutable 'x'\n"
                       "main.ft:3:9: note: 'x' declared here\n"
                       "util.ft:12:23: error: expected ';'\n");
    end_capture();
})

TEST(a_range_and_a_bare_position_print_the_same_line, {
    begin_capture();
    diag_error(loc_make("main.ft", 7, 5), "same");
    diag_error(loc_range("main.ft", 7, 5, 9, 40), "same");
    TEST_ASSERT_EQ_STR(captured(),
                       "main.ft:7:5: error: same\n"
                       "main.ft:7:5: error: same\n");
    end_capture();
})

TEST(large_line_and_column_numbers_print_in_full, {
    begin_capture();
    diag_error(loc_make("big.ft", UINT32_MAX, 100000), "far away");
    TEST_ASSERT_EQ_STR(captured(), "big.ft:4294967295:100000: error: far away\n");
    end_capture();
})

TEST(file_paths_with_directories_print_as_given, {
    begin_capture();
    diag_error(loc_make("lib/util.ft", 2, 3), "x");
    diag_error(loc_make("../up/a.ft", 4, 5), "y");
    diag_error(loc_make("/abs/path/b.ft", 6, 7), "z");
    TEST_ASSERT_EQ_STR(captured(),
                       "lib/util.ft:2:3: error: x\n"
                       "../up/a.ft:4:5: error: y\n"
                       "/abs/path/b.ft:6:7: error: z\n");
    end_capture();
})

TEST(an_empty_message_still_prints_the_prefix, {
    begin_capture();
    diag_error(loc_make("a.ft", 1, 2), "");
    TEST_ASSERT_EQ_STR(captured(), "a.ft:1:2: error: \n");
    end_capture();
})

TEST(a_long_message_prints_in_full, {
    begin_capture();
    sb_t m;
    sb_init(&m);
    msg_begin(&m);
    for (int i = 0; i < 100; i++) {
        msg_str(&m, "0123456789");
    }
    diag_error(loc_make("a.ft", 1, 1), msg_end(&m));
    TEST_ASSERT_EQ_UINT64(sink.len, (uint64_t)(strlen("a.ft:1:1: error: \n") + 1000));
    TEST_ASSERT_TRUE(str_starts_with(sb_view(&sink), str_from_cstr("a.ft:1:1: error: 0123")));
    sb_free(&m);
    end_capture();
})

TEST(many_diagnostics_accumulate_in_the_sink, {
    begin_capture();
    for (int i = 0; i < 500; i++) {
        diag_error(loc_make("a.ft", (uint32_t)i + 1, 1), "e");
    }
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)500);
    TEST_ASSERT_TRUE(str_starts_with(sb_view(&sink), str_from_cstr("a.ft:1:1: error: e\n")));
    // "a.ft:<n>:1: error: e\n": 18 bytes plus the digits of n.
    const uint64_t fixed = strlen("a.ft:") + strlen(":1: error: e\n");
    uint64_t expected = 0;
    for (int i = 1; i <= 500; i++) {
        expected += fixed + (i < 10 ? 1U : i < 100 ? 2U : 3U);
    }
    TEST_ASSERT_EQ_UINT64(sink.len, expected);
    end_capture();
})

// ---- capture -------------------------------------------------------------------------

static void write_to_stderr_uncaptured(void) {
    diag_capture(NULL);
    diag_error(loc_make("main.ft", USE_LINE, USE_COL), "to stderr");
    diag_note(loc_make("main.ft", DECL_LINE, DECL_COL), "also");
}

TEST(without_capture_the_lines_go_to_stderr, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(write_to_stderr_uncaptured, err, sizeof err), 0);
    TEST_ASSERT_EQ_STR(err, "main.ft:7:5: error: to stderr\nmain.ft:3:9: note: also\n");
})

static void write_captured_then_uncaptured(void) {
    sb_t local;
    sb_init(&local);
    diag_capture(&local);
    diag_error(loc_make("a.ft", 1, 1), "captured");
    diag_capture(NULL);
    diag_error(loc_make("b.ft", 2, 2), "released");
    sb_free(&local);
}

TEST(capture_null_restores_stderr, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(write_captured_then_uncaptured, err, sizeof err), 0);
    TEST_ASSERT_EQ_STR(err, "b.ft:2:2: error: released\n");
})

TEST(capture_appends_to_whatever_the_sink_holds, {
    sb_t local;
    sb_init(&local);
    sb_append(&local, "before\n");
    diag_reset();
    diag_capture(&local);
    diag_error(loc_make("a.ft", 1, 1), "after");
    diag_capture(NULL);
    TEST_ASSERT_EQ_STR(sb_cstr(&local), "before\na.ft:1:1: error: after\n");
    sb_free(&local);
    diag_reset();
})

TEST(switching_sinks_routes_later_lines_to_the_new_one, {
    sb_t first;
    sb_t second;
    sb_init(&first);
    sb_init(&second);
    diag_reset();
    diag_capture(&first);
    diag_error(loc_make("a.ft", 1, 1), "one");
    diag_capture(&second);
    diag_error(loc_make("a.ft", 2, 1), "two");
    diag_capture(NULL);
    TEST_ASSERT_EQ_STR(sb_cstr(&first), "a.ft:1:1: error: one\n");
    TEST_ASSERT_EQ_STR(sb_cstr(&second), "a.ft:2:1: error: two\n");
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)2);
    sb_free(&first);
    sb_free(&second);
    diag_reset();
})

// ---- message builder ---------------------------------------------------------------

TEST(msg_begin_empties_the_buffer, {
    sb_t m;
    sb_init(&m);
    sb_append(&m, "stale");
    msg_begin(&m);
    TEST_ASSERT_EQ_UINT64(m.len, (uint64_t)0);
    TEST_ASSERT_EQ_STR(msg_end(&m), "");
    sb_free(&m);
})

TEST(msg_begin_on_a_fresh_buffer_is_fine, {
    sb_t m;
    sb_init(&m);
    msg_begin(&m);
    TEST_ASSERT_EQ_UINT64(m.len, (uint64_t)0);
    TEST_ASSERT_EQ_STR(msg_end(&m), "");
    sb_free(&m);
})

TEST(msg_str_appends_literal_text, {
    sb_t m;
    sb_init(&m);
    msg_begin(&m);
    msg_str(&m, "expected ");
    msg_str(&m, "';'");
    TEST_ASSERT_EQ_STR(msg_end(&m), "expected ';'");
    sb_free(&m);
})

TEST(msg_view_appends_a_view, {
    sb_t m;
    sb_init(&m);
    msg_begin(&m);
    msg_str(&m, "in ");
    msg_view(&m, str_from_range("functionality", 8));
    TEST_ASSERT_EQ_STR(msg_end(&m), "in function");
    sb_free(&m);
})

TEST(msg_int_appends_signed_decimal, {
    sb_t m;
    sb_init(&m);
    msg_begin(&m);
    msg_str(&m, "index ");
    msg_int(&m, -5);
    msg_str(&m, " out of range for length ");
    msg_int(&m, 3);
    TEST_ASSERT_EQ_STR(msg_end(&m), "index -5 out of range for length 3");
    sb_free(&m);
})

TEST(msg_int_handles_the_extremes, {
    sb_t m;
    sb_init(&m);
    msg_begin(&m);
    msg_int(&m, INT64_MIN);
    msg_str(&m, " ");
    msg_int(&m, INT64_MAX);
    msg_str(&m, " ");
    msg_int(&m, 0);
    TEST_ASSERT_EQ_STR(msg_end(&m), "-9223372036854775808 9223372036854775807 0");
    sb_free(&m);
})

TEST(msg_uint_appends_unsigned_decimal, {
    sb_t m;
    sb_init(&m);
    msg_begin(&m);
    msg_str(&m, "value ");
    msg_uint(&m, UINT64_MAX);
    msg_str(&m, " does not fit");
    TEST_ASSERT_EQ_STR(msg_end(&m), "value 18446744073709551615 does not fit");
    sb_free(&m);
})

TEST(msg_quote_wraps_a_name_in_single_quotes, {
    sb_t m;
    sb_init(&m);
    msg_begin(&m);
    msg_str(&m, "cannot assign to immutable ");
    msg_quote(&m, str_from_cstr("x"));
    TEST_ASSERT_EQ_STR(msg_end(&m), "cannot assign to immutable 'x'");
    sb_free(&m);
})

TEST(msg_quote_of_a_view_takes_only_the_view, {
    sb_t m;
    sb_init(&m);
    msg_begin(&m);
    msg_quote(&m, str_from_range("name_and_more", 4));
    msg_str(&m, " shadows a parameter");
    TEST_ASSERT_EQ_STR(msg_end(&m), "'name' shadows a parameter");
    sb_free(&m);
})

TEST(msg_quote_of_the_empty_view_is_two_quotes, {
    sb_t m;
    sb_init(&m);
    msg_begin(&m);
    msg_quote(&m, str_from_cstr(""));
    TEST_ASSERT_EQ_STR(msg_end(&m), "''");
    sb_free(&m);
})

TEST(msg_end_returns_the_buffer_and_keeps_its_length, {
    sb_t m;
    sb_init(&m);
    msg_begin(&m);
    msg_str(&m, "abc");
    const char* text = msg_end(&m);
    TEST_ASSERT_TRUE(text == m.data);
    TEST_ASSERT_EQ_UINT64(m.len, (uint64_t)3);
    TEST_ASSERT_EQ_STR(text, "abc");
    // Ending twice is harmless.
    TEST_ASSERT_EQ_STR(msg_end(&m), "abc");
    TEST_ASSERT_EQ_UINT64(m.len, (uint64_t)3);
    sb_free(&m);
})

TEST(msg_end_terminates_a_buffer_that_was_exactly_full, {
    sb_t m;
    sb_init(&m);
    msg_begin(&m);
    msg_str(&m, "0123456789abcdef");
    TEST_ASSERT_EQ_UINT64(m.cap, (uint64_t)16);
    TEST_ASSERT_EQ_STR(msg_end(&m), "0123456789abcdef");
    sb_free(&m);
})

TEST(a_message_buffer_is_reused_across_messages, {
    sb_t m;
    sb_init(&m);
    begin_capture();
    msg_begin(&m);
    msg_str(&m, "first ");
    msg_int(&m, 1);
    diag_error(loc_make("a.ft", 1, 1), msg_end(&m));
    msg_begin(&m);
    msg_str(&m, "second ");
    msg_int(&m, 2);
    diag_note(loc_make("a.ft", 2, 2), msg_end(&m));
    TEST_ASSERT_EQ_STR(captured(), "a.ft:1:1: error: first 1\na.ft:2:2: note: second 2\n");
    end_capture();
    sb_free(&m);
})

TEST(builder_composes_the_examples_of_toolchain_section_4, {
    sb_t m;
    sb_init(&m);
    begin_capture();
    msg_begin(&m);
    msg_str(&m, "cannot assign to immutable ");
    msg_quote(&m, str_from_cstr("x"));
    diag_error(loc_make("main.ft", 7, 5), msg_end(&m));
    msg_begin(&m);
    msg_quote(&m, str_from_cstr("x"));
    msg_str(&m, " declared here");
    diag_note(loc_make("main.ft", 3, 9), msg_end(&m));
    msg_begin(&m);
    msg_str(&m, "expected ");
    msg_quote(&m, str_from_cstr(";"));
    diag_error(loc_make("util.ft", 12, 23), msg_end(&m));
    TEST_ASSERT_EQ_STR(captured(),
                       "main.ft:7:5: error: cannot assign to immutable 'x'\n"
                       "main.ft:3:9: note: 'x' declared here\n"
                       "util.ft:12:23: error: expected ';'\n");
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)2);
    end_capture();
    sb_free(&m);
})

TEST(builder_composes_a_runtime_style_message_with_numbers, {
    sb_t m;
    sb_init(&m);
    msg_begin(&m);
    msg_str(&m, "index ");
    msg_int(&m, 5);
    msg_str(&m, " out of range for length ");
    msg_uint(&m, 3);
    TEST_ASSERT_EQ_STR(msg_end(&m), "index 5 out of range for length 3");
    sb_free(&m);
})

TEST(zero_initialized_message_buffer_is_valid, {
    sb_t m;
    TEST_UNUSED(memset(&m, 0, sizeof m));
    msg_begin(&m);
    msg_str(&m, "ok");
    TEST_ASSERT_EQ_STR(msg_end(&m), "ok");
    sb_free(&m);
})

TEST(a_muted_diagnostic_is_counted_and_not_written, {
    begin_capture();
    diag_mute();
    diag_error(loc_make("t.ft", 1, 1), "silent");
    diag_note(loc_make("t.ft", 1, 1), "also silent");
    // A muted error still counts, so a caller can ask diag_count whether the file it was probing
    // parsed.
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(diag_unmute(), (uint64_t)1);
    TEST_ASSERT_EQ_STR(captured(), "");
    end_capture();
})

TEST(unmute_restores_the_count_the_mute_saw, {
    begin_capture();
    diag_error(loc_make("t.ft", 1, 1), "real");
    diag_mute();
    diag_error(loc_make("t.ft", 2, 1), "probe");
    diag_error(loc_make("t.ft", 3, 1), "probe");
    TEST_ASSERT_EQ_UINT64(diag_unmute(), (uint64_t)2);
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)1);
    end_capture();
})

TEST(a_diagnostic_after_the_mute_is_written_again, {
    begin_capture();
    diag_mute();
    diag_error(loc_make("t.ft", 1, 1), "silent");
    TEST_UNUSED(diag_unmute());
    diag_error(loc_make("t.ft", 2, 3), "loud");
    TEST_ASSERT_EQ_STR(captured(), "t.ft:2:3: error: loud\n");
    end_capture();
})

TEST(only_the_outermost_mute_restores_the_count, {
    begin_capture();
    diag_mute();
    diag_mute();
    diag_error(loc_make("t.ft", 1, 1), "silent");
    TEST_ASSERT_EQ_UINT64(diag_unmute(), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)1);
    diag_error(loc_make("t.ft", 2, 1), "still silent");
    TEST_ASSERT_EQ_UINT64(diag_unmute(), (uint64_t)2);
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)0);
    TEST_ASSERT_EQ_STR(captured(), "");
    end_capture();
})

int main(int argc, char** argv) {
    TEST_INIT("diag", argc, argv);

    TEST_RUN(loc_make_fills_every_field);
    TEST_RUN(loc_make_is_the_empty_range_at_its_position);
    TEST_RUN(loc_holds_the_largest_line_and_column);
    TEST_RUN(loc_range_fills_every_field);
    TEST_RUN(a_token_range_ends_one_past_its_last_byte);
    TEST_RUN(starts_compare_the_line_first);
    TEST_RUN(starts_compare_the_column_within_a_line);
    TEST_RUN(a_start_is_at_or_before_itself);
    TEST_RUN(ends_compare_independently_of_the_starts);
    TEST_RUN(a_range_is_ordered_when_its_end_is_not_before_its_start);
    TEST_RUN(loc_extend_moves_the_end_and_keeps_the_start);
    TEST_RUN(loc_extend_spans_lines);
    TEST_RUN(loc_extend_keeps_the_later_end_and_never_widens_leftwards);
    TEST_RUN(loc_extend_keeps_the_later_end);
    TEST_RUN(loc_extend_is_idempotent);
    TEST_RUN(loc_extend_with_an_empty_range_at_the_same_end_changes_nothing);
    TEST_RUN(loc_extend_keeps_the_file_of_the_first);
    TEST_RUN(extending_an_empty_range_with_a_token_gives_the_token);

    TEST_RUN(error_prints_the_toolchain_format);
    TEST_RUN(note_prints_the_toolchain_format);
    TEST_RUN(error_then_note_are_consecutive_lines);
    TEST_RUN(errors_are_counted_and_notes_are_not);
    TEST_RUN(the_file_count_starts_at_each_file);
    TEST_RUN(a_muted_probe_spends_no_budget);
    TEST_RUN(reset_clears_the_file_count);
    TEST_RUN(reset_clears_the_count);
    TEST_RUN(reset_does_not_touch_captured_text);
    TEST_RUN(positionless_errors_use_line_one_column_one);
    TEST_RUN(a_range_prints_only_its_start);
    TEST_RUN(a_range_and_a_bare_position_print_the_same_line);
    TEST_RUN(large_line_and_column_numbers_print_in_full);
    TEST_RUN(file_paths_with_directories_print_as_given);
    TEST_RUN(an_empty_message_still_prints_the_prefix);
    TEST_RUN(a_long_message_prints_in_full);
    TEST_RUN(many_diagnostics_accumulate_in_the_sink);

    TEST_RUN(without_capture_the_lines_go_to_stderr);
    TEST_RUN(capture_null_restores_stderr);
    TEST_RUN(capture_appends_to_whatever_the_sink_holds);
    TEST_RUN(switching_sinks_routes_later_lines_to_the_new_one);

    TEST_RUN(msg_begin_empties_the_buffer);
    TEST_RUN(msg_begin_on_a_fresh_buffer_is_fine);
    TEST_RUN(msg_str_appends_literal_text);
    TEST_RUN(msg_view_appends_a_view);
    TEST_RUN(msg_int_appends_signed_decimal);
    TEST_RUN(msg_int_handles_the_extremes);
    TEST_RUN(msg_uint_appends_unsigned_decimal);
    TEST_RUN(msg_quote_wraps_a_name_in_single_quotes);
    TEST_RUN(msg_quote_of_a_view_takes_only_the_view);
    TEST_RUN(msg_quote_of_the_empty_view_is_two_quotes);
    TEST_RUN(msg_end_returns_the_buffer_and_keeps_its_length);
    TEST_RUN(msg_end_terminates_a_buffer_that_was_exactly_full);
    TEST_RUN(a_message_buffer_is_reused_across_messages);
    TEST_RUN(builder_composes_the_examples_of_toolchain_section_4);
    TEST_RUN(builder_composes_a_runtime_style_message_with_numbers);
    TEST_RUN(zero_initialized_message_buffer_is_valid);
    TEST_RUN(a_muted_diagnostic_is_counted_and_not_written);
    TEST_RUN(unmute_restores_the_count_the_mute_saw);
    TEST_RUN(a_diagnostic_after_the_mute_is_written_again);
    TEST_RUN(only_the_outermost_mute_restores_the_count);

    TEST_EXIT();
}

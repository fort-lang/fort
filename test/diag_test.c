// Unit tests of diag.h: the diagnostic lines of toolchain.md 4 (D14.2), the
// error count, capture, and the message builder.
#include "diag.h"

#include <stdint.h>
#include <stdlib.h>

#include "fork.h"

#include "test.h"

enum { ERR_MAX = 1024 };

// The positions of the toolchain.md section 4 example, for the helpers that
// run outside a TEST body.
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

TEST(loc_holds_the_largest_line_and_column, {
    const loc_t loc = loc_make("f", UINT32_MAX, UINT32_MAX);
    TEST_ASSERT_EQ_UINT64((uint64_t)loc.line, (uint64_t)UINT32_MAX);
    TEST_ASSERT_EQ_UINT64((uint64_t)loc.col, (uint64_t)UINT32_MAX);
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
    // A muted error still counts, so a caller can ask diag_count whether the
    // file it was probing parsed (module-system.md 3).
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
    TEST_RUN(loc_holds_the_largest_line_and_column);

    TEST_RUN(error_prints_the_toolchain_format);
    TEST_RUN(note_prints_the_toolchain_format);
    TEST_RUN(error_then_note_are_consecutive_lines);
    TEST_RUN(errors_are_counted_and_notes_are_not);
    TEST_RUN(reset_clears_the_count);
    TEST_RUN(reset_does_not_touch_captured_text);
    TEST_RUN(positionless_errors_use_line_one_column_one);
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

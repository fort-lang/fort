// Unit tests of the diagnostic records of diag.h: what diag_error and
// diag_note keep, what diag_set_text turns off, and the JSON document
// diag_write_json writes. The text form itself is tested in diag_test.c,
// which is a separate suite because one suite's main may not run many more
// tests than it already does.
#include <stdint.h>
#include <string.h>

#include "diag.h"
#include "fork.h"
#include "json.h"
#include "str.h"

#include "test.h"

enum { ERR_MAX = 1024 };

// The positions of the toolchain.md section 4 example: the use of 'x' and the
// declaration it shadows, each one byte wide.
enum { USE_LINE = 7, USE_COL = 5, DECL_LINE = 3, DECL_COL = 9 };

// The capture buffer of every test, so that no test writes to stderr, and the
// buffer a JSON document is built in.
static sb_t lines;
static sb_t doc;

static void begin(void) {
    sb_init(&lines);
    sb_init(&doc);
    diag_reset();
    diag_capture(&lines);
}

// Restores every mode a test may have changed.
static void end(void) {
    diag_capture(NULL);
    diag_set_text(true);
    diag_reset();
    sb_free(&lines);
    sb_free(&doc);
}

// The range of a one-byte token at `line`:`col`.
// D20.4
static loc_t one_byte(uint32_t line, uint32_t col) {
    return loc_range("main.ft", line, col, line, col + 1);
}

// The document of the records reported so far.
static const char* written_json(void) {
    json_t j;
    sb_clear(&doc);
    json_init(&j, &doc);
    diag_write_json(&j);
    return sb_cstr(&doc);
}

// ---- what a diagnostic records -----------------------------------------------------

TEST(a_fresh_sink_holds_no_record, {
    begin();
    TEST_ASSERT_EQ_UINT64(diag_record_count(), (uint64_t)0);
    end();
})

TEST(an_error_is_recorded_with_its_range, {
    begin();
    diag_error(one_byte(USE_LINE, USE_COL), "cannot assign to immutable 'x'");
    TEST_ASSERT_EQ_UINT64(diag_record_count(), (uint64_t)1);
    const diag_record_t rec = diag_record_at(0);
    TEST_ASSERT_EQ_STR(rec.loc.file, "main.ft");
    TEST_ASSERT_EQ_INT32((int32_t)rec.loc.line, USE_LINE);
    TEST_ASSERT_EQ_INT32((int32_t)rec.loc.col, USE_COL);
    TEST_ASSERT_EQ_INT32((int32_t)rec.loc.end_line, USE_LINE);
    TEST_ASSERT_EQ_INT32((int32_t)rec.loc.end_col, USE_COL + 1);
    TEST_ASSERT_EQ_STR(rec.msg.ptr, "cannot assign to immutable 'x'");
    end();
})

TEST(an_error_is_recorded_as_an_error_and_a_note_as_a_note, {
    begin();
    diag_error(one_byte(USE_LINE, USE_COL), "cannot assign to immutable 'x'");
    diag_note(one_byte(DECL_LINE, DECL_COL), "'x' declared here");
    TEST_ASSERT_EQ_UINT64(diag_record_count(), (uint64_t)2);
    TEST_ASSERT_TRUE(diag_record_at(0).severity == DIAG_ERROR);
    TEST_ASSERT_TRUE(diag_record_at(1).severity == DIAG_NOTE);
    end();
})

// The record owns its message, so a builder that is reused or released
// afterwards leaves it intact, and the record is a copy, so a diagnostic
// reported after it, which moves the sink's array, leaves it intact too.
TEST(the_message_is_copied_into_the_sink, {
    enum { GROWTH = 64 };
    sb_t m;
    sb_init(&m);
    begin();
    msg_begin(&m);
    msg_str(&m, "expected ");
    msg_quote(&m, str_from_cstr(";"));
    diag_error(one_byte(1, 1), msg_end(&m));
    msg_begin(&m);
    msg_str(&m, "something else entirely");
    TEST_UNUSED(msg_end(&m));
    sb_free(&m);
    const diag_record_t rec = diag_record_at(0);
    for (uint32_t i = 0; i < (uint32_t)GROWTH; i++) {
        diag_error(one_byte(i + 2, 1), "and another");
    }
    TEST_ASSERT_EQ_STR(rec.msg.ptr, "expected ';'");
    TEST_ASSERT_EQ_UINT64(rec.msg.len, (uint64_t)strlen("expected ';'"));
    TEST_ASSERT_EQ_INT32((int32_t)rec.loc.line, 1);
    end();
})

// The record owns its file name as it owns its message: the front end
// releases the names it read before the check mode writes the document, so a
// name the caller lent out would dangle.
// D20.2
TEST(the_file_name_is_copied_into_the_sink, {
    sb_t name;
    sb_init(&name);
    begin();
    sb_append(&name, "lib/util.ft");
    diag_error(loc_range(sb_cstr(&name), 2, 3, 2, 8), "expected ';'");
    sb_free(&name);
    const diag_record_t rec = diag_record_at(0);
    TEST_ASSERT_EQ_STR(rec.loc.file, "lib/util.ft");
    TEST_ASSERT_NONNULL(strstr(written_json(), "\"file\":\"lib/util.ft\""));
    end();
})

// A note's file name is copied too, and two diagnostics about one file keep
// their own copies.
TEST(every_record_keeps_its_own_file_name, {
    sb_t name;
    sb_init(&name);
    begin();
    sb_append(&name, "a.ft");
    diag_error(loc_range(sb_cstr(&name), 1, 1, 1, 2), "first");
    sb_clear(&name);
    sb_append(&name, "b.ft");
    diag_note(loc_range(sb_cstr(&name), 4, 1, 4, 2), "second");
    sb_free(&name);
    TEST_ASSERT_EQ_STR(diag_record_at(0).loc.file, "a.ft");
    TEST_ASSERT_EQ_STR(diag_record_at(1).loc.file, "b.ft");
    end();
})

// An error without a position in the file has no file name either; the copy
// must not turn that into a name.
// D14.2
TEST(a_record_without_a_file_keeps_none, {
    begin();
    diag_error(loc_make(NULL, 1, 1), "no file at all");
    TEST_ASSERT_NULL(diag_record_at(0).loc.file);
    end();
})

// The records are the diagnostics in the order they were reported, past the
// first growth of the array.
TEST(records_keep_the_order_they_were_reported_in, {
    enum { MANY = 100 };
    sb_t m;
    sb_init(&m);
    begin();
    for (uint32_t i = 0; i < (uint32_t)MANY; i++) {
        msg_begin(&m);
        msg_str(&m, "error ");
        msg_uint(&m, i);
        diag_error(one_byte(i + 1, 1), msg_end(&m));
    }
    sb_free(&m);
    TEST_ASSERT_EQ_UINT64(diag_record_count(), (uint64_t)MANY);
    for (uint32_t i = 0; i < (uint32_t)MANY; i++) {
        TEST_ASSERT_EQ_INT32((int32_t)diag_record_at(i).loc.line, (int32_t)(i + 1));
    }
    TEST_ASSERT_EQ_STR(diag_record_at((uint64_t)MANY - 1).msg.ptr, "error 99");
    end();
})

TEST(diag_reset_clears_the_records, {
    begin();
    diag_error(one_byte(1, 1), "first");
    diag_reset();
    TEST_ASSERT_EQ_UINT64(diag_record_count(), (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)0);
    diag_error(one_byte(2, 1), "second");
    TEST_ASSERT_EQ_UINT64(diag_record_count(), (uint64_t)1);
    TEST_ASSERT_EQ_STR(diag_record_at(0).msg.ptr, "second");
    end();
})

// A muted diagnostic is not reported at all, so it is not recorded either
// (module-system.md 3).
TEST(a_muted_diagnostic_is_not_recorded, {
    begin();
    diag_mute();
    diag_error(one_byte(1, 1), "probing");
    diag_note(one_byte(1, 1), "probing note");
    TEST_ASSERT_EQ_UINT64(diag_unmute(), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(diag_record_count(), (uint64_t)0);
    TEST_ASSERT_EQ_STR(sb_cstr(&lines), "");
    diag_error(one_byte(2, 1), "reported");
    TEST_ASSERT_EQ_UINT64(diag_record_count(), (uint64_t)1);
    end();
})

// The records are the sink's, not the capture buffer's: switching the sink
// mid-run keeps them all.
TEST(records_survive_a_change_of_capture, {
    sb_t other;
    sb_init(&other);
    begin();
    diag_error(one_byte(1, 1), "to the first buffer");
    diag_capture(&other);
    diag_error(one_byte(2, 1), "to the second");
    TEST_ASSERT_EQ_UINT64(diag_record_count(), (uint64_t)2);
    TEST_ASSERT_EQ_STR(sb_cstr(&lines), "main.ft:1:1: error: to the first buffer\n");
    TEST_ASSERT_EQ_STR(sb_cstr(&other), "main.ft:2:1: error: to the second\n");
    sb_free(&other);
    end();
})

// Mutes nest: a diagnostic between the inner unmute and the outer one is
// still muted, so it is still not recorded.
TEST(nested_mutes_record_nothing_until_the_outermost_closes, {
    begin();
    diag_mute();
    diag_mute();
    diag_error(one_byte(1, 1), "inner");
    TEST_ASSERT_EQ_UINT64(diag_unmute(), (uint64_t)0);
    diag_error(one_byte(2, 1), "between");
    TEST_ASSERT_EQ_UINT64(diag_unmute(), (uint64_t)2);
    TEST_ASSERT_EQ_UINT64(diag_record_count(), (uint64_t)0);
    diag_error(one_byte(3, 1), "after");
    TEST_ASSERT_EQ_UINT64(diag_record_count(), (uint64_t)1);
    TEST_ASSERT_EQ_STR(diag_record_at(0).msg.ptr, "after");
    end();
})

// A record keeps the file it was reported for, so the diagnostics of a
// closure of modules stay apart.
TEST(each_record_keeps_its_own_file, {
    begin();
    diag_error(loc_make("main.ft", 1, 1), "in the entry");
    diag_error(loc_make("lib/util.ft", 2, 1), "in the import");
    TEST_ASSERT_EQ_STR(diag_record_at(0).loc.file, "main.ft");
    TEST_ASSERT_EQ_STR(diag_record_at(1).loc.file, "lib/util.ft");
    end();
})

// The array of records grows by copying, so every record must survive every
// growth: enough diagnostics to reallocate several times, then each one read
// back in order with its position, its severity and its message.
TEST(every_record_survives_many_growths, {
    enum { PAIRS = 400 };
    sb_t m;
    sb_init(&m);
    begin();
    for (uint32_t i = 0; i < (uint32_t)PAIRS; i++) {
        msg_begin(&m);
        msg_str(&m, "error ");
        msg_uint(&m, i);
        diag_error(one_byte(i + 1, 1), msg_end(&m));
        msg_begin(&m);
        msg_str(&m, "note ");
        msg_uint(&m, i);
        diag_note(one_byte(i + 1, 5), msg_end(&m));
    }
    TEST_ASSERT_EQ_UINT64(diag_record_count(), (uint64_t)PAIRS * 2);
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)PAIRS);
    for (uint32_t i = 0; i < (uint32_t)PAIRS; i++) {
        const diag_record_t err = diag_record_at((uint64_t)i * 2);
        const diag_record_t note = diag_record_at((uint64_t)i * 2 + 1);
        msg_begin(&m);
        msg_str(&m, "error ");
        msg_uint(&m, i);
        TEST_ASSERT_EQ_STR(err.msg.ptr, msg_end(&m));
        msg_begin(&m);
        msg_str(&m, "note ");
        msg_uint(&m, i);
        TEST_ASSERT_EQ_STR(note.msg.ptr, msg_end(&m));
        TEST_ASSERT_TRUE(err.severity == DIAG_ERROR);
        TEST_ASSERT_TRUE(note.severity == DIAG_NOTE);
        TEST_ASSERT_EQ_INT32((int32_t)err.loc.line, (int32_t)(i + 1));
        TEST_ASSERT_EQ_INT32((int32_t)note.loc.col, 5);
    }
    sb_free(&m);
    end();
})

// A message longer than a pool block gets a block of its own, and the short
// messages around it are packed elsewhere: all of them must read back whole.
TEST(a_message_longer_than_a_pool_block_is_kept_whole, {
    enum { LONG = 9000 };
    enum { ROUNDS = 5 };
    sb_t m;
    sb_init(&m);
    begin();
    for (uint32_t round = 0; round < (uint32_t)ROUNDS; round++) {
        msg_begin(&m);
        msg_str(&m, "short");
        diag_error(one_byte(round + 1, 1), msg_end(&m));
        msg_begin(&m);
        for (uint32_t i = 0; i < (uint32_t)LONG; i++) {
            msg_str(&m, "x");
        }
        diag_error(one_byte(round + 1, 2), msg_end(&m));
    }
    sb_free(&m);
    for (uint32_t round = 0; round < (uint32_t)ROUNDS; round++) {
        const diag_record_t small = diag_record_at((uint64_t)round * 2);
        const diag_record_t big = diag_record_at((uint64_t)round * 2 + 1);
        TEST_ASSERT_EQ_STR(small.msg.ptr, "short");
        TEST_ASSERT_EQ_UINT64(big.msg.len, (uint64_t)LONG);
        TEST_ASSERT_EQ_CHAR(big.msg.ptr[0], 'x');
        TEST_ASSERT_EQ_CHAR(big.msg.ptr[LONG - 1], 'x');
        TEST_ASSERT_EQ_CHAR(big.msg.ptr[LONG], '\0');
    }
    end();
})

static void record_at_the_end(void) {
    diag_reset();
    TEST_UNUSED(diag_record_at(0));
}

TEST(a_record_past_the_end_is_an_internal_error, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(record_at_the_end, err, sizeof err), FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: diag_record_at: index out of range\n");
})

// ---- the text switch ---------------------------------------------------------------

// Text output is on until it is turned off: every existing caller writes its
// line as it always did.
// D14.2
TEST(the_text_line_is_written_by_default, {
    begin();
    diag_error(one_byte(USE_LINE, USE_COL), "to the sink");
    TEST_ASSERT_EQ_STR(sb_cstr(&lines), "main.ft:7:5: error: to the sink\n");
    end();
})

TEST(text_off_writes_no_line_and_still_records, {
    begin();
    diag_set_text(false);
    diag_error(one_byte(USE_LINE, USE_COL), "silent");
    diag_note(one_byte(DECL_LINE, DECL_COL), "silent note");
    TEST_ASSERT_EQ_STR(sb_cstr(&lines), "");
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(diag_record_count(), (uint64_t)2);
    TEST_ASSERT_EQ_STR(diag_record_at(0).msg.ptr, "silent");
    end();
})

TEST(text_on_again_writes_the_line_again, {
    begin();
    diag_set_text(false);
    diag_error(one_byte(1, 1), "silent");
    diag_set_text(true);
    diag_error(one_byte(2, 2), "loud");
    TEST_ASSERT_EQ_STR(sb_cstr(&lines), "main.ft:2:2: error: loud\n");
    end();
})

// diag_set_text is a mode of the run, not state of the sink, so a reset
// leaves it where it was.
TEST(diag_reset_leaves_the_text_switch_alone, {
    begin();
    diag_set_text(false);
    diag_reset();
    diag_error(one_byte(1, 1), "still silent");
    TEST_ASSERT_EQ_STR(sb_cstr(&lines), "");
    end();
})

// ---- the JSON document --------------------------------------------------------------

TEST(no_diagnostic_is_an_empty_array, {
    begin();
    TEST_ASSERT_EQ_STR(written_json(), "[]");
    end();
})

TEST(an_error_is_one_object_with_an_empty_notes_array, {
    begin();
    diag_error(one_byte(USE_LINE, USE_COL), "expected ';'");
    TEST_ASSERT_EQ_STR(written_json(),
                       "[{\"file\":\"main.ft\",\"line\":7,\"col\":5,\"end_line\":7,\"end_col\":6,"
                       "\"severity\":\"error\",\"message\":\"expected ';'\",\"notes\":[]}]");
    end();
})

// The golden document of the worked example: the two notes that follow the
// error are nested under it.
// D14.2
TEST(the_notes_that_follow_an_error_are_nested_under_it, {
    begin();
    diag_error(one_byte(USE_LINE, USE_COL), "cannot assign to immutable 'x'");
    diag_note(one_byte(DECL_LINE, DECL_COL), "'x' declared here");
    diag_note(one_byte(DECL_LINE, DECL_COL + 2), "declare it with 'mut'");
    TEST_ASSERT_EQ_STR(
        written_json(),
        "[{\"file\":\"main.ft\",\"line\":7,\"col\":5,\"end_line\":7,\"end_col\":6,"
        "\"severity\":\"error\",\"message\":\"cannot assign to immutable 'x'\",\"notes\":["
        "{\"file\":\"main.ft\",\"line\":3,\"col\":9,\"end_line\":3,\"end_col\":10,"
        "\"message\":\"'x' declared here\"},"
        "{\"file\":\"main.ft\",\"line\":3,\"col\":11,\"end_line\":3,\"end_col\":12,"
        "\"message\":\"declare it with 'mut'\"}]}]");
    end();
})

// A note belongs to the error before it, so the next error starts a
// diagnostic of its own.
TEST(each_error_takes_only_the_notes_that_follow_it, {
    begin();
    diag_error(one_byte(1, 1), "first");
    diag_note(one_byte(2, 1), "about the first");
    diag_error(one_byte(3, 1), "second");
    TEST_ASSERT_EQ_STR(written_json(),
                       "[{\"file\":\"main.ft\",\"line\":1,\"col\":1,\"end_line\":1,\"end_col\":2,"
                       "\"severity\":\"error\",\"message\":\"first\",\"notes\":["
                       "{\"file\":\"main.ft\",\"line\":2,\"col\":1,\"end_line\":2,\"end_col\":2,"
                       "\"message\":\"about the first\"}]},"
                       "{\"file\":\"main.ft\",\"line\":3,\"col\":1,\"end_line\":3,\"end_col\":2,"
                       "\"severity\":\"error\",\"message\":\"second\",\"notes\":[]}]");
    end();
})

// A note that follows no error has nothing to hang under, so it stands as a
// diagnostic of its own rather than being dropped.
TEST(a_note_without_an_error_stands_alone, {
    begin();
    diag_note(one_byte(1, 1), "lonely");
    diag_note(one_byte(2, 1), "also lonely");
    TEST_ASSERT_EQ_STR(written_json(),
                       "[{\"file\":\"main.ft\",\"line\":1,\"col\":1,\"end_line\":1,\"end_col\":2,"
                       "\"severity\":\"note\",\"message\":\"lonely\",\"notes\":[]},"
                       "{\"file\":\"main.ft\",\"line\":2,\"col\":1,\"end_line\":2,\"end_col\":2,"
                       "\"severity\":\"note\",\"message\":\"also lonely\",\"notes\":[]}]");
    end();
})

// The whole range is written, including one that spans lines, and a
// positionless error is the empty range at 1:1.
// D14.2, D20.4
TEST(the_document_holds_both_ends_of_the_range, {
    begin();
    diag_error(loc_range("util.ft", 12, 23, 14, 2), "infinite size");
    diag_error(loc_make("main.ft", 1, 1), "no 'main' function");
    TEST_ASSERT_EQ_STR(
        written_json(),
        "[{\"file\":\"util.ft\",\"line\":12,\"col\":23,\"end_line\":14,\"end_col\":2,"
        "\"severity\":\"error\",\"message\":\"infinite size\",\"notes\":[]},"
        "{\"file\":\"main.ft\",\"line\":1,\"col\":1,\"end_line\":1,\"end_col\":1,"
        "\"severity\":\"error\",\"message\":\"no 'main' function\",\"notes\":[]}]");
    end();
})

// A message and a file name go through the writer's escapes.
TEST(the_message_and_the_file_name_are_escaped, {
    begin();
    diag_error(loc_range("a\"b.ft", 1, 1, 1, 2), "expected '\"', found a tab\there");
    TEST_ASSERT_EQ_STR(written_json(),
                       "[{\"file\":\"a\\\"b.ft\",\"line\":1,\"col\":1,\"end_line\":1,"
                       "\"end_col\":2,\"severity\":\"error\","
                       "\"message\":\"expected '\\\"', found a tab\\there\",\"notes\":[]}]");
    end();
})

// Every error is one element of the array, whatever their number.
TEST(many_errors_are_many_elements, {
    enum { MANY = 30 };
    uint64_t found = 0;
    const char* at = NULL;
    begin();
    for (uint32_t i = 0; i < (uint32_t)MANY; i++) {
        diag_error(one_byte(i + 1, 1), "boom");
        diag_note(one_byte(i + 1, 3), "note");
    }
    at = written_json();
    while (at != NULL) {
        at = strstr(at, "\"severity\":\"error\"");
        if (at != NULL) {
            found++;
            at++;
        }
    }
    TEST_ASSERT_EQ_UINT64(found, (uint64_t)MANY);
    TEST_ASSERT_EQ_UINT64(diag_record_count(), (uint64_t)MANY * 2);
    end();
})

// Several errors, each with its own notes, in one document: the notes of one
// error never reach the next.
TEST(several_errors_each_keep_their_own_notes, {
    begin();
    diag_error(one_byte(1, 1), "first");
    diag_note(one_byte(1, 5), "note of the first");
    diag_note(one_byte(1, 9), "second note of the first");
    diag_error(one_byte(2, 1), "second");
    diag_error(one_byte(3, 1), "third");
    diag_note(one_byte(3, 5), "note of the third");
    TEST_ASSERT_EQ_STR(written_json(),
                       "[{\"file\":\"main.ft\",\"line\":1,\"col\":1,\"end_line\":1,\"end_col\":2,"
                       "\"severity\":\"error\",\"message\":\"first\",\"notes\":["
                       "{\"file\":\"main.ft\",\"line\":1,\"col\":5,\"end_line\":1,\"end_col\":6,"
                       "\"message\":\"note of the first\"},"
                       "{\"file\":\"main.ft\",\"line\":1,\"col\":9,\"end_line\":1,\"end_col\":10,"
                       "\"message\":\"second note of the first\"}]},"
                       "{\"file\":\"main.ft\",\"line\":2,\"col\":1,\"end_line\":2,\"end_col\":2,"
                       "\"severity\":\"error\",\"message\":\"second\",\"notes\":[]},"
                       "{\"file\":\"main.ft\",\"line\":3,\"col\":1,\"end_line\":3,\"end_col\":2,"
                       "\"severity\":\"error\",\"message\":\"third\",\"notes\":["
                       "{\"file\":\"main.ft\",\"line\":3,\"col\":5,\"end_line\":3,\"end_col\":6,"
                       "\"message\":\"note of the third\"}]}]");
    end();
})

// A note that follows no error still stands alone when errors come later,
// and the error after it takes only the notes that follow it.
TEST(an_orphan_note_does_not_capture_the_notes_of_the_error_after_it, {
    begin();
    diag_note(one_byte(1, 1), "orphan");
    diag_error(one_byte(2, 1), "boom");
    diag_note(one_byte(2, 5), "about the boom");
    TEST_ASSERT_EQ_STR(written_json(),
                       "[{\"file\":\"main.ft\",\"line\":1,\"col\":1,\"end_line\":1,\"end_col\":2,"
                       "\"severity\":\"note\",\"message\":\"orphan\",\"notes\":[]},"
                       "{\"file\":\"main.ft\",\"line\":2,\"col\":1,\"end_line\":2,\"end_col\":2,"
                       "\"severity\":\"error\",\"message\":\"boom\",\"notes\":["
                       "{\"file\":\"main.ft\",\"line\":2,\"col\":5,\"end_line\":2,\"end_col\":6,"
                       "\"message\":\"about the boom\"}]}]");
    end();
})

// The document of a sink that was reset is empty again, whatever it held.
TEST(the_document_is_empty_again_after_a_reset, {
    begin();
    diag_error(one_byte(1, 1), "boom");
    diag_note(one_byte(1, 5), "bang");
    diag_reset();
    TEST_ASSERT_EQ_STR(written_json(), "[]");
    end();
})

// A message can quote any bytes of a source file, which is UTF-8 only by
// convention, so the writer replaces what is not well-formed and the document
// stays valid UTF-8.
// D2.1, D3.7
TEST(a_message_of_arbitrary_bytes_gives_a_valid_document, {
    sb_t m;
    sb_init(&m);
    msg_begin(&m);
    msg_str(&m, "unexpected byte in ");
    msg_quote(&m,
              str_from_range("caf\xC3\xA9"
                             "\xFF",
                             6));
    begin();
    diag_error(one_byte(1, 1), msg_end(&m));
    sb_free(&m);
    TEST_ASSERT_NONNULL(
        strstr(written_json(), "\"message\":\"unexpected byte in 'caf\xC3\xA9\xEF\xBF\xBD'\""));
    end();
})

// The document is written through a writer the caller owns, so it can be one
// member of a larger document.
TEST(the_array_can_be_the_value_of_a_member, {
    json_t j;
    begin();
    diag_error(one_byte(1, 1), "boom");
    sb_clear(&doc);
    json_init(&j, &doc);
    json_object_begin(&j);
    json_key(&j, "diagnostics");
    diag_write_json(&j);
    json_object_end(&j);
    TEST_ASSERT_EQ_STR(sb_cstr(&doc),
                       "{\"diagnostics\":[{\"file\":\"main.ft\",\"line\":1,\"col\":1,"
                       "\"end_line\":1,\"end_col\":2,\"severity\":\"error\","
                       "\"message\":\"boom\",\"notes\":[]}]}");
    end();
})

// What the JSON says and what the text says are the same diagnostics: the
// structured form is written from the records, not from the lines.
TEST(the_document_is_written_with_the_text_off, {
    begin();
    diag_set_text(false);
    diag_error(one_byte(USE_LINE, USE_COL), "expected ';'");
    diag_note(one_byte(DECL_LINE, DECL_COL), "here");
    TEST_ASSERT_EQ_STR(sb_cstr(&lines), "");
    TEST_ASSERT_NONNULL(strstr(written_json(), "\"message\":\"expected ';'\""));
    TEST_ASSERT_NONNULL(strstr(written_json(), "\"message\":\"here\""));
    end();
})

int main(int argc, char** argv) {
    TEST_INIT("diag_records", argc, argv);
    TEST_RUN(a_fresh_sink_holds_no_record);
    TEST_RUN(an_error_is_recorded_with_its_range);
    TEST_RUN(an_error_is_recorded_as_an_error_and_a_note_as_a_note);
    TEST_RUN(the_message_is_copied_into_the_sink);
    TEST_RUN(the_file_name_is_copied_into_the_sink);
    TEST_RUN(every_record_keeps_its_own_file_name);
    TEST_RUN(a_record_without_a_file_keeps_none);
    TEST_RUN(records_keep_the_order_they_were_reported_in);
    TEST_RUN(diag_reset_clears_the_records);
    TEST_RUN(a_muted_diagnostic_is_not_recorded);
    TEST_RUN(every_record_survives_many_growths);
    TEST_RUN(a_message_longer_than_a_pool_block_is_kept_whole);
    TEST_RUN(records_survive_a_change_of_capture);
    TEST_RUN(nested_mutes_record_nothing_until_the_outermost_closes);
    TEST_RUN(each_record_keeps_its_own_file);
    TEST_RUN(a_record_past_the_end_is_an_internal_error);
    TEST_RUN(the_text_line_is_written_by_default);
    TEST_RUN(text_off_writes_no_line_and_still_records);
    TEST_RUN(text_on_again_writes_the_line_again);
    TEST_RUN(diag_reset_leaves_the_text_switch_alone);
    TEST_RUN(no_diagnostic_is_an_empty_array);
    TEST_RUN(an_error_is_one_object_with_an_empty_notes_array);
    TEST_RUN(the_notes_that_follow_an_error_are_nested_under_it);
    TEST_RUN(each_error_takes_only_the_notes_that_follow_it);
    TEST_RUN(a_note_without_an_error_stands_alone);
    TEST_RUN(the_document_holds_both_ends_of_the_range);
    TEST_RUN(the_message_and_the_file_name_are_escaped);
    TEST_RUN(several_errors_each_keep_their_own_notes);
    TEST_RUN(an_orphan_note_does_not_capture_the_notes_of_the_error_after_it);
    TEST_RUN(the_document_is_empty_again_after_a_reset);
    TEST_RUN(a_message_of_arbitrary_bytes_gives_a_valid_document);
    TEST_RUN(many_errors_are_many_elements);
    TEST_RUN(the_array_can_be_the_value_of_a_member);
    TEST_RUN(the_document_is_written_with_the_text_off);
    TEST_EXIT();
}

// Unit tests of json.h: the shape of the written document, the escapes of a
// string, the numbers, and the internal errors of a misused writer.
#include "json.h"

#include <stdint.h>
#include <string.h>

#include "fork.h"
#include "str.h"

#include "test.h"

enum { ERR_MAX = 1024 };

// The last control byte, the DEL byte, the first byte of the non-ASCII range
// and the last byte of all.
// (DEL is also the last byte of the ASCII range.)
enum {
    CONTROL_MAX = 0x1F,
    DELETE_BYTE = 0x7F,
    ASCII_MAX = 0x7F,
    HIGH_MIN = 0x80,
    BYTE_MAX = 0xFF,
};

// The JSON form of every byte from 0x00 to CONTROL_MAX, written out here so
// that the table is a golden and not a second copy of the writer's rule: the
// control bytes fort spells out (D2.8) are spelled out, every other one is
// \u00XX with uppercase hex digits.
static const char* const CONTROL_FORMS[] = {
    "\\u0000", "\\u0001", "\\u0002", "\\u0003", "\\u0004", "\\u0005", "\\u0006", "\\u0007",
    "\\u0008", "\\t",     "\\n",     "\\u000B", "\\u000C", "\\r",     "\\u000E", "\\u000F",
    "\\u0010", "\\u0011", "\\u0012", "\\u0013", "\\u0014", "\\u0015", "\\u0016", "\\u0017",
    "\\u0018", "\\u0019", "\\u001A", "\\u001B", "\\u001C", "\\u001D", "\\u001E", "\\u001F",
};

// The UTF-8 bytes of U+FFFD, which a byte that is part of no well-formed
// sequence is written as.
#define REPLACEMENT_FORM "\xEF\xBF\xBD"

// The buffer and the writer of every test that does not fork, and the buffer
// the expected document of a byte-by-byte test is built in.
static sb_t out;
static json_t w;
static sb_t expected;

static void begin(void) {
    sb_init(&out);
    json_init(&w, &out);
}

static void end(void) {
    sb_free(&out);
}

static const char* written(void) {
    return sb_cstr(&out);
}

// The one-byte string of `byte`, written into the fresh shared buffer.
static void write_one_byte(int byte) {
    char text[1];
    text[0] = (char)byte;
    begin();
    json_str(&w, str_from_range(text, 1));
}

// `form` between two quotes: the document a one-byte string produces.
static const char* quoted(const char* form) {
    sb_clear(&expected);
    sb_push(&expected, '"');
    sb_append(&expected, form);
    sb_push(&expected, '"');
    return sb_cstr(&expected);
}

// The same for a byte that needs no escape.
static const char* quoted_byte(int byte) {
    sb_clear(&expected);
    sb_push(&expected, '"');
    sb_push(&expected, (char)byte);
    sb_push(&expected, '"');
    return sb_cstr(&expected);
}

// ---- the UTF-8 corpus ----------------------------------------------------------------

// One case of the corpus: the bytes that may or may not form one sequence,
// how many they are, the text the document must hold for them, whether they
// are well-formed UTF-8 (RFC 3629), and a name, so that a failing case says
// which one it is.
typedef struct {
    const char* bytes;
    uint64_t len;
    const char* want;
    bool well_formed;
    const char* what;
} utf8_case_t;

// Both edges of every range of every sequence length, and the five ways a
// sequence is rejected: a stray continuation byte, a lead byte no sequence
// starts with, an overlong form, a UTF-16 surrogate and a value past
// U+10FFFF, plus sequences cut short and continuation bytes one step out of
// range. A byte that is part of no well-formed sequence costs one U+FFFD and
// no more, so the ASCII byte after a broken lead is still itself.
static const utf8_case_t UTF8_CASES[] = {
    {"\xC2\x80", 2, "\xC2\x80", true, "U+0080, the lowest two-byte sequence"},
    {"\xC2\xBF", 2, "\xC2\xBF", true, "U+00BF, the last of the lowest lead byte"},
    {"\xDF\x80", 2, "\xDF\x80", true, "U+07C0, the first of the highest lead byte"},
    {"\xDF\xBF", 2, "\xDF\xBF", true, "U+07FF, the highest two-byte sequence"},
    {"\xE0\xA0\x80", 3, "\xE0\xA0\x80", true, "U+0800, the lowest three-byte sequence"},
    {"\xE0\xBF\xBF", 3, "\xE0\xBF\xBF", true, "U+0FFF, the last of lead E0"},
    {"\xE1\x80\x80", 3, "\xE1\x80\x80", true, "U+1000, the first of lead E1"},
    {"\xEC\xBF\xBF", 3, "\xEC\xBF\xBF", true, "U+CFFF, the last of lead EC"},
    {"\xED\x80\x80", 3, "\xED\x80\x80", true, "U+D000, the first of lead ED"},
    {"\xED\x9F\xBF", 3, "\xED\x9F\xBF", true, "U+D7FF, the last code point before the surrogates"},
    {"\xEE\x80\x80", 3, "\xEE\x80\x80", true, "U+E000, the first code point after the surrogates"},
    {"\xEF\xBF\xBF", 3, "\xEF\xBF\xBF", true, "U+FFFF, the highest three-byte sequence"},
    {"\xF0\x90\x80\x80", 4, "\xF0\x90\x80\x80", true, "U+10000, the lowest four-byte sequence"},
    {"\xF0\xBF\xBF\xBF", 4, "\xF0\xBF\xBF\xBF", true, "U+3FFFF, the last of lead F0"},
    {"\xF1\x80\x80\x80", 4, "\xF1\x80\x80\x80", true, "U+40000, the first of lead F1"},
    {"\xF3\xBF\xBF\xBF", 4, "\xF3\xBF\xBF\xBF", true, "U+FFFFF, the last of lead F3"},
    {"\xF4\x80\x80\x80", 4, "\xF4\x80\x80\x80", true, "U+100000, the first of lead F4"},
    {"\xF4\x8F\xBF\xBF", 4, "\xF4\x8F\xBF\xBF", true, "U+10FFFF, the highest code point"},
    {"\x80", 1, REPLACEMENT_FORM, false, "a stray continuation byte, the lowest"},
    {"\xBF", 1, REPLACEMENT_FORM, false, "a stray continuation byte, the highest"},
    {"\xC0\x80",
     2,
     REPLACEMENT_FORM REPLACEMENT_FORM,
     false,
     "C0, a lead byte only an overlong form uses"},
    {"\xC1\xBF",
     2,
     REPLACEMENT_FORM REPLACEMENT_FORM,
     false,
     "C1, the other lead byte of an overlong form"},
    {"\xE0\x80\x80",
     3,
     REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM,
     false,
     "an overlong U+0000 in three bytes"},
    {"\xE0\x9F\xBF",
     3,
     REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM,
     false,
     "an overlong U+07FF, one below the lowest of its length"},
    {"\xF0\x80\x80\x80",
     4,
     REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM,
     false,
     "an overlong U+0000 in four bytes"},
    {"\xF0\x8F\xBF\xBF",
     4,
     REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM,
     false,
     "an overlong U+FFFF, one below the lowest of its length"},
    {"\xED\xA0\x80",
     3,
     REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM,
     false,
     "U+D800, the first UTF-16 surrogate"},
    {"\xED\xBF\xBF",
     3,
     REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM,
     false,
     "U+DFFF, the last UTF-16 surrogate"},
    {"\xF4\x90\x80\x80",
     4,
     REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM,
     false,
     "U+110000, one past the highest code point"},
    {"\xF5\x80\x80\x80",
     4,
     REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM,
     false,
     "F5, the first lead byte past the highest"},
    {"\xF7\xBF\xBF\xBF",
     4,
     REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM,
     false,
     "F7, the last lead byte of the old five-byte range"},
    {"\xF8\x88\x80\x80",
     4,
     REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM,
     false,
     "F8, a lead byte of no sequence at all"},
    {"\xFE", 1, REPLACEMENT_FORM, false, "FE, a byte UTF-8 never uses"},
    {"\xFF", 1, REPLACEMENT_FORM, false, "FF, the other byte UTF-8 never uses"},
    {"\xC2", 1, REPLACEMENT_FORM, false, "a two-byte sequence cut short"},
    {"\xE1\x80", 2, REPLACEMENT_FORM REPLACEMENT_FORM, false, "a three-byte sequence cut short"},
    {"\xF1\x80\x80",
     3,
     REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM,
     false,
     "a four-byte sequence cut short"},
    {"\xC2\x7F",
     2,
     REPLACEMENT_FORM "\x7F",
     false,
     "a second byte one below the continuation range"},
    {"\xC2\xC0",
     2,
     REPLACEMENT_FORM REPLACEMENT_FORM,
     false,
     "a second byte one above the continuation range"},
    {"\xE1\x80\x7F",
     3,
     REPLACEMENT_FORM REPLACEMENT_FORM "\x7F",
     false,
     "a third byte one below the continuation range"},
    {"\xF1\x80\x80\xC0",
     4,
     REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM,
     false,
     "a fourth byte one above the continuation range"},
};

enum { UTF8_CASE_COUNT = (int)(sizeof UTF8_CASES / sizeof UTF8_CASES[0]) };

// The two buffers a labelled comparison needs, since both sides are built
// before either is read.
static sb_t label_actual;
static sb_t label_expected;

// `what` and `text`, so that an assertion over a table names the case that
// failed instead of printing two runs of bytes.
static const char* labelled(sb_t* b, const char* what, str_t text) {
    sb_clear(b);
    sb_append(b, what);
    sb_append(b, ": ");
    sb_append_str(b, text);
    return sb_cstr(b);
}

// The document `c` must produce: its own text between two quotes.
static str_t expected_document(const utf8_case_t* c) {
    sb_clear(&expected);
    sb_push(&expected, '"');
    sb_append(&expected, c->want);
    sb_push(&expected, '"');
    return sb_view(&expected);
}

static void free_labels(void) {
    sb_free(&label_actual);
    sb_free(&label_expected);
    sb_free(&expected);
}

// ---- values ------------------------------------------------------------------------

TEST(an_empty_object_is_two_braces, {
    begin();
    json_object_begin(&w);
    json_object_end(&w);
    TEST_ASSERT_EQ_STR(written(), "{}");
    end();
})

TEST(an_empty_array_is_two_brackets, {
    begin();
    json_array_begin(&w);
    json_array_end(&w);
    TEST_ASSERT_EQ_STR(written(), "[]");
    end();
})

TEST(a_document_may_be_a_bare_value, {
    begin();
    json_cstr(&w, "alone");
    TEST_ASSERT_EQ_STR(written(), "\"alone\"");
    end();
})

TEST(booleans_and_null_are_written_as_literals, {
    begin();
    json_array_begin(&w);
    json_bool(&w, true);
    json_bool(&w, false);
    json_null(&w);
    json_array_end(&w);
    TEST_ASSERT_EQ_STR(written(), "[true,false,null]");
    end();
})

TEST(numbers_are_decimal_with_a_sign_for_a_negative_one, {
    begin();
    json_array_begin(&w);
    json_uint(&w, 0);
    json_int(&w, -1);
    json_int(&w, 42);
    json_array_end(&w);
    TEST_ASSERT_EQ_STR(written(), "[0,-1,42]");
    end();
})

TEST(the_widest_numbers_are_written_in_full, {
    begin();
    json_array_begin(&w);
    json_uint(&w, UINT64_MAX);
    json_int(&w, INT64_MIN);
    json_array_end(&w);
    TEST_ASSERT_EQ_STR(written(), "[18446744073709551615,-9223372036854775808]");
    end();
})

TEST(a_string_view_may_hold_any_bytes_including_a_nul, {
    begin();
    json_str(&w, str_from_range("a\0b", 3));
    TEST_ASSERT_EQ_STR(written(), "\"a\\u0000b\"");
    end();
})

// ---- separators ---------------------------------------------------------------------

TEST(object_members_are_separated_by_commas, {
    begin();
    json_object_begin(&w);
    json_key(&w, "a");
    json_uint(&w, 1);
    json_key(&w, "b");
    json_uint(&w, 2);
    json_object_end(&w);
    TEST_ASSERT_EQ_STR(written(), "{\"a\":1,\"b\":2}");
    end();
})

// A member's value follows its key with no comma between them, whatever the
// value is.
TEST(no_comma_separates_a_key_from_its_value, {
    begin();
    json_object_begin(&w);
    json_key(&w, "list");
    json_array_begin(&w);
    json_array_end(&w);
    json_object_end(&w);
    TEST_ASSERT_EQ_STR(written(), "{\"list\":[]}");
    end();
})

TEST(array_elements_are_separated_by_commas, {
    begin();
    json_array_begin(&w);
    json_cstr(&w, "x");
    json_cstr(&w, "y");
    json_array_end(&w);
    TEST_ASSERT_EQ_STR(written(), "[\"x\",\"y\"]");
    end();
})

// A comma belongs to the container that is open, so a closed container is one
// element of its parent.
TEST(a_closed_container_is_an_element_of_its_parent, {
    begin();
    json_array_begin(&w);
    json_object_begin(&w);
    json_key(&w, "n");
    json_uint(&w, 1);
    json_object_end(&w);
    json_object_begin(&w);
    json_key(&w, "n");
    json_uint(&w, 2);
    json_object_end(&w);
    json_array_end(&w);
    TEST_ASSERT_EQ_STR(written(), "[{\"n\":1},{\"n\":2}]");
    end();
})

TEST(a_reopened_container_starts_empty_again, {
    begin();
    json_array_begin(&w);
    json_array_begin(&w);
    json_uint(&w, 1);
    json_uint(&w, 2);
    json_array_end(&w);
    json_array_begin(&w);
    json_uint(&w, 3);
    json_array_end(&w);
    json_array_end(&w);
    TEST_ASSERT_EQ_STR(written(), "[[1,2],[3]]");
    end();
})

// The document is one line: no indentation and no space between tokens.
TEST(the_document_holds_no_whitespace, {
    begin();
    json_object_begin(&w);
    json_key(&w, "a");
    json_array_begin(&w);
    json_uint(&w, 1);
    json_cstr(&w, "t");
    json_array_end(&w);
    json_object_end(&w);
    TEST_ASSERT_NULL(strpbrk(written(), " \t\n\r"));
    end();
})

// json_init does not empty the buffer, so a document may follow other bytes.
TEST(a_document_is_appended_to_the_buffer, {
    sb_init(&out);
    sb_append(&out, "before:");
    json_init(&w, &out);
    json_null(&w);
    TEST_ASSERT_EQ_STR(written(), "before:null");
    end();
})

// ---- escapes -------------------------------------------------------------------------

TEST(the_quote_and_the_backslash_are_escaped, {
    begin();
    json_cstr(&w, "a\"b\\c");
    TEST_ASSERT_EQ_STR(written(), "\"a\\\"b\\\\c\"");
    end();
})

// The control bytes fort spells out are spelled out here too (D2.8).
TEST(newline_tab_and_return_are_spelled_out, {
    begin();
    json_cstr(&w, "\n\t\r");
    TEST_ASSERT_EQ_STR(written(), "\"\\n\\t\\r\"");
    end();
})

TEST(a_key_is_escaped_like_a_string, {
    begin();
    json_object_begin(&w);
    json_key(&w, "a\"b\nc");
    json_uint(&w, 1);
    json_object_end(&w);
    TEST_ASSERT_EQ_STR(written(), "{\"a\\\"b\\nc\":1}");
    end();
})

// Every byte of the control range, the NUL of D2.8's \0 included, has the
// form the golden table gives.
TEST(every_control_byte_is_escaped, {
    for (int byte = 0; byte <= CONTROL_MAX; byte++) {
        write_one_byte(byte);
        TEST_ASSERT_EQ_STR(written(), quoted(CONTROL_FORMS[byte]));
        end();
    }
    sb_free(&expected);
})

// 0x7F is not a control byte for JSON and needs no escape.
TEST(delete_goes_through_verbatim, {
    write_one_byte(DELETE_BYTE);
    TEST_ASSERT_EQ_STR(written(), quoted_byte(DELETE_BYTE));
    TEST_ASSERT_EQ_UINT64(sb_view(&out).len, (uint64_t)3);
    end();
    sb_free(&expected);
})

// A well-formed UTF-8 sequence is part of the text and goes through as it
// stands, so a document that was UTF-8 stays UTF-8.
TEST(a_utf8_sequence_goes_through_verbatim, {
    begin();
    json_cstr(&w, "caf\xC3\xA9"); // U+00E9
    TEST_ASSERT_EQ_STR(written(), "\"caf\xC3\xA9\"");
    TEST_ASSERT_EQ_UINT64(sb_view(&out).len, (uint64_t)7);
    end();
})

// One sequence of each length, at the edges of their ranges: U+0080, U+07FF,
// U+0800, U+D7FF, U+E000, U+FFFD, U+10000 and U+10FFFF.
TEST(a_sequence_of_every_length_goes_through_verbatim, {
    begin();
    json_cstr(&w,
              "\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xEF\xBF\xBD"
              "\xF0\x90\x80\x80\xF4\x8F\xBF\xBF");
    TEST_ASSERT_EQ_STR(written(),
                       "\"\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xEF\xBF\xBD"
                       "\xF0\x90\x80\x80\xF4\x8F\xBF\xBF\"");
    end();
})

// The document is valid UTF-8 whatever the message held, so a byte that is
// not part of a well-formed sequence becomes U+FFFD (D2.1 and D3.7 leave
// fort source unvalidated, so a diagnostic can quote any bytes).
TEST(a_byte_that_starts_no_sequence_becomes_the_replacement_character, {
    for (int byte = HIGH_MIN; byte <= BYTE_MAX; byte++) {
        write_one_byte(byte);
        TEST_ASSERT_EQ_STR(written(), quoted(REPLACEMENT_FORM));
        end();
    }
    sb_free(&expected);
})

TEST(an_overlong_form_becomes_the_replacement_character, {
    begin();
    json_str(&w, str_from_range("\xC0\x80", 2)); // an overlong U+0000
    TEST_ASSERT_EQ_STR(written(), quoted(REPLACEMENT_FORM REPLACEMENT_FORM));
    end();
    begin();
    json_str(&w, str_from_range("\xE0\x80\x80", 3)); // an overlong U+0000 again
    TEST_ASSERT_EQ_STR(written(), quoted(REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM));
    end();
    sb_free(&expected);
})

// A UTF-16 surrogate and a value past U+10FFFF are not UTF-8 either.
TEST(a_surrogate_and_a_value_past_the_last_code_point_are_replaced, {
    begin();
    json_str(&w, str_from_range("\xED\xA0\x80", 3)); // U+D800 if it were one
    TEST_ASSERT_EQ_STR(written(), quoted(REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM));
    end();
    begin();
    json_str(&w, str_from_range("\xF4\x90\x80\x80", 4)); // U+110000 if it were one
    TEST_ASSERT_EQ_STR(written(),
                       quoted(REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM REPLACEMENT_FORM));
    end();
    sb_free(&expected);
})

// A sequence cut short by the end of the string is not well-formed, and
// neither is a lead byte followed by something that is not a continuation.
TEST(a_truncated_sequence_is_replaced_byte_by_byte, {
    begin();
    json_str(&w, str_from_range("\xE2\x82", 2)); // the first two bytes of U+20AC
    TEST_ASSERT_EQ_STR(written(), quoted(REPLACEMENT_FORM REPLACEMENT_FORM));
    end();
    begin();
    json_cstr(&w, "\xC3z");
    TEST_ASSERT_EQ_STR(written(), quoted(REPLACEMENT_FORM "z"));
    end();
    sb_free(&expected);
})

// Only the bad bytes are replaced: the text around them is untouched.
TEST(a_bad_byte_does_not_disturb_the_text_around_it, {
    begin();
    json_cstr(&w, "a\xFF\xC3\xA9z");
    TEST_ASSERT_EQ_STR(written(), quoted("a" REPLACEMENT_FORM "\xC3\xA9z"));
    end();
    sb_free(&expected);
})

// Every printable ASCII byte other than the quote and the backslash is
// written as it stands.
TEST(a_printable_string_is_written_unchanged, {
    begin();
    json_cstr(&w, " !#$%&'()*+,-./0123456789:;<=>?@AZ[]^_`az{|}~");
    TEST_ASSERT_EQ_STR(written(), "\" !#$%&'()*+,-./0123456789:;<=>?@AZ[]^_`az{|}~\"");
    end();
})

TEST(an_empty_string_is_two_quotes, {
    begin();
    json_cstr(&w, "");
    TEST_ASSERT_EQ_STR(written(), "\"\"");
    end();
})

// ---- documents -----------------------------------------------------------------------

// A value may follow a closed container in the same object, which is the
// shape a document of diagnostics has.
TEST(a_member_may_follow_a_nested_container, {
    begin();
    json_object_begin(&w);
    json_key(&w, "version");
    json_uint(&w, 1);
    json_key(&w, "files");
    json_array_begin(&w);
    json_cstr(&w, "main.ft");
    json_array_end(&w);
    json_key(&w, "ok");
    json_bool(&w, false);
    json_object_end(&w);
    TEST_ASSERT_EQ_STR(written(), "{\"version\":1,\"files\":[\"main.ft\"],\"ok\":false}");
    end();
})

// One document of the shape the check mode writes, so the pieces are seen
// together: an array of objects, each with a nested array of objects.
TEST(a_document_of_diagnostics_is_written_whole, {
    begin();
    json_array_begin(&w);
    json_object_begin(&w);
    json_key(&w, "file");
    json_cstr(&w, "main.ft");
    json_key(&w, "line");
    json_uint(&w, 7);
    json_key(&w, "message");
    json_cstr(&w, "cannot assign to immutable 'x'");
    json_key(&w, "notes");
    json_array_begin(&w);
    json_object_begin(&w);
    json_key(&w, "line");
    json_uint(&w, 3);
    json_key(&w, "decl");
    json_null(&w);
    json_object_end(&w);
    json_array_end(&w);
    json_object_end(&w);
    json_array_end(&w);
    TEST_ASSERT_EQ_STR(written(),
                       "[{\"file\":\"main.ft\",\"line\":7,"
                       "\"message\":\"cannot assign to immutable 'x'\","
                       "\"notes\":[{\"line\":3,\"decl\":null}]}]");
    end();
})

// A string longer than the buffer's first capacity is written in full: the
// buffer grows under the writer, which holds no pointer into it.
TEST(a_long_string_is_written_in_full, {
    enum { LONG = 1000 };
    sb_t text;
    sb_init(&text);
    for (int i = 0; i < LONG; i++) {
        sb_push(&text, 'x');
    }
    begin();
    json_str(&w, sb_view(&text));
    TEST_ASSERT_EQ_UINT64(sb_view(&out).len, (uint64_t)LONG + 2);
    TEST_ASSERT_EQ_CHAR(sb_cstr(&out)[0], '"');
    TEST_ASSERT_EQ_CHAR(sb_cstr(&out)[LONG + 1], '"');
    sb_free(&text);
    end();
})

// An escape at each end of a string is written like one in the middle.
TEST(an_escape_at_either_end_is_written, {
    begin();
    json_cstr(&w, "\n x \"");
    TEST_ASSERT_EQ_STR(written(), "\"\\n x \\\"\"");
    end();
})

// Every sequence of the corpus that is well-formed reaches the document as it
// stands: the writer replaces nothing it should keep.
TEST(every_well_formed_sequence_of_the_corpus_is_kept, {
    for (int i = 0; i < UTF8_CASE_COUNT; i++) {
        const utf8_case_t* c = &UTF8_CASES[i];
        if (!c->well_formed) {
            continue;
        }
        begin();
        json_str(&w, str_from_range(c->bytes, c->len));
        const char* want = labelled(&label_expected, c->what, expected_document(c));
        const char* got = labelled(&label_actual, c->what, sb_view(&out));
        TEST_ASSERT_EQ_STR(got, want);
        end();
    }
    free_labels();
})

// Every sequence that is not well-formed loses exactly its own bytes, one
// U+FFFD each: the writer keeps nothing it should replace.
TEST(every_ill_formed_sequence_of_the_corpus_is_replaced, {
    for (int i = 0; i < UTF8_CASE_COUNT; i++) {
        const utf8_case_t* c = &UTF8_CASES[i];
        if (c->well_formed) {
            continue;
        }
        begin();
        json_str(&w, str_from_range(c->bytes, c->len));
        const char* want = labelled(&label_expected, c->what, expected_document(c));
        const char* got = labelled(&label_actual, c->what, sb_view(&out));
        TEST_ASSERT_EQ_STR(got, want);
        end();
    }
    free_labels();
})

// The same corpus between two ASCII letters: a case that is well-formed on
// its own is still well-formed in a sentence, and one that is not still
// costs exactly its own bytes.
TEST(the_corpus_reads_the_same_inside_a_message, {
    sb_t text;
    sb_init(&text);
    for (int i = 0; i < UTF8_CASE_COUNT; i++) {
        const utf8_case_t* c = &UTF8_CASES[i];
        sb_clear(&text);
        sb_push(&text, 'a');
        sb_append_str(&text, str_from_range(c->bytes, c->len));
        sb_push(&text, 'z');
        begin();
        json_str(&w, sb_view(&text));
        const str_t inner = expected_document(c);
        // The inner document without its quotes, between the two letters.
        sb_clear(&label_expected);
        sb_append(&label_expected, c->what);
        sb_append(&label_expected, ": \"a");
        sb_append_str(&label_expected, str_from_range(inner.ptr + 1, inner.len - 2));
        sb_append(&label_expected, "z\"");
        const char* got = labelled(&label_actual, c->what, sb_view(&out));
        TEST_ASSERT_EQ_STR(got, sb_cstr(&label_expected));
        end();
    }
    sb_free(&text);
    free_labels();
})

// Every byte of the ASCII range in one pass: a control byte takes the form
// the golden table gives, the quote and the backslash are escaped, and the
// 0x20 boundary falls between the last escaped byte and the first kept one.
TEST(every_ascii_byte_takes_its_form, {
    for (int byte = 0; byte <= ASCII_MAX; byte++) {
        write_one_byte(byte);
        if (byte <= CONTROL_MAX) {
            TEST_ASSERT_EQ_STR(written(), quoted(CONTROL_FORMS[byte]));
        } else if (byte == (int)'"') {
            TEST_ASSERT_EQ_STR(written(), quoted("\\\""));
        } else if (byte == (int)'\\') {
            TEST_ASSERT_EQ_STR(written(), quoted("\\\\"));
        } else {
            TEST_ASSERT_EQ_STR(written(), quoted_byte(byte));
        }
        end();
    }
    sb_free(&expected);
})

// Exactly two bytes of the printable range are escaped, and the first byte
// that is not escaped at all is the space.
TEST(only_the_quote_and_the_backslash_are_escaped_above_the_controls, {
    uint64_t escaped = 0;
    for (int byte = CONTROL_MAX + 1; byte <= ASCII_MAX; byte++) {
        write_one_byte(byte);
        if (sb_view(&out).len != 3) {
            escaped++;
        }
        end();
    }
    TEST_ASSERT_EQ_UINT64(escaped, (uint64_t)2);
    write_one_byte(CONTROL_MAX + 1);
    TEST_ASSERT_EQ_STR(written(), "\" \"");
    end();
    sb_free(&expected);
})

// One message with a byte of every kind: two escapes, a control byte, an
// ASCII run, a well-formed sequence and a byte that is part of none.
TEST(a_message_of_every_kind_of_byte_is_written_once, {
    begin();
    json_str(&w,
             str_from_range("a\"b\\c\nd\x01"
                            "e\xC3\xA9"
                            "f\xFF"
                            "g",
                            14));
    TEST_ASSERT_EQ_STR(written(),
                       "\"a\\\"b\\\\c\\nd\\u0001"
                       "e\xC3\xA9"
                       "f" REPLACEMENT_FORM "g\"");
    end();
})

// ---- depth ---------------------------------------------------------------------------

// JSON_MAX_DEPTH containers may be open at once.
TEST(nesting_to_the_limit_is_written, {
    begin();
    for (int i = 0; i < JSON_MAX_DEPTH; i++) {
        json_array_begin(&w);
    }
    json_uint(&w, 1);
    for (int i = 0; i < JSON_MAX_DEPTH; i++) {
        json_array_end(&w);
    }
    TEST_ASSERT_EQ_STR(written(), "[[[[[[[[1]]]]]]]]");
    end();
})

// Objects and arrays count against the same limit.
TEST(objects_and_arrays_share_the_limit, {
    begin();
    for (int i = 0; i < JSON_MAX_DEPTH / 2; i++) {
        json_object_begin(&w);
        json_key(&w, "k");
        json_array_begin(&w);
    }
    for (int i = 0; i < JSON_MAX_DEPTH / 2; i++) {
        json_array_end(&w);
        json_object_end(&w);
    }
    TEST_ASSERT_EQ_STR(written(), "{\"k\":[{\"k\":[{\"k\":[{\"k\":[]}]}]}]}");
    end();
})

// Closing every container brings the depth back, so a long document of
// sibling containers is not a nesting error.
TEST(siblings_do_not_add_up_to_the_limit, {
    const uint64_t siblings = (uint64_t)JSON_MAX_DEPTH + (uint64_t)JSON_MAX_DEPTH;
    begin();
    json_array_begin(&w);
    for (uint64_t i = 0; i < siblings; i++) {
        json_object_begin(&w);
        json_object_end(&w);
    }
    json_array_end(&w);
    // The brackets, one "{}" per sibling and one comma between two of them.
    TEST_ASSERT_EQ_UINT64(sb_view(&out).len, 2 + 2 * siblings + siblings - 1);
    end();
})

static void open_one_too_many(void) {
    sb_t local;
    json_t writer;
    sb_init(&local);
    json_init(&writer, &local);
    for (int i = 0; i < JSON_MAX_DEPTH + 1; i++) {
        json_array_begin(&writer);
    }
    sb_free(&local);
}

TEST(nesting_past_the_limit_is_an_internal_error, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(open_one_too_many, err, sizeof err), FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: json: nesting too deep\n");
})

static void close_what_was_not_opened(void) {
    sb_t local;
    json_t writer;
    sb_init(&local);
    json_init(&writer, &local);
    json_object_end(&writer);
    sb_free(&local);
}

TEST(closing_at_depth_zero_is_an_internal_error, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(close_what_was_not_opened, err, sizeof err), FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: json: no container is open\n");
})

static void key_outside_an_object(void) {
    sb_t local;
    json_t writer;
    sb_init(&local);
    json_init(&writer, &local);
    json_key(&writer, "k");
    sb_free(&local);
}

TEST(a_key_outside_an_object_is_an_internal_error, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(key_outside_an_object, err, sizeof err), FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: json_key: no object is open\n");
})

static void init_without_a_buffer(void) {
    json_t writer;
    json_init(&writer, NULL);
}

TEST(a_writer_without_a_buffer_is_an_internal_error, {
    char err[ERR_MAX];
    TEST_ASSERT_EQ_INT32(run_forked(init_without_a_buffer, err, sizeof err), FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: json_init: no buffer\n");
})

int main(int argc, char** argv) {
    TEST_INIT("json", argc, argv);
    TEST_RUN(an_empty_object_is_two_braces);
    TEST_RUN(an_empty_array_is_two_brackets);
    TEST_RUN(a_document_may_be_a_bare_value);
    TEST_RUN(booleans_and_null_are_written_as_literals);
    TEST_RUN(numbers_are_decimal_with_a_sign_for_a_negative_one);
    TEST_RUN(the_widest_numbers_are_written_in_full);
    TEST_RUN(a_string_view_may_hold_any_bytes_including_a_nul);
    TEST_RUN(object_members_are_separated_by_commas);
    TEST_RUN(no_comma_separates_a_key_from_its_value);
    TEST_RUN(array_elements_are_separated_by_commas);
    TEST_RUN(a_closed_container_is_an_element_of_its_parent);
    TEST_RUN(a_reopened_container_starts_empty_again);
    TEST_RUN(the_document_holds_no_whitespace);
    TEST_RUN(a_document_is_appended_to_the_buffer);
    TEST_RUN(the_quote_and_the_backslash_are_escaped);
    TEST_RUN(newline_tab_and_return_are_spelled_out);
    TEST_RUN(a_key_is_escaped_like_a_string);
    TEST_RUN(every_control_byte_is_escaped);
    TEST_RUN(delete_goes_through_verbatim);
    TEST_RUN(a_utf8_sequence_goes_through_verbatim);
    TEST_RUN(a_sequence_of_every_length_goes_through_verbatim);
    TEST_RUN(a_byte_that_starts_no_sequence_becomes_the_replacement_character);
    TEST_RUN(an_overlong_form_becomes_the_replacement_character);
    TEST_RUN(a_surrogate_and_a_value_past_the_last_code_point_are_replaced);
    TEST_RUN(a_truncated_sequence_is_replaced_byte_by_byte);
    TEST_RUN(a_bad_byte_does_not_disturb_the_text_around_it);
    TEST_RUN(a_printable_string_is_written_unchanged);
    TEST_RUN(an_empty_string_is_two_quotes);
    TEST_RUN(a_member_may_follow_a_nested_container);
    TEST_RUN(a_document_of_diagnostics_is_written_whole);
    TEST_RUN(a_long_string_is_written_in_full);
    TEST_RUN(an_escape_at_either_end_is_written);
    TEST_RUN(every_well_formed_sequence_of_the_corpus_is_kept);
    TEST_RUN(every_ill_formed_sequence_of_the_corpus_is_replaced);
    TEST_RUN(the_corpus_reads_the_same_inside_a_message);
    TEST_RUN(every_ascii_byte_takes_its_form);
    TEST_RUN(only_the_quote_and_the_backslash_are_escaped_above_the_controls);
    TEST_RUN(a_message_of_every_kind_of_byte_is_written_once);
    TEST_RUN(nesting_to_the_limit_is_written);
    TEST_RUN(objects_and_arrays_share_the_limit);
    TEST_RUN(siblings_do_not_add_up_to_the_limit);
    TEST_RUN(nesting_past_the_limit_is_an_internal_error);
    TEST_RUN(closing_at_depth_zero_is_an_internal_error);
    TEST_RUN(a_key_outside_an_object_is_an_internal_error);
    TEST_RUN(a_writer_without_a_buffer_is_an_internal_error);
    TEST_EXIT();
}

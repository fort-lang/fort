// Unit tests of lexer.h's token dump, the output of `fort --tokens` whose
// form spec/toolchain.md 1 documents:
// D14.1
//
//     <line>:<col>-<end_line>:<end_col> <value> "<spelling>" <kind>
//
// one token per line, the range 1-based with its end exclusive, the value of
// an integer or char literal and 0 elsewhere, the spelling escaped so that a
// token holding a newline, a tab, a quote or a byte outside printable ASCII
// still occupies one line, and the kind last because it is the only field
// that may hold a space.
// D20.4, D2.5, D2.7
//
// The dump is what tools/diff_tokens.sh compares between this lexer and
// src/fort/lexer.ft over every .ft file in the repository, so the format is
// pinned here and in test/fort/lexer_dump_test.ft, which asserts the same
// lines of the same sources against the other implementation.
#include <stdint.h>
#include <string.h>

#include "lexer.h"
#include "lexer_helpers.h"
#include "str.h"

#include "test.h"

// The literals below are the test data: sample sources and their dumps.
// NOLINTBEGIN(readability-magic-numbers)

// The dump of the last lex, as one string.
static sb_t dump;
static bool dumping = false;

static const char* dump_of(const char* src) {
    if (!dumping) {
        sb_init(&dump);
        dumping = true;
    }
    sb_clear(&dump);
    TEST_UNUSED(lex(src));
    tok_dump(&toks, &dump);
    return sb_cstr(&dump);
}

// The same for a source holding a NUL byte, which strlen could not measure.
static const char* dump_of_bytes(const char* src, uint64_t len) {
    if (!dumping) {
        sb_init(&dump);
        dumping = true;
    }
    sb_clear(&dump);
    TEST_UNUSED(lex_bytes(src, len));
    tok_dump(&toks, &dump);
    return sb_cstr(&dump);
}

static void dump_done(void) {
    if (dumping) {
        sb_free(&dump);
        dumping = false;
    }
}

// ---- the form of a line ---------------------------------------------------------

// The worked example of toolchain.md 1, byte for byte.
TEST(the_worked_example_of_toolchain_section_1, {
    TEST_ASSERT_EQ_STR(dump_of("x = 0x10;\n"),
                       "1:1-1:2 0 \"x\" identifier\n"
                       "1:3-1:4 0 \"=\" =\n"
                       "1:5-1:9 16 \"0x10\" integer literal\n"
                       "1:9-1:10 0 \";\" ;\n"
                       "2:1-2:1 0 \"\" end of file\n");
})

TEST(an_empty_file_dumps_one_line,
     { TEST_ASSERT_EQ_STR(dump_of(""), "1:1-1:1 0 \"\" end of file\n"); })

// The range ends where the next token may begin: `col + len`, exclusive.
TEST(the_range_is_the_tokens_own, {
    TEST_ASSERT_EQ_STR(dump_of("fn\n  +%=@"),
                       "1:1-1:3 0 \"fn\" fn\n"
                       "2:3-2:6 0 \"+%=\" +%=\n"
                       "2:6-2:7 0 \"@\" @\n"
                       "2:7-2:7 0 \"\" end of file\n");
})

// A tab is one column, not a stop.
// D14.2
TEST(a_tab_moves_the_range_by_one_column, {
    TEST_ASSERT_EQ_STR(dump_of("\ta"),
                       "1:2-1:3 0 \"a\" identifier\n"
                       "1:3-1:3 0 \"\" end of file\n");
})

// Every line of a file with several lines carries its own line number.
TEST(each_line_of_a_file_dumps_its_own_number, {
    TEST_ASSERT_EQ_STR(dump_of("a\nbb\n\nccc"),
                       "1:1-1:2 0 \"a\" identifier\n"
                       "2:1-2:3 0 \"bb\" identifier\n"
                       "4:1-4:4 0 \"ccc\" identifier\n"
                       "4:4-4:4 0 \"\" end of file\n");
})

// ---- the value field ------------------------------------------------------------
// D2.5, D2.7

TEST(an_integer_literal_dumps_its_magnitude, {
    TEST_ASSERT_EQ_STR(dump_of("0xFF_F 0b10 0o17 18446744073709551615"),
                       "1:1-1:7 4095 \"0xFF_F\" integer literal\n"
                       "1:8-1:12 2 \"0b10\" integer literal\n"
                       "1:13-1:17 15 \"0o17\" integer literal\n"
                       "1:18-1:38 18446744073709551615 \"18446744073709551615\" integer literal\n"
                       "1:38-1:38 0 \"\" end of file\n");
})

// A char literal carries its byte value and keeps the source's spelling, so
// the escape is visible beside the byte it decoded to.
TEST(a_char_literal_dumps_its_byte_and_its_spelling, {
    TEST_ASSERT_EQ_STR(dump_of("'\\n' '\\xff' 'a'"),
                       "1:1-1:5 10 \"'\\\\n'\" char literal\n"
                       "1:6-1:12 255 \"'\\\\xff'\" char literal\n"
                       "1:13-1:16 97 \"'a'\" char literal\n"
                       "1:16-1:16 0 \"\" end of file\n");
})

// Every other kind leaves the value at 0, a float literal included: the lexer
// computes no float value.
// D2.6
TEST(every_other_kind_dumps_a_zero_value, {
    TEST_ASSERT_EQ_STR(dump_of("1.5e2 \"ab\" own"),
                       "1:1-1:6 0 \"1.5e2\" float literal\n"
                       "1:7-1:11 0 \"ab\" string literal\n"
                       "1:12-1:15 0 \"own\" own\n"
                       "1:15-1:15 0 \"\" end of file\n");
})

// ---- the escaped spelling --------------------------------------------------------

// A string literal's spelling is its decoded bytes, so what the dump escapes
// is what the literal means and not what was typed.
// D2.9
TEST(a_decoded_newline_tab_and_return_are_escaped, {
    TEST_ASSERT_EQ_STR(dump_of("\"a\\nb\\tc\\rd\""),
                       "1:1-1:13 0 \"a\\nb\\tc\\rd\" string literal\n"
                       "1:13-1:13 0 \"\" end of file\n");
})

// The two bytes that would end the field, or start an escape of the dump's
// own, are escaped: `"` and `\`.
TEST(a_quote_and_a_backslash_are_escaped, {
    TEST_ASSERT_EQ_STR(dump_of("\"a\\\"b\\\\c\""),
                       "1:1-1:10 0 \"a\\\"b\\\\c\" string literal\n"
                       "1:10-1:10 0 \"\" end of file\n");
})

// A byte outside printable ASCII is written 0xHH with uppercase digits, the
// NUL and the high bytes of UTF-8 included.
TEST(a_byte_outside_printable_ascii_is_written_in_hex, {
    TEST_ASSERT_EQ_STR(dump_of("\"a\\x00b\\x01\\x7F\xC3\xA9\""),
                       "1:1-1:19 0 \"a\\x00b\\x01\\x7F\\xC3\\xA9\" string literal\n"
                       "1:19-1:19 0 \"\" end of file\n");
})

// A raw tab inside a string literal is a byte of it and is escaped like a
// decoded one, so one token is still one line.
// D2.9
TEST(a_raw_tab_inside_a_literal_is_escaped, {
    TEST_ASSERT_EQ_STR(dump_of("\"a\tb\""),
                       "1:1-1:6 0 \"a\\tb\" string literal\n"
                       "1:6-1:6 0 \"\" end of file\n");
})

// A printable byte stands for itself, the space and the comment markers
// included, so a spelling is readable.
TEST(printable_bytes_stand_for_themselves, {
    TEST_ASSERT_EQ_STR(dump_of("\"a b // c /* d\""),
                       "1:1-1:16 0 \"a b // c /* d\" string literal\n"
                       "1:16-1:16 0 \"\" end of file\n");
})

// The dump of a file that itself holds a NUL byte outside a literal: the byte
// is a lexical error, so the line is dropped, and nothing of it reaches
// stdout to break the one-token-per-line rule.
TEST(a_nul_byte_in_the_source_costs_its_line, {
    TEST_ASSERT_EQ_STR(dump_of_bytes("a\n\0\nb\n", 6),
                       "1:1-1:2 0 \"a\" identifier\n"
                       "3:1-3:2 0 \"b\" identifier\n"
                       "4:1-4:1 0 \"\" end of file\n");
})

// ---- a file that does not lex ----------------------------------------------------

// The dump covers the file whatever was reported: the broken line is dropped
// and the lines around it are there.
// D14.2
TEST(a_broken_line_is_missing_from_the_dump, {
    TEST_ASSERT_EQ_STR(dump_of("a\n#\nb\n"),
                       "1:1-1:2 0 \"a\" identifier\n"
                       "3:1-3:2 0 \"b\" identifier\n"
                       "4:1-4:1 0 \"\" end of file\n");
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)1);
})

// Every kind of token dumps, and the dump has exactly one line per token:
// the count of newlines is the count of tokens.
TEST(the_dump_has_one_line_per_token, {
    const char* text = dump_of("fn (x) f { s = \"a\\n\"; c = 'b'; n = 1; y = 1.5; }");
    uint64_t lines = 0;
    for (const char* p = text; *p != '\0'; p++) {
        if (*p == '\n') {
            lines++;
        }
    }
    TEST_ASSERT_EQ_UINT64(lines, toks.len);
    TEST_ASSERT_EQ_UINT64(toks.len, (uint64_t)24);
})

// An empty vector dumps nothing at all, so a caller may append to a buffer
// that already holds something.
TEST(an_empty_vector_dumps_nothing, {
    tokvec_t v;
    tokvec_init(&v);
    sb_t out;
    sb_init(&out);
    sb_append(&out, "before");
    tok_dump(&v, &out);
    TEST_ASSERT_EQ_STR(sb_cstr(&out), "before");
    sb_free(&out);
    tokvec_free(&v);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("lexer_dump", argc, argv);
    TEST_RUN(the_worked_example_of_toolchain_section_1);
    TEST_RUN(an_empty_file_dumps_one_line);
    TEST_RUN(the_range_is_the_tokens_own);
    TEST_RUN(a_tab_moves_the_range_by_one_column);
    TEST_RUN(each_line_of_a_file_dumps_its_own_number);
    TEST_RUN(an_integer_literal_dumps_its_magnitude);
    TEST_RUN(a_char_literal_dumps_its_byte_and_its_spelling);
    TEST_RUN(every_other_kind_dumps_a_zero_value);
    TEST_RUN(a_decoded_newline_tab_and_return_are_escaped);
    TEST_RUN(a_quote_and_a_backslash_are_escaped);
    TEST_RUN(a_byte_outside_printable_ascii_is_written_in_hex);
    TEST_RUN(a_raw_tab_inside_a_literal_is_escaped);
    TEST_RUN(printable_bytes_stand_for_themselves);
    TEST_RUN(a_nul_byte_in_the_source_costs_its_line);
    TEST_RUN(a_broken_line_is_missing_from_the_dump);
    TEST_RUN(the_dump_has_one_line_per_token);
    TEST_RUN(an_empty_vector_dumps_nothing);
    dump_done();
    lex_done();
    TEST_EXIT();
}

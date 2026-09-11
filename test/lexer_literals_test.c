// Unit tests of lexer.h: the literal forms of D2.5 to D2.9 (integer, float,
// char and string literals), their separator rules, the escapes and their
// errors with the wording of core-language.md 2.
#include <stdint.h>
#include <string.h>

#include "lexer.h"
#include "lexer_helpers.h"

#include "test.h"

// The literals below are the test data: sample values, positions and
// expected texts.
// NOLINTBEGIN(readability-magic-numbers)

// ---- integer literals (D2.5) ------------------------------------------------

TEST(decimal_literals, {
    ASSERT_LEX_OK("0 7 123 4294967296", 4);
    ASSERT_TOK_KIND(0, TOK_INT);
    TEST_ASSERT_EQ_UINT64(tok(0)->ival, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(tok(1)->ival, (uint64_t)7);
    TEST_ASSERT_EQ_UINT64(tok(2)->ival, (uint64_t)123);
    TEST_ASSERT_EQ_UINT64(tok(3)->ival, (uint64_t)4294967296ULL);
    ASSERT_TOK_TEXT(3, "4294967296");
    ASSERT_TOK_RANGE(3, 8, 10);
})

TEST(hex_literals_in_both_cases, {
    ASSERT_LEX_OK("0x7F 0xff 0xDeadBeef 0x0 0x00000001", 5);
    TEST_ASSERT_EQ_UINT64(tok(0)->ival, (uint64_t)0x7F);
    TEST_ASSERT_EQ_UINT64(tok(1)->ival, (uint64_t)0xFF);
    TEST_ASSERT_EQ_UINT64(tok(2)->ival, (uint64_t)0xDEADBEEF);
    TEST_ASSERT_EQ_UINT64(tok(3)->ival, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(tok(4)->ival, (uint64_t)1);
    ASSERT_TOK_KIND(2, TOK_INT);
    ASSERT_TOK_TEXT(2, "0xDeadBeef");
})

TEST(octal_literals, {
    ASSERT_LEX_OK("0o17 0o0 0o777", 3);
    TEST_ASSERT_EQ_UINT64(tok(0)->ival, (uint64_t)15);
    TEST_ASSERT_EQ_UINT64(tok(1)->ival, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(tok(2)->ival, (uint64_t)511);
})

TEST(binary_literals, {
    ASSERT_LEX_OK("0b1010 0b0 0b1", 3);
    TEST_ASSERT_EQ_UINT64(tok(0)->ival, (uint64_t)10);
    TEST_ASSERT_EQ_UINT64(tok(1)->ival, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(tok(2)->ival, (uint64_t)1);
})

TEST(underscores_between_digits, {
    ASSERT_LEX_OK("1_000_000 0xFF_FF 0o1_7 0b1_0 1_2_3", 5);
    TEST_ASSERT_EQ_UINT64(tok(0)->ival, (uint64_t)1000000);
    TEST_ASSERT_EQ_UINT64(tok(1)->ival, (uint64_t)0xFFFF);
    TEST_ASSERT_EQ_UINT64(tok(2)->ival, (uint64_t)15);
    TEST_ASSERT_EQ_UINT64(tok(3)->ival, (uint64_t)2);
    TEST_ASSERT_EQ_UINT64(tok(4)->ival, (uint64_t)123);
    ASSERT_TOK_TEXT(0, "1_000_000");
    ASSERT_TOK_RANGE(0, 0, 9);
})

TEST(largest_integer_literals, {
    ASSERT_LEX_OK("18446744073709551615 0xFFFF_FFFF_FFFF_FFFF 0o1777777777777777777777 "
                  "0b1111111111111111111111111111111111111111111111111111111111111111",
                  4);
    for (uint64_t i = 0; i < 4; i++) {
        ASSERT_TOK_KIND(i, TOK_INT);
        TEST_ASSERT_EQ_UINT64(tok(i)->ival, UINT64_MAX);
    }
    ASSERT_LEX_OK("0x0000000000000000000000000000000000FF", 1);
    TEST_ASSERT_EQ_UINT64(tok(0)->ival, (uint64_t)0xFF);
})

TEST(just_too_large_integer_literals, {
    ASSERT_LEX_ERROR("18446744073709551616", "t.ft:1:1: error: integer literal too large\n");
    ASSERT_LEX_ERROR("x = 0x1_0000_0000_0000_0000;",
                     "t.ft:1:5: error: integer literal too large\n");
    ASSERT_LEX_ERROR("0o2000000000000000000000", "t.ft:1:1: error: integer literal too large\n");
    ASSERT_LEX_ERROR("0b10000000000000000000000000000000000000000000000000000000000000000",
                     "t.ft:1:1: error: integer literal too large\n");
    ASSERT_LEX_ERROR("99999999999999999999999", "t.ft:1:1: error: integer literal too large\n");
})

TEST(a_float_literal_has_no_magnitude_limit, {
    ASSERT_LEX_OK("18446744073709551616.0", 1);
    ASSERT_TOK_KIND(0, TOK_FLOAT);
    ASSERT_TOK_TEXT(0, "18446744073709551616.0");
    TEST_ASSERT_EQ_UINT64(tok(0)->ival, (uint64_t)0);
    ASSERT_LEX_OK("99999999999999999999999e99", 1);
    ASSERT_TOK_KIND(0, TOK_FLOAT);
    ASSERT_LEX_OK("18446744073709551616.0 1", 2);
    ASSERT_TOK_KIND(0, TOK_FLOAT);
    ASSERT_TOK_KIND(1, TOK_INT);
    ASSERT_LEX_ERROR("18446744073709551616.0f",
                     "t.ft:1:23: error: float literals have no suffix\n");
})

TEST(decimal_literal_may_not_start_with_zero, {
    ASSERT_LEX_ERROR("i32 b = 017;", "t.ft:1:9: error: decimal literal may not start with '0'\n");
    ASSERT_LEX_ERROR("00", "t.ft:1:1: error: decimal literal may not start with '0'\n");
    ASSERT_LEX_ERROR("0_1", "t.ft:1:1: error: decimal literal may not start with '0'\n");
    ASSERT_LEX_ERROR("09.5", "t.ft:1:1: error: decimal literal may not start with '0'\n");
    ASSERT_LEX_OK("0.5 0e1 0.0e0", 3);
    ASSERT_TOK_KIND(0, TOK_FLOAT);
    ASSERT_TOK_KIND(1, TOK_FLOAT);
    ASSERT_TOK_KIND(2, TOK_FLOAT);
})

TEST(underscore_must_stand_between_two_digits, {
    ASSERT_LEX_ERROR("i32 c = 1__0;", "t.ft:1:10: error: '_' must stand between two digits\n");
    ASSERT_LEX_ERROR("1_", "t.ft:1:2: error: '_' must stand between two digits\n");
    ASSERT_LEX_ERROR("1_;", "t.ft:1:2: error: '_' must stand between two digits\n");
    ASSERT_LEX_ERROR("0x_F", "t.ft:1:3: error: '_' must stand between two digits\n");
    ASSERT_LEX_ERROR("0b1_2", "t.ft:1:4: error: '_' must stand between two digits\n");
    ASSERT_LEX_ERROR("0xF_g", "t.ft:1:4: error: '_' must stand between two digits\n");
    ASSERT_LEX_ERROR("1_x", "t.ft:1:2: error: '_' must stand between two digits\n");
})

TEST(integer_literals_have_no_suffix, {
    ASSERT_LEX_ERROR("i32 d = 10u;", "t.ft:1:11: error: integer literals have no suffix\n");
    ASSERT_LEX_ERROR("0xFFg", "t.ft:1:5: error: integer literals have no suffix\n");
    ASSERT_LEX_ERROR("0b1u", "t.ft:1:4: error: integer literals have no suffix\n");
    ASSERT_LEX_ERROR("1L", "t.ft:1:2: error: integer literals have no suffix\n");
    ASSERT_LEX_ERROR("0z", "t.ft:1:2: error: integer literals have no suffix\n");
    ASSERT_LEX_ERROR("1e", "t.ft:1:2: error: integer literals have no suffix\n");
    ASSERT_LEX_ERROR("1e+", "t.ft:1:2: error: integer literals have no suffix\n");
    ASSERT_LEX_ERROR("1ex", "t.ft:1:2: error: integer literals have no suffix\n");
    ASSERT_LEX_ERROR("0X7F", "t.ft:1:2: error: integer literals have no suffix\n");
    ASSERT_LEX_ERROR("0B1", "t.ft:1:2: error: integer literals have no suffix\n");
    ASSERT_LEX_ERROR("0O7", "t.ft:1:2: error: integer literals have no suffix\n");
})

TEST(digits_outside_the_radix_are_errors, {
    ASSERT_LEX_ERROR("0b12", "t.ft:1:4: error: invalid digit '2' in binary literal\n");
    ASSERT_LEX_ERROR("0o8", "t.ft:1:3: error: invalid digit '8' in octal literal\n");
    ASSERT_LEX_ERROR("0o79", "t.ft:1:4: error: invalid digit '9' in octal literal\n");
    ASSERT_LEX_ERROR("0b1f", "t.ft:1:4: error: invalid digit 'f' in binary literal\n");
    ASSERT_LEX_ERROR("0o7A", "t.ft:1:4: error: invalid digit 'A' in octal literal\n");
})

TEST(a_prefix_needs_at_least_one_digit, {
    ASSERT_LEX_ERROR("0x", "t.ft:1:1: error: hex literal needs at least one digit\n");
    ASSERT_LEX_ERROR("0x;", "t.ft:1:1: error: hex literal needs at least one digit\n");
    ASSERT_LEX_ERROR("0xg", "t.ft:1:1: error: hex literal needs at least one digit\n");
    ASSERT_LEX_ERROR("0o ", "t.ft:1:1: error: octal literal needs at least one digit\n");
    ASSERT_LEX_ERROR("0b", "t.ft:1:1: error: binary literal needs at least one digit\n");
})

TEST(integer_followed_by_a_range_is_two_tokens, {
    ASSERT_LEX_OK("1..5", 3);
    ASSERT_TOK_KIND(0, TOK_INT);
    ASSERT_TOK_KIND(1, TOK_DOT_DOT);
    ASSERT_TOK_KIND(2, TOK_INT);
    TEST_ASSERT_EQ_UINT64(tok(2)->ival, (uint64_t)5);
    ASSERT_TOK_POS(2, 1, 4);
    ASSERT_LEX_OK("0..0x10", 3);
    TEST_ASSERT_EQ_UINT64(tok(2)->ival, (uint64_t)16);
})

TEST(integer_directly_followed_by_punctuation, {
    ASSERT_LEX_OK("a[0]+1;", 7);
    ASSERT_TOK_KIND(2, TOK_INT);
    ASSERT_TOK_KIND(3, TOK_RBRACKET);
    ASSERT_TOK_KIND(4, TOK_PLUS);
    ASSERT_TOK_KIND(5, TOK_INT);
    ASSERT_TOK_KIND(6, TOK_SEMI);
})

// ---- float literals (D2.6) ---------------------------------------------------

TEST(float_literal_with_a_fraction, {
    ASSERT_LEX_OK("1.0 0.5 123.456", 3);
    ASSERT_TOK_KIND(0, TOK_FLOAT);
    ASSERT_TOK_TEXT(0, "1.0");
    ASSERT_TOK_RANGE(0, 0, 3);
    ASSERT_TOK_KIND(1, TOK_FLOAT);
    ASSERT_TOK_TEXT(1, "0.5");
    ASSERT_TOK_KIND(2, TOK_FLOAT);
    ASSERT_TOK_TEXT(2, "123.456");
    ASSERT_TOK_POS(2, 1, 9);
})

TEST(float_literal_with_an_exponent, {
    ASSERT_LEX_OK("2.5e-3 6.02E23 1e10 1E+5 0e0 7.0e+1", 6);
    ASSERT_TOK_TEXT(0, "2.5e-3");
    ASSERT_TOK_TEXT(1, "6.02E23");
    ASSERT_TOK_TEXT(2, "1e10");
    ASSERT_TOK_TEXT(3, "1E+5");
    ASSERT_TOK_TEXT(4, "0e0");
    ASSERT_TOK_TEXT(5, "7.0e+1");
    for (uint64_t i = 0; i < 6; i++) {
        ASSERT_TOK_KIND(i, TOK_FLOAT);
    }
})

TEST(float_digit_groups_take_underscores, {
    ASSERT_LEX_OK("1_0.5_0e1_0", 1);
    ASSERT_TOK_KIND(0, TOK_FLOAT);
    ASSERT_TOK_TEXT(0, "1_0.5_0e1_0");
    ASSERT_LEX_ERROR("1.5_", "t.ft:1:4: error: '_' must stand between two digits\n");
    ASSERT_LEX_ERROR("1.5__0", "t.ft:1:4: error: '_' must stand between two digits\n");
    ASSERT_LEX_ERROR("1e1__0", "t.ft:1:4: error: '_' must stand between two digits\n");
})

TEST(a_huge_exponent_or_fraction_is_still_a_float, {
    ASSERT_LEX_OK("1e99999999999999999999 0.99999999999999999999999", 2);
    ASSERT_TOK_KIND(0, TOK_FLOAT);
    ASSERT_TOK_KIND(1, TOK_FLOAT);
})

TEST(digits_dot_alone_is_not_a_float_literal, {
    ASSERT_LEX_ERROR("f64 x = 1.;", "t.ft:1:9: error: '1.' is not a float literal\n");
    ASSERT_LEX_ERROR("12.x", "t.ft:1:1: error: '12.' is not a float literal\n");
    ASSERT_LEX_ERROR("1.e5", "t.ft:1:1: error: '1.' is not a float literal\n");
    ASSERT_LEX_ERROR("1._5", "t.ft:1:1: error: '1.' is not a float literal\n");
    ASSERT_LEX_ERROR("1.", "t.ft:1:1: error: '1.' is not a float literal\n");
    ASSERT_LEX_OK(".5", 2);
    ASSERT_TOK_KIND(0, TOK_DOT);
    ASSERT_TOK_KIND(1, TOK_INT);
})

TEST(float_literals_have_no_suffix, {
    ASSERT_LEX_ERROR("f32 y = 1.0f;", "t.ft:1:12: error: float literals have no suffix\n");
    ASSERT_LEX_ERROR("1.0e", "t.ft:1:4: error: float literals have no suffix\n");
    ASSERT_LEX_ERROR("1e5x", "t.ft:1:4: error: float literals have no suffix\n");
    ASSERT_LEX_ERROR("2.5e-3_", "t.ft:1:7: error: '_' must stand between two digits\n");
})

TEST(float_followed_by_a_range_is_two_tokens, {
    ASSERT_LEX_OK("1.0..2.0", 3);
    ASSERT_TOK_KIND(0, TOK_FLOAT);
    ASSERT_TOK_KIND(1, TOK_DOT_DOT);
    ASSERT_TOK_KIND(2, TOK_FLOAT);
    ASSERT_TOK_TEXT(2, "2.0");
})

TEST(hex_literal_is_never_a_float, {
    ASSERT_LEX_OK("0x1e5 0xA.b", 4);
    ASSERT_TOK_KIND(0, TOK_INT);
    TEST_ASSERT_EQ_UINT64(tok(0)->ival, (uint64_t)0x1E5);
    ASSERT_TOK_KIND(1, TOK_INT);
    ASSERT_TOK_KIND(2, TOK_DOT);
    ASSERT_TOK_KIND(3, TOK_IDENT);
})

// ---- char literals (D2.7, D2.8) ------------------------------------------------

TEST(plain_char_literals, {
    ASSERT_LEX_OK("'x' ' ' '~' '\"'", 4);
    ASSERT_TOK_KIND(0, TOK_CHAR);
    TEST_ASSERT_EQ_UINT64(tok(0)->ival, (uint64_t)'x');
    TEST_ASSERT_EQ_UINT64(tok(1)->ival, (uint64_t)' ');
    TEST_ASSERT_EQ_UINT64(tok(2)->ival, (uint64_t)'~');
    TEST_ASSERT_EQ_UINT64(tok(3)->ival, (uint64_t)'"');
    ASSERT_TOK_TEXT(0, "'x'");
    ASSERT_TOK_RANGE(0, 0, 3);
    ASSERT_TOK_POS(3, 1, 13);
})

TEST(every_escape_in_a_char_literal, {
    ASSERT_LEX_OK("'\\n' '\\t' '\\r' '\\0' '\\\\' '\\'' '\\\"' '\\x41'", 8);
    TEST_ASSERT_EQ_UINT64(tok(0)->ival, (uint64_t)0x0A);
    TEST_ASSERT_EQ_UINT64(tok(1)->ival, (uint64_t)0x09);
    TEST_ASSERT_EQ_UINT64(tok(2)->ival, (uint64_t)0x0D);
    TEST_ASSERT_EQ_UINT64(tok(3)->ival, (uint64_t)0x00);
    TEST_ASSERT_EQ_UINT64(tok(4)->ival, (uint64_t)'\\');
    TEST_ASSERT_EQ_UINT64(tok(5)->ival, (uint64_t)'\'');
    TEST_ASSERT_EQ_UINT64(tok(6)->ival, (uint64_t)'"');
    TEST_ASSERT_EQ_UINT64(tok(7)->ival, (uint64_t)'A');
    for (uint64_t i = 0; i < 8; i++) {
        ASSERT_TOK_KIND(i, TOK_CHAR);
    }
    ASSERT_TOK_RANGE(7, 35, 6);
})

TEST(hex_escape_takes_both_cases_and_the_whole_byte, {
    ASSERT_LEX_OK("'\\x4F' '\\x4f' '\\xff' '\\xFF' '\\x00' '\\x7f'", 6);
    TEST_ASSERT_EQ_UINT64(tok(0)->ival, (uint64_t)0x4F);
    TEST_ASSERT_EQ_UINT64(tok(1)->ival, (uint64_t)0x4F);
    TEST_ASSERT_EQ_UINT64(tok(2)->ival, (uint64_t)0xFF);
    TEST_ASSERT_EQ_UINT64(tok(3)->ival, (uint64_t)0xFF);
    TEST_ASSERT_EQ_UINT64(tok(4)->ival, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(tok(5)->ival, (uint64_t)0x7F);
})

TEST(hex_escape_needs_exactly_two_digits, {
    ASSERT_LEX_ERROR("char d = '\\x4';", "t.ft:1:11: error: '\\x' needs exactly two hex digits\n");
    ASSERT_LEX_ERROR("'\\x'", "t.ft:1:2: error: '\\x' needs exactly two hex digits\n");
    ASSERT_LEX_ERROR("'\\xG1'", "t.ft:1:2: error: '\\x' needs exactly two hex digits\n");
    ASSERT_LEX_ERROR("'\\x1G'", "t.ft:1:2: error: '\\x' needs exactly two hex digits\n");
    ASSERT_LEX_ERROR("'\\x123'", "t.ft:1:1: error: char literal holds exactly one character\n");
})

TEST(unknown_escape_is_an_error, {
    ASSERT_LEX_ERROR("char c = '\\q';", "t.ft:1:11: error: unknown escape '\\q'\n");
    ASSERT_LEX_ERROR("'\\a'", "t.ft:1:2: error: unknown escape '\\a'\n");
    ASSERT_LEX_ERROR("'\\1'", "t.ft:1:2: error: unknown escape '\\1'\n");
    ASSERT_LEX_ERROR("'\\X41'", "t.ft:1:2: error: unknown escape '\\X'\n");
    ASSERT_LEX_ERROR("'\\ '", "t.ft:1:2: error: unknown escape '\\ '\n");
})

TEST(unknown_escape_with_a_non_printable_byte, {
    ASSERT_LEX_ERROR("'\\\xC3\xA9'",
                     "t.ft:1:2: error: unknown escape '\\' followed by byte 0xC3\n");
    ASSERT_LEX_ERROR("'\\\t'", "t.ft:1:2: error: unknown escape '\\' followed by byte 0x09\n");
})

TEST(char_literal_holds_exactly_one_character, {
    ASSERT_LEX_ERROR("char b = 'ab';",
                     "t.ft:1:10: error: char literal holds exactly one character\n");
    ASSERT_LEX_ERROR("''", "t.ft:1:1: error: char literal holds exactly one character\n");
    ASSERT_LEX_ERROR("'\\n\\n'", "t.ft:1:1: error: char literal holds exactly one character\n");
    ASSERT_LEX_ERROR("'a\"'", "t.ft:1:1: error: char literal holds exactly one character\n");
})

TEST(non_ascii_byte_in_a_char_literal, {
    ASSERT_LEX_ERROR("char a = '\xC3\xA9';",
                     "t.ft:1:11: error: non-ASCII byte in char literal; use a string\n");
    ASSERT_LEX_ERROR("'\x80'", "t.ft:1:2: error: non-ASCII byte in char literal; use a string\n");
})

TEST(control_character_in_a_char_literal, {
    ASSERT_LEX_ERROR("'\t'", "t.ft:1:2: error: control character in char literal; use an escape\n");
    // A lone `\r` is whitespace (D2.1), so a resync ends the line there and
    // the `'` left after it opens a literal of its own.
    ASSERT_LEX_ERRORS("'\r'",
                      2,
                      "t.ft:1:2: error: control character in char literal; use an escape\n"
                      "t.ft:1:3: error: unterminated char literal\n");
    ASSERT_LEX_ERROR("'\x7F'",
                     "t.ft:1:2: error: control character in char literal; use an escape\n");
})

TEST(unterminated_char_literal, {
    ASSERT_LEX_ERROR("'a", "t.ft:1:1: error: unterminated char literal\n");
    // The `'` left on the next line is a second unterminated literal, on its
    // own line, so it is reported as well (D14.2).
    ASSERT_LEX_ERRORS("x = 'a\n'",
                      2,
                      "t.ft:1:5: error: unterminated char literal\n"
                      "t.ft:2:1: error: unterminated char literal\n");
    ASSERT_LEX_ERRORS("'\n'",
                      2,
                      "t.ft:1:1: error: unterminated char literal\n"
                      "t.ft:2:1: error: unterminated char literal\n");
    ASSERT_LEX_ERROR("'", "t.ft:1:1: error: unterminated char literal\n");
    ASSERT_LEX_ERROR("'\\", "t.ft:1:1: error: unterminated char literal\n");
    ASSERT_LEX_ERROR("'\\'", "t.ft:1:1: error: unterminated char literal\n");
    ASSERT_LEX_ERROR("'\\\n", "t.ft:1:1: error: unterminated char literal\n");
})

// ---- string literals (D2.8, D2.9) ----------------------------------------------

TEST(string_literal_is_decoded_into_the_pool, {
    const char* src = "\"hello\"";
    TEST_ASSERT_TRUE(lex(src));
    ASSERT_TOK_KIND(0, TOK_STRING);
    ASSERT_TOK_TEXT(0, "hello");
    TEST_ASSERT_EQ_UINT64(tok(0)->text.len, (uint64_t)5);
    TEST_ASSERT_EQ_STR(tok(0)->text.ptr, "hello");
    TEST_ASSERT_TRUE(tok(0)->text.ptr != src);
    ASSERT_TOK_RANGE(0, 0, 7);
    ASSERT_TOK_POS(0, 1, 1);
    ASSERT_TOK_POS(1, 1, 8);
})

TEST(empty_string_literal, {
    ASSERT_LEX_OK("\"\"", 1);
    ASSERT_TOK_KIND(0, TOK_STRING);
    TEST_ASSERT_EQ_UINT64(tok(0)->text.len, (uint64_t)0);
    TEST_ASSERT_EQ_STR(tok(0)->text.ptr, "");
    ASSERT_TOK_RANGE(0, 0, 2);
})

TEST(every_escape_in_a_string_literal, {
    ASSERT_LEX_OK("\"a\\nb\\tc\\rd\\\\e\\'f\\\"g\\x41h\"", 1);
    ASSERT_TOK_TEXT(0, "a\nb\tc\rd\\e'f\"gAh");
    ASSERT_TOK_RANGE(0, 0, 26);
})

TEST(string_literal_keeps_an_escaped_nul, {
    TEST_ASSERT_TRUE(lex("\"a\\0b\\x00c\""));
    TEST_ASSERT_EQ_UINT64(tok(0)->text.len, (uint64_t)5);
    TEST_ASSERT_EQ_CHAR(tok(0)->text.ptr[0], 'a');
    TEST_ASSERT_EQ_CHAR(tok(0)->text.ptr[1], '\0');
    TEST_ASSERT_EQ_CHAR(tok(0)->text.ptr[2], 'b');
    TEST_ASSERT_EQ_CHAR(tok(0)->text.ptr[3], '\0');
    TEST_ASSERT_EQ_CHAR(tok(0)->text.ptr[4], 'c');
    TEST_ASSERT_EQ_CHAR(tok(0)->text.ptr[5], '\0');
})

TEST(string_literal_keeps_non_ascii_bytes, {
    ASSERT_LEX_OK("\"caf\xC3\xA9\"", 1);
    ASSERT_TOK_TEXT(0, "caf\xC3\xA9");
    TEST_ASSERT_EQ_UINT64(tok(0)->text.len, (uint64_t)5);
    ASSERT_TOK_POS(1, 1, 8);
})

TEST(string_literal_keeps_raw_tabs_and_carriage_returns, {
    ASSERT_LEX_OK("\"a\tb\rc\"", 1);
    ASSERT_TOK_TEXT(0, "a\tb\rc");
})

TEST(string_literal_may_hold_comment_markers_and_quotes, {
    ASSERT_LEX_OK("\"// not a comment /* nor this\" 'x'", 2);
    ASSERT_TOK_TEXT(0, "// not a comment /* nor this");
    ASSERT_TOK_KIND(1, TOK_CHAR);
    ASSERT_LEX_OK("\"it's\"", 1);
    ASSERT_TOK_TEXT(0, "it's");
})

TEST(raw_newline_in_a_string_is_an_error, {
    // The rest of the literal stands on the next line, where its closing
    // quote opens an unterminated literal of its own (D14.2).
    ASSERT_LEX_ERRORS("s = \"ab\ncd\"",
                      2,
                      "t.ft:1:5: error: unterminated string literal\n"
                      "t.ft:2:3: error: unterminated string literal\n");
    ASSERT_LEX_ERRORS("\"\n\"",
                      2,
                      "t.ft:1:1: error: unterminated string literal\n"
                      "t.ft:2:1: error: unterminated string literal\n");
})

TEST(unterminated_string_literal, {
    ASSERT_LEX_ERROR("\"abc", "t.ft:1:1: error: unterminated string literal\n");
    ASSERT_LEX_ERROR("\"", "t.ft:1:1: error: unterminated string literal\n");
    ASSERT_LEX_ERROR("\"abc\\\"", "t.ft:1:1: error: unterminated string literal\n");
    ASSERT_LEX_ERROR("\"abc\\", "t.ft:1:1: error: unterminated string literal\n");
    ASSERT_LEX_ERRORS("\"abc\\\nx\"",
                      2,
                      "t.ft:1:1: error: unterminated string literal\n"
                      "t.ft:2:2: error: unterminated string literal\n");
})

TEST(escape_errors_inside_a_string, {
    ASSERT_LEX_ERROR("\"ab\\qc\"", "t.ft:1:4: error: unknown escape '\\q'\n");
    ASSERT_LEX_ERROR("\"ab\\x4\"", "t.ft:1:4: error: '\\x' needs exactly two hex digits\n");
    ASSERT_LEX_ERROR("\"\\x\"", "t.ft:1:2: error: '\\x' needs exactly two hex digits\n");
})

TEST(adjacent_string_literals_are_two_tokens, {
    ASSERT_LEX_OK("\"a\" \"b\"", 2);
    ASSERT_TOK_KIND(0, TOK_STRING);
    ASSERT_TOK_KIND(1, TOK_STRING);
    ASSERT_TOK_TEXT(0, "a");
    ASSERT_TOK_TEXT(1, "b");
    ASSERT_LEX_OK("\"a\"\"b\"", 2);
    ASSERT_TOK_TEXT(1, "b");
    ASSERT_TOK_POS(1, 1, 4);
})

TEST(many_strings_stay_valid_in_the_pool, {
    sb_t src;
    sb_init(&src);
    for (int i = 0; i < 2000; i++) {
        sb_append(&src, "\"str\" ");
    }
    TEST_ASSERT_TRUE(lex(sb_cstr(&src)));
    TEST_ASSERT_EQ_UINT64(toks.len, (uint64_t)2001);
    for (uint64_t i = 0; i < 2000; i++) {
        ASSERT_TOK_KIND(i, TOK_STRING);
        ASSERT_TOK_TEXT(i, "str");
    }
    sb_free(&src);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("lexer_literals", argc, argv);
    TEST_RUN(decimal_literals);
    TEST_RUN(hex_literals_in_both_cases);
    TEST_RUN(octal_literals);
    TEST_RUN(binary_literals);
    TEST_RUN(underscores_between_digits);
    TEST_RUN(largest_integer_literals);
    TEST_RUN(just_too_large_integer_literals);
    TEST_RUN(a_float_literal_has_no_magnitude_limit);
    TEST_RUN(decimal_literal_may_not_start_with_zero);
    TEST_RUN(underscore_must_stand_between_two_digits);
    TEST_RUN(integer_literals_have_no_suffix);
    TEST_RUN(digits_outside_the_radix_are_errors);
    TEST_RUN(a_prefix_needs_at_least_one_digit);
    TEST_RUN(integer_followed_by_a_range_is_two_tokens);
    TEST_RUN(integer_directly_followed_by_punctuation);
    TEST_RUN(float_literal_with_a_fraction);
    TEST_RUN(float_literal_with_an_exponent);
    TEST_RUN(float_digit_groups_take_underscores);
    TEST_RUN(a_huge_exponent_or_fraction_is_still_a_float);
    TEST_RUN(digits_dot_alone_is_not_a_float_literal);
    TEST_RUN(float_literals_have_no_suffix);
    TEST_RUN(float_followed_by_a_range_is_two_tokens);
    TEST_RUN(hex_literal_is_never_a_float);
    TEST_RUN(plain_char_literals);
    TEST_RUN(every_escape_in_a_char_literal);
    TEST_RUN(hex_escape_takes_both_cases_and_the_whole_byte);
    TEST_RUN(hex_escape_needs_exactly_two_digits);
    TEST_RUN(unknown_escape_is_an_error);
    TEST_RUN(unknown_escape_with_a_non_printable_byte);
    TEST_RUN(char_literal_holds_exactly_one_character);
    TEST_RUN(non_ascii_byte_in_a_char_literal);
    TEST_RUN(control_character_in_a_char_literal);
    TEST_RUN(unterminated_char_literal);
    TEST_RUN(string_literal_is_decoded_into_the_pool);
    TEST_RUN(empty_string_literal);
    TEST_RUN(every_escape_in_a_string_literal);
    TEST_RUN(string_literal_keeps_an_escaped_nul);
    TEST_RUN(string_literal_keeps_non_ascii_bytes);
    TEST_RUN(string_literal_keeps_raw_tabs_and_carriage_returns);
    TEST_RUN(string_literal_may_hold_comment_markers_and_quotes);
    TEST_RUN(raw_newline_in_a_string_is_an_error);
    TEST_RUN(unterminated_string_literal);
    TEST_RUN(escape_errors_inside_a_string);
    TEST_RUN(adjacent_string_literals_are_two_tokens);
    TEST_RUN(many_strings_stay_valid_in_the_pool);
    lex_done();
    TEST_EXIT();
}

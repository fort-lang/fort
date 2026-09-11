// Unit tests of lexer.h: the file, positions and comments (D2.1, D2.2,
// D14.2), identifiers, keywords and reserved words (D2.3, D2.4), operators
// (D2.10), the kind names and the token vector. The literal forms are in
// lexer_literals_test.c.
#include "lexer.h"

#include <stdint.h>
#include <string.h>

#include "lexer_helpers.h"

#include "test.h"

// The literals below are the test data: sample values, positions and
// expected texts.
// NOLINTBEGIN(readability-magic-numbers)

// ---- files, whitespace and positions (D2.1, D14.2) -------------------------

TEST(empty_file_is_only_eof, {
    ASSERT_LEX_OK("", 0);
    ASSERT_TOK_POS(0, 1, 1);
    ASSERT_TOK_RANGE(0, 0, 0);
    TEST_ASSERT_EQ_UINT64(tok(0)->text.len, (uint64_t)0);
})

TEST(whitespace_only_file_is_only_eof, {
    ASSERT_LEX_OK(" \t\r\n \n", 0);
    ASSERT_TOK_POS(0, 3, 1);
    ASSERT_TOK_RANGE(0, 6, 0);
})

TEST(file_without_trailing_newline, {
    ASSERT_LEX_OK("x", 1);
    ASSERT_TOK_KIND(0, TOK_IDENT);
    ASSERT_TOK_POS(1, 1, 2);
    ASSERT_TOK_RANGE(1, 1, 0);
})

TEST(eof_sits_after_the_trailing_newline, {
    ASSERT_LEX_OK("x;\n", 2);
    ASSERT_TOK_POS(2, 2, 1);
    ASSERT_TOK_RANGE(2, 3, 0);
})

TEST(bom_is_skipped_and_takes_no_column, {
    ASSERT_LEX_OK("\xEF\xBB\xBFx", 1);
    ASSERT_TOK_KIND(0, TOK_IDENT);
    ASSERT_TOK_TEXT(0, "x");
    ASSERT_TOK_POS(0, 1, 1);
    ASSERT_TOK_RANGE(0, 3, 1);
    ASSERT_LEX_OK("\xEF\xBB\xBF", 0);
    ASSERT_TOK_POS(0, 1, 1);
    ASSERT_TOK_RANGE(0, 3, 0);
})

TEST(cr_is_whitespace, {
    ASSERT_LEX_OK("a\r\nb\rc", 3);
    ASSERT_TOK_POS(0, 1, 1);
    ASSERT_TOK_POS(1, 2, 1);
    ASSERT_TOK_POS(2, 2, 3);
})

TEST(tab_counts_as_one_column, {
    ASSERT_LEX_OK("\t\tx\ty", 2);
    ASSERT_TOK_POS(0, 1, 3);
    ASSERT_TOK_POS(1, 1, 5);
    ASSERT_TOK_RANGE(0, 2, 1);
    ASSERT_TOK_RANGE(1, 4, 1);
})

TEST(positions_advance_across_lines, {
    ASSERT_LEX_OK("fn main() {\n    return 0;\n}\n", 9);
    ASSERT_TOK_POS(0, 1, 1);
    ASSERT_TOK_POS(1, 1, 4);
    ASSERT_TOK_POS(2, 1, 8);
    ASSERT_TOK_POS(3, 1, 9);
    ASSERT_TOK_POS(4, 1, 11);
    ASSERT_TOK_POS(5, 2, 5);
    ASSERT_TOK_POS(6, 2, 12);
    ASSERT_TOK_POS(7, 2, 13);
    ASSERT_TOK_POS(8, 3, 1);
    ASSERT_TOK_POS(9, 4, 1);
    ASSERT_TOK_RANGE(5, 16, 6);
    ASSERT_TOK_RANGE(9, 28, 0);
})

TEST(non_ascii_outside_strings_and_comments_is_an_error, {
    ASSERT_LEX_ERROR("x = \xC3\xA9;",
                     "t.ft:1:5: error: non-ASCII byte outside a string literal or comment\n");
    TEST_ASSERT_EQ_UINT64(toks.len, (uint64_t)2);
    ASSERT_LEX_ERROR("\xEF\xBBx",
                     "t.ft:1:1: error: non-ASCII byte outside a string literal or comment\n");
})

TEST(unexpected_printable_character_is_an_error, {
    ASSERT_LEX_ERROR("a # b", "t.ft:1:3: error: unexpected character '#'\n");
    ASSERT_LEX_ERROR("#", "t.ft:1:1: error: unexpected character '#'\n");
    ASSERT_LEX_ERROR("`", "t.ft:1:1: error: unexpected character '`'\n");
    ASSERT_LEX_ERROR("$", "t.ft:1:1: error: unexpected character '$'\n");
    ASSERT_LEX_ERROR("\\", "t.ft:1:1: error: unexpected character '\\'\n");
})

TEST(unexpected_control_byte_is_an_error, {
    ASSERT_LEX_ERROR("\x01", "t.ft:1:1: error: unexpected byte 0x01\n");
    ASSERT_LEX_ERROR("x\x7F", "t.ft:1:2: error: unexpected byte 0x7F\n");
    ASSERT_LEX_ERROR("\f", "t.ft:1:1: error: unexpected byte 0x0C\n");
    ASSERT_LEX_ERROR("\v", "t.ft:1:1: error: unexpected byte 0x0B\n");
    TEST_ASSERT_FALSE(lex_bytes("\0", 1));
    TEST_ASSERT_EQ_STR(captured(), "t.ft:1:1: error: unexpected byte 0x00\n");
})

TEST(error_position_counts_lines_columns_and_tabs, {
    ASSERT_LEX_ERROR("a\n\n\t\tb #", "t.ft:3:5: error: unexpected character '#'\n");
    TEST_ASSERT_EQ_UINT64(toks.len, (uint64_t)2);
})

TEST(error_keeps_the_tokens_before_it_and_stops, {
    ASSERT_LEX_ERROR("a b # c #", "t.ft:1:5: error: unexpected character '#'\n");
    TEST_ASSERT_EQ_UINT64(toks.len, (uint64_t)2);
    ASSERT_TOK_TEXT(0, "a");
    ASSERT_TOK_TEXT(1, "b");
})

// ---- comments (D2.2) -----------------------------------------------------------

TEST(line_comment_runs_to_the_end_of_the_line, {
    ASSERT_LEX_OK("a // b c \"d\" /* e\nf", 2);
    ASSERT_TOK_TEXT(0, "a");
    ASSERT_TOK_TEXT(1, "f");
    ASSERT_TOK_POS(1, 2, 1);
})

TEST(line_comment_at_the_end_of_the_file, {
    ASSERT_LEX_OK("a //", 1);
    ASSERT_LEX_OK("//", 0);
    ASSERT_LEX_OK("// no newline", 0);
    ASSERT_TOK_POS(0, 1, 14);
})

TEST(block_comment_start_is_an_error, {
    ASSERT_LEX_ERROR("a /* stale */ + 1",
                     "t.ft:1:3: error: block comments are not supported, use '//'\n");
    TEST_ASSERT_EQ_UINT64(toks.len, (uint64_t)1);
    ASSERT_TOK_TEXT(0, "a");
    ASSERT_LEX_ERROR("/*", "t.ft:1:1: error: block comments are not supported, use '//'\n");
    ASSERT_LEX_ERROR("/**/", "t.ft:1:1: error: block comments are not supported, use '//'\n");
    ASSERT_LEX_ERROR("x/*", "t.ft:1:2: error: block comments are not supported, use '//'\n");
    ASSERT_LEX_ERROR("a/*=b", "t.ft:1:2: error: block comments are not supported, use '//'\n");
})

TEST(block_comment_start_is_an_error_wherever_it_stands, {
    ASSERT_LEX_ERROR("// fine\nx = /* y */ 1;\n",
                     "t.ft:2:5: error: block comments are not supported, use '//'\n");
    TEST_ASSERT_EQ_UINT64(toks.len, (uint64_t)2);
    ASSERT_LEX_ERROR("f(/*x*/)", "t.ft:1:3: error: block comments are not supported, use '//'\n");
    ASSERT_LEX_ERROR("a\n\t/*", "t.ft:2:2: error: block comments are not supported, use '//'\n");
})

TEST(a_slash_before_a_separated_star_is_a_division, {
    ASSERT_LEX_OK("a / *p", 4);
    ASSERT_TOK_KIND(0, TOK_IDENT);
    ASSERT_TOK_KIND(1, TOK_SLASH);
    ASSERT_TOK_KIND(2, TOK_STAR);
    ASSERT_TOK_KIND(3, TOK_IDENT);
    ASSERT_TOK_POS(2, 1, 5);
    ASSERT_LEX_OK("a/ *p", 4);
    ASSERT_TOK_KIND(1, TOK_SLASH);
    ASSERT_TOK_KIND(2, TOK_STAR);
    ASSERT_LEX_OK("a /\n*p", 4);
    ASSERT_TOK_POS(2, 2, 1);
    ASSERT_LEX_OK("a / *(p)", 6);
    ASSERT_TOK_KIND(2, TOK_STAR);
    ASSERT_TOK_KIND(3, TOK_LPAREN);
})

TEST(line_comments_may_hold_non_ascii_bytes, {
    ASSERT_LEX_OK("// caf\xC3\xA9\nx // na\xC3\xAFve", 1);
    ASSERT_TOK_TEXT(0, "x");
    ASSERT_TOK_POS(0, 2, 1);
})

TEST(comment_markers_inside_a_line_comment_are_text, {
    ASSERT_LEX_OK("// /* still a line comment */\nx", 1);
    ASSERT_TOK_TEXT(0, "x");
    ASSERT_LEX_OK("x // //\n", 1);
})

// ---- identifiers, keywords and reserved words (D2.3, D2.4) -------------------

TEST(identifier_forms, {
    ASSERT_LEX_OK("_ a1 _x_ CamelCase __ a_b_c z9", 7);
    ASSERT_TOK_TEXT(0, "_");
    ASSERT_TOK_TEXT(1, "a1");
    ASSERT_TOK_TEXT(2, "_x_");
    ASSERT_TOK_TEXT(3, "CamelCase");
    ASSERT_TOK_TEXT(4, "__");
    ASSERT_TOK_TEXT(5, "a_b_c");
    ASSERT_TOK_TEXT(6, "z9");
    for (uint64_t i = 0; i < 7; i++) {
        ASSERT_TOK_KIND(i, TOK_IDENT);
    }
    ASSERT_LEX_OK("_1 _0x", 2);
    ASSERT_TOK_KIND(0, TOK_IDENT);
    ASSERT_TOK_KIND(1, TOK_IDENT);
})

TEST(identifier_text_is_a_view_into_the_source, {
    const char* src = "  hello  ";
    TEST_ASSERT_TRUE(lex(src));
    TEST_ASSERT_TRUE(tok(0)->text.ptr == src + 2);
    TEST_ASSERT_EQ_UINT64(tok(0)->text.len, (uint64_t)5);
    ASSERT_TOK_RANGE(0, 2, 5);
})

TEST(identifiers_have_no_length_limit, {
    sb_t long_name;
    sb_init(&long_name);
    for (int i = 0; i < 10000; i++) {
        sb_push(&long_name, 'a');
    }
    TEST_ASSERT_TRUE(lex(sb_cstr(&long_name)));
    ASSERT_TOK_KIND(0, TOK_IDENT);
    TEST_ASSERT_EQ_UINT64(tok(0)->text.len, (uint64_t)10000);
    ASSERT_TOK_POS(1, 1, 10001);
    sb_free(&long_name);
})

TEST(every_keyword_lexes_as_its_kind, {
    for (int k = TOK_KW_FIRST; k <= TOK_KW_LAST; k++) {
        const char* word = tok_kind_name((tok_kind_t)k);
        ASSERT_LEX_OK(word, 1);
        ASSERT_TOK_KIND(0, (tok_kind_t)k);
        ASSERT_TOK_TEXT(0, word);
        ASSERT_TOK_RANGE(0, 0, strlen(word));
    }
    TEST_ASSERT_EQ_INT64((int64_t)(TOK_KW_LAST - TOK_KW_FIRST + 1), (int64_t)41);
})

TEST(the_keyword_list_of_d2_4, {
    ASSERT_LEX_OK("as bool break case cast char continue default defer do else enum extern "
                  "f32 f64 false fn for i8 i16 i32 i64 if import mut new noreturn null own "
                  "return sizeof string struct switch true u8 u16 u32 u64 void while",
                  41);
    ASSERT_TOK_KIND(0, TOK_KW_AS);
    ASSERT_TOK_KIND(5, TOK_KW_CHAR);
    ASSERT_TOK_KIND(13, TOK_KW_F32);
    ASSERT_TOK_KIND(18, TOK_KW_I8);
    ASSERT_TOK_KIND(28, TOK_KW_OWN);
    ASSERT_TOK_KIND(31, TOK_KW_STRING);
    ASSERT_TOK_KIND(40, TOK_KW_WHILE);
})

TEST(own_is_a_keyword, {
    ASSERT_LEX_OK("own mut node* n", 5);
    ASSERT_TOK_KIND(0, TOK_KW_OWN);
    ASSERT_TOK_KIND(1, TOK_KW_MUT);
    ASSERT_TOK_KIND(2, TOK_IDENT);
    ASSERT_TOK_KIND(3, TOK_STAR);
})

TEST(a_word_extending_a_keyword_is_an_identifier, {
    ASSERT_LEX_OK("iff i3 fnx structs _if if_ i32_t", 7);
    for (uint64_t i = 0; i < 7; i++) {
        ASSERT_TOK_KIND(i, TOK_IDENT);
    }
})

TEST(keywords_are_case_sensitive, {
    ASSERT_LEX_OK("If TRUE Fn i32 I32", 5);
    ASSERT_TOK_KIND(0, TOK_IDENT);
    ASSERT_TOK_KIND(1, TOK_IDENT);
    ASSERT_TOK_KIND(2, TOK_IDENT);
    ASSERT_TOK_KIND(3, TOK_KW_I32);
    ASSERT_TOK_KIND(4, TOK_IDENT);
})

TEST(universe_functions_are_not_keywords, {
    ASSERT_LEX_OK("del move assert panic print", 5);
    for (uint64_t i = 0; i < 5; i++) {
        ASSERT_TOK_KIND(i, TOK_IDENT);
    }
})

TEST(reserved_words_are_errors, {
    ASSERT_LEX_ERROR("i32 const = 1;", "t.ft:1:5: error: 'const' is a reserved word\n");
    TEST_ASSERT_EQ_UINT64(toks.len, (uint64_t)1);
    ASSERT_LEX_ERROR("async", "t.ft:1:1: error: 'async' is a reserved word\n");
    ASSERT_LEX_ERROR("await", "t.ft:1:1: error: 'await' is a reserved word\n");
    ASSERT_LEX_ERROR("match", "t.ft:1:1: error: 'match' is a reserved word\n");
    ASSERT_LEX_ERROR("pub", "t.ft:1:1: error: 'pub' is a reserved word\n");
    ASSERT_LEX_ERROR("priv", "t.ft:1:1: error: 'priv' is a reserved word\n");
    ASSERT_LEX_ERROR("trait", "t.ft:1:1: error: 'trait' is a reserved word\n");
    ASSERT_LEX_ERROR("type", "t.ft:1:1: error: 'type' is a reserved word\n");
    ASSERT_LEX_ERROR("union", "t.ft:1:1: error: 'union' is a reserved word\n");
    ASSERT_LEX_ERROR("yield", "t.ft:1:1: error: 'yield' is a reserved word\n");
})

TEST(a_word_extending_a_reserved_word_is_an_identifier, {
    ASSERT_LEX_OK("constant types union_ _pub", 4);
    for (uint64_t i = 0; i < 4; i++) {
        ASSERT_TOK_KIND(i, TOK_IDENT);
    }
})

// ---- operators and punctuation (D2.10) -----------------------------------------

TEST(every_operator_lexes_alone_as_its_kind, {
    for (int k = TOK_OP_FIRST; k <= TOK_OP_LAST; k++) {
        const char* op = tok_kind_name((tok_kind_t)k);
        ASSERT_LEX_OK(op, 1);
        ASSERT_TOK_KIND(0, (tok_kind_t)k);
        ASSERT_TOK_TEXT(0, op);
        ASSERT_TOK_RANGE(0, 0, strlen(op));
        ASSERT_TOK_POS(1, 1, strlen(op) + 1);
    }
    TEST_ASSERT_EQ_INT64((int64_t)(TOK_OP_LAST - TOK_OP_FIRST + 1), (int64_t)54);
})

TEST(the_operator_list_of_d2_10, {
    ASSERT_LEX_OK("+ - * / % +% -% *% = += -= *= /= %= +%= -%= *%= &= |= ^= <<= >>= == != < <= "
                  "> >= && || ! & | ^ ~ << >> ++ -- ? : :: . -> .. ( ) [ ] { } , ; @",
                  54);
    for (int k = TOK_OP_FIRST; k <= TOK_OP_LAST; k++) {
        ASSERT_TOK_KIND((uint64_t)(k - TOK_OP_FIRST), (tok_kind_t)k);
    }
})

TEST(three_character_operators_and_their_prefixes, {
    ASSERT_LEX_OK("+%= +% + -%= -% - *%= *% * <<= << < >>= >> >", 15);
    ASSERT_TOK_KIND(0, TOK_PLUS_WRAP_ASSIGN);
    ASSERT_TOK_KIND(1, TOK_PLUS_WRAP);
    ASSERT_TOK_KIND(2, TOK_PLUS);
    ASSERT_TOK_KIND(3, TOK_MINUS_WRAP_ASSIGN);
    ASSERT_TOK_KIND(4, TOK_MINUS_WRAP);
    ASSERT_TOK_KIND(5, TOK_MINUS);
    ASSERT_TOK_KIND(6, TOK_STAR_WRAP_ASSIGN);
    ASSERT_TOK_KIND(7, TOK_STAR_WRAP);
    ASSERT_TOK_KIND(8, TOK_STAR);
    ASSERT_TOK_KIND(9, TOK_SHL_ASSIGN);
    ASSERT_TOK_KIND(10, TOK_SHL);
    ASSERT_TOK_KIND(11, TOK_LT);
    ASSERT_TOK_KIND(12, TOK_SHR_ASSIGN);
    ASSERT_TOK_KIND(13, TOK_SHR);
    ASSERT_TOK_KIND(14, TOK_GT);
    ASSERT_TOK_RANGE(0, 0, 3);
    ASSERT_TOK_RANGE(1, 4, 2);
    ASSERT_TOK_RANGE(2, 7, 1);
})

TEST(longest_match_without_spaces, {
    ASSERT_LEX_OK("a+%=b", 3);
    ASSERT_TOK_KIND(1, TOK_PLUS_WRAP_ASSIGN);
    ASSERT_LEX_OK("a+%b", 3);
    ASSERT_TOK_KIND(1, TOK_PLUS_WRAP);
    ASSERT_TOK_KIND(2, TOK_IDENT);
    ASSERT_LEX_OK("a+b", 3);
    ASSERT_TOK_KIND(1, TOK_PLUS);
    ASSERT_LEX_OK("x<<=1", 3);
    ASSERT_TOK_KIND(1, TOK_SHL_ASSIGN);
    ASSERT_LEX_OK("x>>=1", 3);
    ASSERT_TOK_KIND(1, TOK_SHR_ASSIGN);
    ASSERT_LEX_OK("p->q", 3);
    ASSERT_TOK_KIND(1, TOK_ARROW);
    ASSERT_LEX_OK("a::b", 3);
    ASSERT_TOK_KIND(1, TOK_COLON_COLON);
    ASSERT_LEX_OK("a..b", 3);
    ASSERT_TOK_KIND(1, TOK_DOT_DOT);
})

TEST(runs_of_operator_characters_split_greedily, {
    ASSERT_LEX_OK("&&=", 2);
    ASSERT_TOK_KIND(0, TOK_AND_AND);
    ASSERT_TOK_KIND(1, TOK_ASSIGN);
    ASSERT_LEX_OK("||=", 2);
    ASSERT_TOK_KIND(0, TOK_PIPE_PIPE);
    ASSERT_TOK_KIND(1, TOK_ASSIGN);
    ASSERT_LEX_OK("...", 2);
    ASSERT_TOK_KIND(0, TOK_DOT_DOT);
    ASSERT_TOK_KIND(1, TOK_DOT);
    ASSERT_LEX_OK("===", 2);
    ASSERT_TOK_KIND(0, TOK_EQ);
    ASSERT_TOK_KIND(1, TOK_ASSIGN);
    ASSERT_LEX_OK("->>", 2);
    ASSERT_TOK_KIND(0, TOK_ARROW);
    ASSERT_TOK_KIND(1, TOK_GT);
    ASSERT_LEX_OK("+++", 2);
    ASSERT_TOK_KIND(0, TOK_PLUS_PLUS);
    ASSERT_TOK_KIND(1, TOK_PLUS);
    ASSERT_LEX_OK("-->", 2);
    ASSERT_TOK_KIND(0, TOK_MINUS_MINUS);
    ASSERT_TOK_KIND(1, TOK_GT);
    ASSERT_LEX_OK(":::", 2);
    ASSERT_TOK_KIND(0, TOK_COLON_COLON);
    ASSERT_TOK_KIND(1, TOK_COLON);
    ASSERT_LEX_OK("<<<=", 2);
    ASSERT_TOK_KIND(0, TOK_SHL);
    ASSERT_TOK_KIND(1, TOK_LE);
    ASSERT_LEX_OK("!==", 2);
    ASSERT_TOK_KIND(0, TOK_NE);
    ASSERT_TOK_KIND(1, TOK_ASSIGN);
    ASSERT_LEX_OK("%%=", 2);
    ASSERT_TOK_KIND(0, TOK_PERCENT);
    ASSERT_TOK_KIND(1, TOK_PERCENT_ASSIGN);
})

TEST(slash_operators_beside_comments, {
    ASSERT_LEX_OK("a/=b//c\n/d", 5);
    ASSERT_TOK_KIND(1, TOK_SLASH_ASSIGN);
    ASSERT_TOK_KIND(3, TOK_SLASH);
    ASSERT_TOK_KIND(4, TOK_IDENT);
    ASSERT_LEX_OK("a / b", 3);
    ASSERT_TOK_KIND(1, TOK_SLASH);
    ASSERT_LEX_OK("*/", 2);
    ASSERT_TOK_KIND(0, TOK_STAR);
    ASSERT_TOK_KIND(1, TOK_SLASH);
})

TEST(at_lexes_as_the_span_suffix_token, {
    // `@` is a one-byte operator token, the span suffix (D2.10, D3.5).
    ASSERT_LEX_OK("@", 1);
    ASSERT_TOK_KIND(0, TOK_AT);
    ASSERT_TOK_TEXT(0, "@");
    ASSERT_TOK_POS(0, 1, 1);
    ASSERT_TOK_RANGE(0, 0, 1);
    ASSERT_LEX_OK("a\n  @ b", 3);
    ASSERT_TOK_KIND(1, TOK_AT);
    ASSERT_TOK_POS(1, 2, 3);
    ASSERT_TOK_RANGE(1, 4, 1);
})

TEST(at_in_a_type_lexes_beside_its_neighbours, {
    // A span of writable bytes, `u8 mut@ mut s` (D3.5, D5.3).
    ASSERT_LEX_OK("u8 mut@ mut s", 5);
    ASSERT_TOK_KIND(0, TOK_KW_U8);
    ASSERT_TOK_KIND(1, TOK_KW_MUT);
    ASSERT_TOK_KIND(2, TOK_AT);
    ASSERT_TOK_KIND(3, TOK_KW_MUT);
    ASSERT_TOK_KIND(4, TOK_IDENT);
    ASSERT_TOK_RANGE(2, 6, 1);
    // Reference suffixes may be adjacent and repeat: `node*@`, `u8@*`, `u8@@`.
    ASSERT_LEX_OK("node*@ u8@* u8@@", 9);
    ASSERT_TOK_KIND(1, TOK_STAR);
    ASSERT_TOK_KIND(2, TOK_AT);
    ASSERT_TOK_KIND(4, TOK_AT);
    ASSERT_TOK_KIND(5, TOK_STAR);
    ASSERT_TOK_KIND(7, TOK_AT);
    ASSERT_TOK_KIND(8, TOK_AT);
})

TEST(at_starts_no_longer_operator, {
    // No two-byte operator begins with `@`, so a run splits after each one.
    ASSERT_LEX_OK("@=", 2);
    ASSERT_TOK_KIND(0, TOK_AT);
    ASSERT_TOK_KIND(1, TOK_ASSIGN);
    ASSERT_LEX_OK("@@", 2);
    ASSERT_TOK_KIND(0, TOK_AT);
    ASSERT_TOK_KIND(1, TOK_AT);
    ASSERT_LEX_OK("a@b", 3);
    ASSERT_TOK_KIND(1, TOK_AT);
    ASSERT_TOK_KIND(2, TOK_IDENT);
    ASSERT_LEX_OK("x[..]@", 5);
    ASSERT_TOK_KIND(4, TOK_AT);
})

TEST(at_inside_a_literal_or_a_comment_is_an_ordinary_byte, {
    // A string keeps `@` as a byte of its text (D2.9).
    ASSERT_LEX_OK("\"a@b\"", 1);
    ASSERT_TOK_KIND(0, TOK_STRING);
    ASSERT_TOK_TEXT(0, "a@b");
    ASSERT_LEX_OK("\"@\"", 1);
    ASSERT_TOK_TEXT(0, "@");
    // A char literal holds its byte value, 0x40 (D2.7).
    ASSERT_LEX_OK("'@'", 1);
    ASSERT_TOK_KIND(0, TOK_CHAR);
    TEST_ASSERT_EQ_UINT64(tok(0)->ival, (uint64_t)0x40);
    ASSERT_TOK_RANGE(0, 0, 3);
    // A comment swallows it (D2.2).
    ASSERT_LEX_OK("a // @ @\n@", 2);
    ASSERT_TOK_KIND(0, TOK_IDENT);
    ASSERT_TOK_KIND(1, TOK_AT);
    ASSERT_TOK_POS(1, 2, 1);
})

TEST(a_statement_lexes_into_the_expected_sequence, {
    // An owned span of writable bytes from new(T, n) (D5.3, D17.2, D10.2).
    ASSERT_LEX_OK("u8 mut@ own mut s = new(u8, n +% 1);", 16);
    ASSERT_TOK_KIND(0, TOK_KW_U8);
    ASSERT_TOK_KIND(1, TOK_KW_MUT);
    ASSERT_TOK_KIND(2, TOK_AT);
    ASSERT_TOK_KIND(3, TOK_KW_OWN);
    ASSERT_TOK_KIND(4, TOK_KW_MUT);
    ASSERT_TOK_KIND(5, TOK_IDENT);
    ASSERT_TOK_KIND(6, TOK_ASSIGN);
    ASSERT_TOK_KIND(7, TOK_KW_NEW);
    ASSERT_TOK_KIND(8, TOK_LPAREN);
    ASSERT_TOK_KIND(9, TOK_KW_U8);
    ASSERT_TOK_KIND(10, TOK_COMMA);
    ASSERT_TOK_KIND(11, TOK_IDENT);
    ASSERT_TOK_KIND(12, TOK_PLUS_WRAP);
    ASSERT_TOK_KIND(13, TOK_INT);
    ASSERT_TOK_KIND(14, TOK_RPAREN);
    ASSERT_TOK_KIND(15, TOK_SEMI);
})

TEST(every_kind_of_token_in_one_file, {
    ASSERT_LEX_OK("fn f(x) { s = \"a\"; c = 'b'; n = 1; y = 1.5; }", 23);
    ASSERT_TOK_KIND(0, TOK_KW_FN);
    ASSERT_TOK_KIND(1, TOK_IDENT);
    ASSERT_TOK_KIND(2, TOK_LPAREN);
    ASSERT_TOK_KIND(8, TOK_STRING);
    ASSERT_TOK_KIND(12, TOK_CHAR);
    ASSERT_TOK_KIND(16, TOK_INT);
    ASSERT_TOK_KIND(20, TOK_FLOAT);
    ASSERT_TOK_KIND(22, TOK_RBRACE);
    ASSERT_TOK_KIND(23, TOK_EOF);
})

// ---- kinds and the token vector ------------------------------------------------

TEST(kind_names_of_the_literal_classes, {
    TEST_ASSERT_EQ_STR(tok_kind_name(TOK_EOF), "end of file");
    TEST_ASSERT_EQ_STR(tok_kind_name(TOK_IDENT), "identifier");
    TEST_ASSERT_EQ_STR(tok_kind_name(TOK_INT), "integer literal");
    TEST_ASSERT_EQ_STR(tok_kind_name(TOK_FLOAT), "float literal");
    TEST_ASSERT_EQ_STR(tok_kind_name(TOK_CHAR), "char literal");
    TEST_ASSERT_EQ_STR(tok_kind_name(TOK_STRING), "string literal");
    TEST_ASSERT_EQ_STR(tok_kind_name(TOK_KW_OWN), "own");
    TEST_ASSERT_EQ_STR(tok_kind_name(TOK_PLUS_WRAP_ASSIGN), "+%=");
    TEST_ASSERT_EQ_INT64((int64_t)TOK_COUNT, (int64_t)(6 + 41 + 54));
})

TEST(kind_names_are_distinct, {
    for (int a = 0; a < TOK_COUNT; a++) {
        for (int b = a + 1; b < TOK_COUNT; b++) {
            TEST_ASSERT_TRUE(strcmp(tok_kind_name((tok_kind_t)a), tok_kind_name((tok_kind_t)b)) !=
                             0);
        }
    }
})

TEST(tokvec_grows_and_keeps_its_tokens, {
    tokvec_t v;
    tokvec_init(&v);
    TEST_ASSERT_NULL(v.items);
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)0);
    for (uint64_t i = 0; i < 100; i++) {
        token_t t;
        TEST_UNUSED(memset(&t, 0, sizeof t));
        t.kind = TOK_INT;
        t.ival = i;
        t.off = i * 2;
        tokvec_push(&v, t);
    }
    TEST_ASSERT_EQ_UINT64(v.len, (uint64_t)100);
    TEST_ASSERT_TRUE(v.cap >= 100);
    for (uint64_t i = 0; i < 100; i++) {
        TEST_ASSERT_EQ_UINT64(v.items[i].ival, i);
        TEST_ASSERT_EQ_UINT64(v.items[i].off, i * 2);
    }
    tokvec_free(&v);
    TEST_ASSERT_NULL(v.items);
    TEST_ASSERT_EQ_UINT64(v.cap, (uint64_t)0);
})

TEST(tokvec_reserve_grows_once, {
    tokvec_t v;
    tokvec_init(&v);
    tokvec_reserve(&v, 1000);
    const token_t* items = v.items;
    TEST_ASSERT_NONNULL(items);
    TEST_ASSERT_TRUE(v.cap >= 1000);
    for (uint64_t i = 0; i < 1000; i++) {
        token_t t;
        TEST_UNUSED(memset(&t, 0, sizeof t));
        tokvec_push(&v, t);
    }
    TEST_ASSERT_TRUE(v.items == items);
    tokvec_free(&v);
})

TEST(lexing_a_long_file_grows_the_output, {
    sb_t src;
    sb_init(&src);
    for (int i = 0; i < 500; i++) {
        sb_append(&src, "x = x + 1;\n");
    }
    TEST_ASSERT_TRUE(lex(sb_cstr(&src)));
    TEST_ASSERT_EQ_UINT64(toks.len, (uint64_t)3001);
    ASSERT_TOK_POS(3000, 501, 1);
    ASSERT_TOK_KIND(2999, TOK_SEMI);
    ASSERT_TOK_POS(2999, 500, 10);
    ASSERT_TOK_RANGE(2999, 500 * 11 - 2, 1);
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)0);
    sb_free(&src);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("lexer", argc, argv);
    TEST_RUN(empty_file_is_only_eof);
    TEST_RUN(whitespace_only_file_is_only_eof);
    TEST_RUN(file_without_trailing_newline);
    TEST_RUN(eof_sits_after_the_trailing_newline);
    TEST_RUN(bom_is_skipped_and_takes_no_column);
    TEST_RUN(cr_is_whitespace);
    TEST_RUN(tab_counts_as_one_column);
    TEST_RUN(positions_advance_across_lines);
    TEST_RUN(non_ascii_outside_strings_and_comments_is_an_error);
    TEST_RUN(unexpected_printable_character_is_an_error);
    TEST_RUN(unexpected_control_byte_is_an_error);
    TEST_RUN(error_position_counts_lines_columns_and_tabs);
    TEST_RUN(error_keeps_the_tokens_before_it_and_stops);
    TEST_RUN(line_comment_runs_to_the_end_of_the_line);
    TEST_RUN(line_comment_at_the_end_of_the_file);
    TEST_RUN(block_comment_start_is_an_error);
    TEST_RUN(block_comment_start_is_an_error_wherever_it_stands);
    TEST_RUN(a_slash_before_a_separated_star_is_a_division);
    TEST_RUN(line_comments_may_hold_non_ascii_bytes);
    TEST_RUN(comment_markers_inside_a_line_comment_are_text);
    TEST_RUN(identifier_forms);
    TEST_RUN(identifier_text_is_a_view_into_the_source);
    TEST_RUN(identifiers_have_no_length_limit);
    TEST_RUN(every_keyword_lexes_as_its_kind);
    TEST_RUN(the_keyword_list_of_d2_4);
    TEST_RUN(own_is_a_keyword);
    TEST_RUN(a_word_extending_a_keyword_is_an_identifier);
    TEST_RUN(keywords_are_case_sensitive);
    TEST_RUN(universe_functions_are_not_keywords);
    TEST_RUN(reserved_words_are_errors);
    TEST_RUN(a_word_extending_a_reserved_word_is_an_identifier);
    TEST_RUN(every_operator_lexes_alone_as_its_kind);
    TEST_RUN(the_operator_list_of_d2_10);
    TEST_RUN(three_character_operators_and_their_prefixes);
    TEST_RUN(longest_match_without_spaces);
    TEST_RUN(runs_of_operator_characters_split_greedily);
    TEST_RUN(slash_operators_beside_comments);
    TEST_RUN(at_lexes_as_the_span_suffix_token);
    TEST_RUN(at_in_a_type_lexes_beside_its_neighbours);
    TEST_RUN(at_starts_no_longer_operator);
    TEST_RUN(at_inside_a_literal_or_a_comment_is_an_ordinary_byte);
    TEST_RUN(a_statement_lexes_into_the_expected_sequence);
    TEST_RUN(every_kind_of_token_in_one_file);
    TEST_RUN(kind_names_of_the_literal_classes);
    TEST_RUN(kind_names_are_distinct);
    TEST_RUN(tokvec_grows_and_keeps_its_tokens);
    TEST_RUN(tokvec_reserve_grows_once);
    TEST_RUN(lexing_a_long_file_grows_the_output);
    lex_done();
    TEST_EXIT();
}

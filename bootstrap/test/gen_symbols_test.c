// Tests ELF symbol spelling, uniqueness, and module-qualified names.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "common/gen_helpers.h"
#include "gen.h"
#include "str.h"

#include "common/test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// ---- the spelling of one name ---------------------------------------------

static sb_t name_buf;
static bool name_buf_ready = false;

// Returns `<prefix><name>` in a shared buffer.
// The next call invalidates the returned spelling.
static const char* spelled(const char* prefix, const char* name, bool always) {
    if (!name_buf_ready) {
        sb_init(&name_buf);
        name_buf_ready = true;
    }
    sb_clear(&name_buf);
    gen_append_name(&name_buf, prefix, str_from_cstr(name), always);
    return sb_cstr(&name_buf);
}

TEST(a_name_is_quoted_only_when_llvm_needs_it, {
    // LLVM's unquoted identifiers are `[-a-zA-Z$._][-a-zA-Z$._0-9]*`, which
    // every module path satisfies, since its segments are identifiers joined
    // with dots.
    TEST_ASSERT_EQ_STR(spelled("struct.", "util.chars.pair", false), "struct.util.chars.pair");
    TEST_ASSERT_EQ_STR(spelled("", "strlen", false), "strlen");
    TEST_ASSERT_EQ_STR(spelled(".enum.", "main.color", false), ".enum.main.color");
    // A digit may not begin one, which only an entry base name can.
    TEST_ASSERT_EQ_STR(spelled("", "007_case.main", false), "\"007_case.main\"");
    TEST_ASSERT_EQ_STR(spelled("struct.", "007_case.point", false), "struct.007_case.point");
    // Every fort symbol is quoted whatever its bytes.
    TEST_ASSERT_EQ_STR(spelled("", "main.add", true), "\"main.add\"");
})

TEST(a_name_escapes_what_a_quoted_name_cannot_hold, {
    // Quoted LLVM names escape quotes, backslashes, and non-printable bytes.
    // LLVM decodes each escape back to the original ELF symbol byte.
    TEST_ASSERT_EQ_STR(spelled("", "a\"b.main", true), "\"a\\22b.main\"");
    TEST_ASSERT_EQ_STR(spelled("", "a\\b.main", true), "\"a\\5Cb.main\"");
    TEST_ASSERT_EQ_STR(spelled("",
                               "a\x01"
                               "b.main",
                               false),
                       "\"a\\01b.main\"");
    TEST_ASSERT_EQ_STR(spelled("struct.",
                               "a\x7F"
                               "b.point",
                               false),
                       "\"struct.a\\7Fb.point\"");
    // A printable byte outside the identifier set needs the quotes and
    // nothing else.
    TEST_ASSERT_EQ_STR(spelled("", "a b.main", false), "\"a b.main\"");
})

// ---- the module path in the symbol -------------------------------------------------

// The module `util.chars`, reached by `import util.chars;` from the entry file
// beside its directory.
static const char CHARS_SOURCE[] = "struct pair {\n    i32 x;\n    i32 y;\n}\n"
                                   "enum color {\n    red,\n    green,\n}\n"
                                   "fn twice(i32 n) i32 { return n +% n; }\n"
                                   "fn pick() color { return color.green; }\n";

static const char CHARS_MAIN[] = "import util.chars;\n"
                                 "fn main() i32 {\n"
                                 "    chars.pair p = {1, 2};\n"
                                 "    println(chars.twice(p.x), chars.pick());\n"
                                 "    return 0;\n}\n";

TEST(a_nested_module_path_joins_every_segment_with_a_dot, {
    TEST_ASSERT_TRUE(emit_two("main.ft", CHARS_MAIN, "util/chars.ft", CHARS_SOURCE));
    // `twice` in the module `util.chars` is `util.chars.twice`: the module
    // path, a dot and the declaration name, quoted because it is dotted.
    TEST_ASSERT_EQ_STR(found("define dso_local i32 @\"util.chars.twice\"(i32 %n.in) #0 {"),
                       "define dso_local i32 @\"util.chars.twice\"(i32 %n.in) #0 {");
    TEST_ASSERT_EQ_STR(found("%struct.util.chars.pair = type { i32, i32 }"),
                       "%struct.util.chars.pair = type { i32, i32 }");
    TEST_ASSERT_EQ_STR(found("@.enum.util.chars.color = private"),
                       "@.enum.util.chars.color = private");
    // The file path separator never reaches the symbol: the module path is
    // `util.chars`, not `util/chars`, and the mangler copies it across
    // unchanged.
    TEST_ASSERT_EQ_STR(absent("util/chars"), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_entry_base_name_that_is_no_identifier_reaches_its_symbols, {
    TEST_ASSERT_TRUE(emit_as("007_case.ft",
                             "struct point {\n    i32 x;\n}\n"
                             "fn main() i32 {\n    point p = {7};\n"
                             "    println(p.x);\n    return 0;\n}\n"));
    // The entry module's path is its base name, which need not be an
    // identifier; quoting the dotted symbol is what carries it.
    TEST_ASSERT_EQ_STR(found("define dso_local i32 @\"007_case.main\"() #0 {"),
                       "define dso_local i32 @\"007_case.main\"() #0 {");
    // A named type starts with `struct.`, so a digit after it needs no
    // quotes: LLVM's unquoted names are `[-a-zA-Z$._][-a-zA-Z$._0-9]*`.
    TEST_ASSERT_EQ_STR(found("%struct.007_case.point = type { i32 }"),
                       "%struct.007_case.point = type { i32 }");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_byte_a_quoted_name_cannot_hold_is_written_as_a_hex_escape, {
    TEST_ASSERT_TRUE(emit_as("a\"b.ft",
                             "struct point {\n    i32 x;\n}\n"
                             "enum color {\n    red,\n}\n"
                             "fn main() i32 {\n    point p = {7};\n"
                             "    println(p.x, color.red);\n    return 0;\n}\n"));
    // An entry base name bars `.` and nothing else, so a `\"` reaches the symbol. LLVM reads `\\22`
    // back to that byte, so the ELF symbol is `a\"b.main` and the quoting stays spelling only.
    TEST_ASSERT_EQ_STR(found("define dso_local i32 @\"a\\22b.main\"() #0 {"),
                       "define dso_local i32 @\"a\\22b.main\"() #0 {");
    // The named type and the enum table carry the same name and so need the
    // quotes the module path never does.
    TEST_ASSERT_EQ_STR(found("%\"struct.a\\22b.point\" = type { i32 }"),
                       "%\"struct.a\\22b.point\" = type { i32 }");
    TEST_ASSERT_EQ_STR(found("@\".enum.a\\22b.color\" = private"),
                       "@\".enum.a\\22b.color\" = private");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_colon_in_the_entry_base_name_reaches_the_symbol, {
    TEST_ASSERT_TRUE(emit_as("my:app.ft",
                             "struct point {\n    i32 x;\n}\n"
                             "enum color {\n    red,\n}\n"
                             "fn main() i32 {\n    point p = {7};\n"
                             "    println(p.x, color.red);\n    return 0;\n}\n"));
    // The mangler copies the colon into the symbol.
    // A module path cannot contain it because each path segment is an identifier.
    TEST_ASSERT_EQ_STR(found("define dso_local i32 @\"my:app.main\"() #0 {"),
                       "define dso_local i32 @\"my:app.main\"() #0 {");
    TEST_ASSERT_EQ_STR(absent("@\"myapp.main\""), "absent");
    // `:` is outside LLVM's unquoted identifiers, so the named type and the enum table are quoted
    // where a dotted path leaves them bare.
    TEST_ASSERT_EQ_STR(found("%\"struct.my:app.point\" = type { i32 }"),
                       "%\"struct.my:app.point\" = type { i32 }");
    TEST_ASSERT_EQ_STR(found("@\".enum.my:app.color\" = private"),
                       "@\".enum.my:app.color\" = private");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_backslash_reaches_the_symbol_through_its_own_escape, {
    TEST_ASSERT_TRUE(emit_as("a\\b.ft", "fn main() i32 {\n    println(1);\n    return 0;\n}\n"));
    // The other byte a quoted name cannot hold.
    TEST_ASSERT_EQ_STR(found("define dso_local i32 @\"a\\5Cb.main\"() #0 {"),
                       "define dso_local i32 @\"a\\5Cb.main\"() #0 {");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_control_byte_in_the_entry_base_name_reaches_the_symbol, {
    TEST_ASSERT_TRUE(emit_as("a\x01"
                             "b.ft",
                             "fn main() i32 {\n    println(1);\n    return 0;\n}\n"));
    // An entry base name bars `.` alone, so a control byte is a legal entry base name and its
    // module path holds it. `\\XX` is how a quoted name does.
    TEST_ASSERT_EQ_STR(found("define dso_local i32 @\"a\\01b.main\"() #0 {"),
                       "define dso_local i32 @\"a\\01b.main\"() #0 {");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(a_c_name_is_never_quoted, {
    TEST_ASSERT_TRUE(emit("extern fn strlen(char* s) u64;\n"
                          "fn main() i32 {\n    string s = \"ab\";\n"
                          "    println(strlen(s.ptr));\n    return 0;\n}\n"));
    // An `extern` name is unmangled, and an identifier needs no quotes.
    TEST_ASSERT_EQ_STR(found("declare i64 @strlen(ptr) nobuiltin\n"),
                       "declare i64 @strlen(ptr) nobuiltin\n");
    TEST_ASSERT_EQ_STR(absent("@\"strlen\""), "absent");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// ---- one ELF symbol, one definition ---------------------------------------

TEST(an_extern_declaring_the_program_entry_point_is_refused, {
    TEST_ASSERT_FALSE(emit("extern fn fort_entry(void* args) void;\n"
                           "fn main() i32 {\n    fort_entry(null);\n    return 0;\n}\n"));
    // `fort_entry` is reserved: the compiler emits its definition. A declaration of that name never
    // reaches the emitter, where it would be a `declare` beside a `define` of one ELF symbol. And,
    // since nothing could check the declared signature against that definition, a call through the
    // wrong type.
    TEST_ASSERT_NONNULL(strstr(gen_said(), "'fort_entry' is reserved: the compiler emits it"));
})

TEST(the_program_entry_point_is_one_entity_of_the_module, {
    TEST_ASSERT_TRUE(emit("fn main() i32 {\n    println(1);\n    return 0;\n}\n"));
    // One ELF symbol maps to one IR entity.
    // `fort_entry` is the only undotted name that a fort program defines.
    TEST_ASSERT_EQ_UINT64(occurrences("define dso_local i32 @fort_entry"), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(occurrences("declare void @fort_entry"), (uint64_t)0);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// A module imported twice over, under its own name and under another, whose
// bindings name the same entities.
static const char TWICE_MAIN[] = "import util;\n"
                                 "import util as tools;\n"
                                 "import util.twice;\n"
                                 "fn main() i32 {\n"
                                 "    println(util.twice(1), tools.twice(2), twice(3));\n"
                                 "    return 0;\n}\n";

TEST(a_module_imported_under_several_names_is_emitted_once, {
    TEST_ASSERT_TRUE(
        emit_two("main.ft", TWICE_MAIN, "util.ft", "fn twice(i32 n) i32 { return n +% n; }\n"));
    // One module is one set of definitions however many names reach it, and
    // every call names the one symbol.
    TEST_ASSERT_EQ_UINT64(occurrences("define dso_local i32 @\"util.twice\""), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(occurrences("call i32 @\"util.twice\""), (uint64_t)3);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// A diamond: the entry file imports two modules that both import a third,
// which the loader reads once and orders before both.
static const char* const DIAMOND_NAMES[] = {"main.ft", "left.ft", "right.ft", "leaf.ft"};
static const char DIAMOND_ENTRY[] = "import left;\nimport right;\n"
                                    "fn main() i32 {\n"
                                    "    println(left.up() +% right.down());\n"
                                    "    return 0;\n}\n";
static const char* const DIAMOND_TEXTS[] = {
    DIAMOND_ENTRY,
    "import leaf;\nfn up() i32 { return leaf.value(); }\n",
    "import leaf as base;\nfn down() i32 { return base.value(); }\n",
    "fn value() i32 { return 5; }\n",
};

TEST(a_module_two_importers_share_is_emitted_once, {
    TEST_ASSERT_TRUE(emit_files(DIAMOND_NAMES, DIAMOND_TEXTS, (uint64_t)4));
    // One file is one module however many importers reach it, so its
    // definitions are emitted once.
    TEST_ASSERT_EQ_UINT64(occurrences("define dso_local i32 @\"leaf.value\""), (uint64_t)1);
    // Every module stands after the ones it imports.
    TEST_ASSERT_TRUE(before("@\"leaf.value\"", "define dso_local i32 @\"left.up\""));
    TEST_ASSERT_TRUE(before("define dso_local i32 @\"right.down\"", "@\"main.main\""));
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(main_in_another_module_is_an_ordinary_function, {
    TEST_ASSERT_TRUE(emit_two("main.ft",
                              "import other;\n"
                              "fn main() i32 {\n    println(other.main());\n    return 0;\n}\n",
                              "other.ft",
                              "fn main() i32 { return 41; }\n"));
    // `main` in a non-entry module is an ordinary function. Its symbol includes its path, so the 2
    // names do not collide.
    TEST_ASSERT_EQ_STR(found("define dso_local i32 @\"other.main\"() #0 {"),
                       "define dso_local i32 @\"other.main\"() #0 {");
    // `fort_entry` calls the entry module's `main` alone.
    TEST_ASSERT_EQ_UINT64(occurrences("call i32 @\"main.main\"()"), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(occurrences("call i32 @\"other.main\"()"), (uint64_t)1);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(an_enum_two_modules_print_has_one_table, {
    TEST_ASSERT_TRUE(emit_two("main.ft",
                              "import util;\n"
                              "fn main() i32 {\n    println(util.color.red, util.show());\n"
                              "    return 0;\n}\n",
                              "util.ft",
                              "enum color {\n    red,\n    green,\n}\n"
                              "fn show() color { return color.green; }\n"));
    // The table belongs to the declaration, not the printing module. One enum has one
    // `@.enum.<path.name>`.
    TEST_ASSERT_EQ_UINT64(occurrences("@.enum.util.color = private"), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(occurrences("ptr @.enum.util.color"), (uint64_t)2);
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

TEST(two_modules_declaring_one_name_keep_two_named_types, {
    TEST_ASSERT_TRUE(emit_two("main.ft",
                              "import util;\n"
                              "struct point {\n    i32 x;\n}\n"
                              "fn main() i32 {\n    point p = {1};\n"
                              "    println(p.x +% util.origin().x);\n    return 0;\n}\n",
                              "util.ft",
                              "struct point {\n    i32 x;\n}\n"
                              "fn origin() point { return point{2}; }\n"));
    // Structs are nominal and every symbol carries its module path, so two
    // declarations of one name are two named types.
    TEST_ASSERT_EQ_STR(found("%struct.util.point = type { i32 }"),
                       "%struct.util.point = type { i32 }");
    TEST_ASSERT_EQ_STR(found("%struct.main.point = type { i32 }"),
                       "%struct.main.point = type { i32 }");
    TEST_ASSERT_EQ_STR(verified(), "verified");
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("gen_symbols", argc, argv);
    TEST_RUN(a_name_is_quoted_only_when_llvm_needs_it);
    TEST_RUN(a_name_escapes_what_a_quoted_name_cannot_hold);
    TEST_RUN(a_nested_module_path_joins_every_segment_with_a_dot);
    TEST_RUN(an_entry_base_name_that_is_no_identifier_reaches_its_symbols);
    TEST_RUN(a_byte_a_quoted_name_cannot_hold_is_written_as_a_hex_escape);
    TEST_RUN(a_colon_in_the_entry_base_name_reaches_the_symbol);
    TEST_RUN(a_backslash_reaches_the_symbol_through_its_own_escape);
    TEST_RUN(a_control_byte_in_the_entry_base_name_reaches_the_symbol);
    TEST_RUN(a_c_name_is_never_quoted);
    TEST_RUN(an_extern_declaring_the_program_entry_point_is_refused);
    TEST_RUN(the_program_entry_point_is_one_entity_of_the_module);
    TEST_RUN(a_module_imported_under_several_names_is_emitted_once);
    TEST_RUN(a_module_two_importers_share_is_emitted_once);
    TEST_RUN(main_in_another_module_is_an_ordinary_function);
    TEST_RUN(an_enum_two_modules_print_has_one_table);
    TEST_RUN(two_modules_declaring_one_name_keep_two_named_types);
    gen_done();
    if (name_buf_ready) {
        sb_free(&name_buf);
    }
    TEST_EXIT();
}

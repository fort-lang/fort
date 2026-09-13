// Unit tests of the identifier index (toolchain.md 9.1): the walk over the
// trees a checked closure leaves, the record it makes of every identifier
// occurrence the checker resolved, and the order the records come out in.
// D20.3
//
// A record is compared as one line -- range, kind, name, type, whether it is
// the declaration and where the declaration is -- so that a test states the
// whole record and not a field of it; index_helpers.h spells it. The sandbox
// and the checker are modules_helpers.h and check_helpers.h.
#include "index.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "check_helpers.h"
#include "fork.h"
#include "index_helpers.h"
#include "modules_helpers.h"
#include "str.h"
#include "sym.h"

#include "test.h"

// The stderr a forked child may write, which is one diagnostic line.
enum { ERR_CAP = 512 };

// NOLINTBEGIN(readability-magic-numbers) the line and column of each
// occurrence are the test data: they are read off the fixture above them.

// A module with one declaration of every kind a module can hold, imported by
// the entry module below.
// D20.3
static const char UTIL_SOURCE[] = "i32 LIMIT = 4;\n"            // line 1
                                  "i32 mut seen = 0;\n"         // line 2
                                  "struct vec {\n"              // line 3
                                  "    i32 x;\n"                // line 4
                                  "}\n"                         // line 5
                                  "enum color {\n"              // line 6
                                  "    red,\n"                  // line 7
                                  "}\n"                         // line 8
                                  "extern fn i32 abs(i32 n);\n" // line 9
                                  "fn i32 twice(i32 n) {\n"     // line 10
                                  "    return n + n;\n"         // line 11
                                  "}\n";                        // line 12

// The entry module: an import, a local, a parameter, a builtin and a use of
// every declaration of `util`.
static const char MAIN_SOURCE[] = "import util;\n"                            // line 1
                                  "fn i32 main() {\n"                         // line 2
                                  "    util.vec v = {util.LIMIT};\n"          // line 3
                                  "    println(util.twice(v.x));\n"           // line 4
                                  "    println(util.color.red, util.seen);\n" // line 5
                                  "    return 0;\n"                           // line 6
                                  "}\n";                                      // line 7

// Loads and checks the two modules above, then indexes the closure.
static bool index_two_modules(void) {
    begin();
    add("util.ft", UTIL_SOURCE);
    add("main.ft", MAIN_SOURCE);
    const bool ok = check_entry("main.ft");
    index_closure();
    return ok;
}

// ---- the record of one occurrence ------------------------------------------------
// D20.3

TEST(a_declaration_is_recorded_at_its_own_name, {
    TEST_ASSERT_TRUE(index_src("fn i32 main() { return 0; }\n"));
    // The name token alone, never the `fn` the construct starts at.
    // D20.4
    TEST_ASSERT_EQ_STR(text_at(1, 8), "main.ft:1:8-1:12 fn main 'fn i32()' decl main.ft:1:8");
    TEST_ASSERT_EQ_UINT64(index_count(&ix), (uint64_t)1);
})

TEST(a_use_carries_the_declarations_range, {
    // A module under inspection need not define main, which is the mode the
    // index runs in.
    // D20.1
    want_main = false;
    TEST_ASSERT_TRUE(index_src("fn i32 add(i32 a) {\n    return a;\n}\n"));
    TEST_ASSERT_EQ_STR(text_at(1, 16), "main.ft:1:16-1:17 parameter a 'i32' decl main.ft:1:16");
    // The use says where the declaration is and repeats its type, so a client
    // answers hover and go-to-definition from the record alone.
    // D20.3
    TEST_ASSERT_EQ_STR(text_at(2, 12), "main.ft:2:12-2:13 parameter a 'i32' use main.ft:1:16");
})

TEST(a_local_is_a_local_and_a_parameter_a_parameter, {
    TEST_ASSERT_TRUE(index_src("fn i32 main() {\n    i32 n = 1;\n    return n;\n}\n"));
    TEST_ASSERT_EQ_STR(text_at(2, 9), "main.ft:2:9-2:10 local n 'i32' decl main.ft:2:9");
    TEST_ASSERT_EQ_STR(text_at(3, 12), "main.ft:3:12-3:13 local n 'i32' use main.ft:2:9");
})

TEST(a_locals_type_carries_its_level_0_mutability, {
    TEST_ASSERT_TRUE(index_src("fn i32 main() {\n    i32 mut n = 1;\n    return n;\n}\n"));
    // The type is what a declaration of it spells, `mut` included.
    // D5.2
    TEST_ASSERT_EQ_STR(text_at(2, 13), "main.ft:2:13-2:14 local n 'i32 mut' decl main.ft:2:13");
})

TEST(a_builtin_has_no_declaration_to_jump_to, {
    TEST_ASSERT_TRUE(index_src("fn i32 main() {\n    println(1);\n    return 0;\n}\n"));
    // No source declares a universe function, so its record has no declaration
    // range at all.
    // D12.2, D20.3
    TEST_ASSERT_EQ_STR(text_at(2, 5), "main.ft:2:5-2:12 builtin println '' use null");
    TEST_ASSERT_FALSE(entry_at(2, 5)->has_decl);
})

TEST(a_struct_name_and_an_enum_name_have_no_type, {
    TEST_ASSERT_TRUE(index_two_modules());
    // A struct or enum name denotes a type rather than having one, so its
    // record's type is empty.
    // D20.3
    TEST_ASSERT_EQ_STR(text_in("util.ft", 3, 8), "util.ft:3:8-3:11 struct vec '' decl util.ft:3:8");
    TEST_ASSERT_EQ_STR(text_in("util.ft", 6, 6), "util.ft:6:6-6:11 enum color '' decl util.ft:6:6");
})

TEST(a_field_and_an_enum_member_carry_their_types, {
    TEST_ASSERT_TRUE(index_two_modules());
    TEST_ASSERT_EQ_STR(text_in("util.ft", 4, 9), "util.ft:4:9-4:10 field x 'i32' decl util.ft:4:9");
    // A member's type is its enum.
    // D3.9
    TEST_ASSERT_EQ_STR(text_in("util.ft", 7, 5),
                       "util.ft:7:5-7:8 enum member red 'color' decl util.ft:7:5");
})

TEST(a_constant_and_a_global_are_told_apart, {
    TEST_ASSERT_TRUE(index_two_modules());
    // A module-level immutable is a constant and a `mut` one a global.
    // D7.10
    TEST_ASSERT_EQ_STR(text_in("util.ft", 1, 5),
                       "util.ft:1:5-1:10 constant LIMIT 'i32' decl util.ft:1:5");
    TEST_ASSERT_EQ_STR(text_in("util.ft", 2, 9),
                       "util.ft:2:9-2:13 global seen 'i32 mut' decl util.ft:2:9");
})

TEST(an_extern_function_is_an_extern_fn, {
    TEST_ASSERT_TRUE(index_two_modules());
    TEST_ASSERT_EQ_STR(text_in("util.ft", 9, 15),
                       "util.ft:9:15-9:18 extern fn abs 'fn i32(i32)' decl util.ft:9:15");
})

TEST(a_qualified_use_records_the_module_and_the_declaration, {
    TEST_ASSERT_TRUE(index_two_modules());
    // A module has no name token of its own, so go-to-definition on the
    // qualifier opens the file at 1:1.
    // D9.1, D20.3
    TEST_ASSERT_EQ_STR(text_in("main.ft", 4, 13),
                       "main.ft:4:13-4:17 module util '' use util.ft:1:1");
    TEST_ASSERT_EQ_STR(text_in("main.ft", 4, 18),
                       "main.ft:4:18-4:23 fn twice 'fn i32(i32)' use util.ft:10:8");
})

TEST(the_last_segment_of_an_import_path_is_the_module, {
    TEST_ASSERT_TRUE(index_two_modules());
    TEST_ASSERT_EQ_STR(text_in("main.ft", 1, 8), "main.ft:1:8-1:12 module util '' use util.ft:1:1");
})

TEST(a_field_access_records_the_field_of_the_struct, {
    TEST_ASSERT_TRUE(index_two_modules());
    TEST_ASSERT_EQ_STR(text_in("main.ft", 4, 26),
                       "main.ft:4:26-4:27 field x 'i32' use util.ft:4:9");
})

TEST(an_enum_member_is_reached_through_its_enum, {
    TEST_ASSERT_TRUE(index_two_modules());
    TEST_ASSERT_EQ_STR(text_in("main.ft", 5, 18),
                       "main.ft:5:18-5:23 enum color '' use util.ft:6:6");
    TEST_ASSERT_EQ_STR(text_in("main.ft", 5, 24),
                       "main.ft:5:24-5:27 enum member red 'color' use util.ft:7:5");
})

// ---- the order of the records ----------------------------------------------------
// D20.3

TEST(an_imported_module_comes_before_its_importer, {
    TEST_ASSERT_TRUE(index_two_modules());
    // The dependency order is the file order of the index.
    // D9.10
    TEST_ASSERT_EQ_STR(rel_file(index_at(&ix, 0)->loc.file), "util.ft");
    const index_entry_t* last = index_at(&ix, index_count(&ix) - 1);
    TEST_ASSERT_EQ_STR(rel_file(last->loc.file), "main.ft");
})

TEST(the_records_of_a_file_are_in_position_order, {
    TEST_ASSERT_TRUE(index_src("fn i32 main() {\n    i32 n = 1;\n    return n;\n}\n"));
    TEST_ASSERT_EQ_STR(file_text("main.ft"), "main@1:8\nn@2:9\nn@3:12");
})

TEST(an_operand_comes_before_the_field_it_is_read_through, {
    // The walk reaches the `.` node before its operand, so the sort is what
    // puts `v` before `x`.
    // D20.3
    TEST_ASSERT_TRUE(index_src("struct p {\n    i32 x;\n}\n"
                               "fn i32 main() {\n    p v = {1};\n    return v.x;\n}\n"));
    TEST_ASSERT_EQ_STR(file_text("main.ft"),
                       "p@1:8\nx@2:9\nmain@4:8\np@5:5\nv@5:7\nv@6:12\nx@6:14");
})

TEST(the_records_of_one_line_are_in_column_order, {
    TEST_ASSERT_TRUE(index_two_modules());
    TEST_ASSERT_NONNULL(strstr(file_text("main.ft"), "util@5:13\ncolor@5:18\nred@5:24"));
})

// ---- what carries no record ------------------------------------------------------
// D20.3

TEST(a_directory_segment_of_an_import_path_carries_no_record, {
    begin();
    add("sub/util.ft", "fn i32 twice(i32 n) { return n + n; }\n");
    add("main.ft", "import sub.util;\nfn i32 main() { return util.twice(1); }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    index_closure();
    // `sub` names a search directory and not a module, so only the last
    // segment of the path denotes anything.
    // D9.2, D9.3, D20.3
    TEST_ASSERT_EQ_STR(text_in("main.ft", 1, 8), "<none>");
    TEST_ASSERT_EQ_STR(text_in("main.ft", 1, 12),
                       "main.ft:1:12-1:16 module util '' use sub/util.ft:1:1");
})

TEST(an_alias_of_an_import_that_failed_carries_no_record, {
    begin();
    add("main.ft", "import nothere as n;\nfn i32 main() { return 0; }\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    index_closure();
    // The import bound nothing, so neither the path segment nor the alias
    // denotes anything and the file indexes only what resolved.
    // D20.3
    TEST_ASSERT_EQ_STR(file_text("main.ft"), "main@2:8");
})

TEST(the_module_node_itself_is_not_an_occurrence, {
    TEST_ASSERT_TRUE(index_src("fn i32 main() { return 0; }\n"));
    // The module's own record hangs on the module node, which has no name
    // token, so nothing is recorded for it.
    // D20.3
    TEST_ASSERT_EQ_UINT64(index_count(&ix), (uint64_t)1);
    TEST_ASSERT_EQ_STR(text_at(1, 1), "<none>");
})

TEST(a_name_that_did_not_resolve_carries_no_record, {
    TEST_ASSERT_FALSE(index_src("fn i32 main() {\n    return nope;\n}\n"));
    TEST_ASSERT_TRUE(said("unknown name 'nope'"));
    // The checker resolved nothing for it, so the index says nothing about it
    // and everything that did resolve is still there.
    // D20.3
    TEST_ASSERT_EQ_STR(text_at(2, 12), "<none>");
    TEST_ASSERT_EQ_STR(text_at(1, 8), "main.ft:1:8-1:12 fn main 'fn i32()' decl main.ft:1:8");
})

TEST(a_declaration_that_failed_to_check_has_no_type_to_show, {
    TEST_ASSERT_FALSE(index_src("fn i32 main() {\n    nope n = 1;\n    return 0;\n}\n"));
    // Its type is the poison, which says nothing a reader wants, so the record
    // carries no type at all and a client renders it as unknown, which the
    // empty spelling of a name that has no value type would not say.
    // D14.2, D20.3
    TEST_ASSERT_EQ_STR(text_at(2, 10), "main.ft:2:10-2:11 local n null decl main.ft:2:10");
    TEST_ASSERT_FALSE(entry_at(2, 10)->has_type);
})

TEST(a_module_that_did_not_parse_contributes_nothing, {
    begin();
    add("util.ft", "fn i32 twice(i32 n) { return n + n\n");
    add("main.ft", "import util;\nfn i32 main() { return 0; }\n");
    TEST_UNUSED(check_entry("main.ft"));
    index_closure();
    // It was never checked, so it has no annotation to read.
    // D14.2
    TEST_ASSERT_EQ_UINT64(file_count("util.ft"), (uint64_t)0);
})

TEST(a_module_whose_import_failed_is_indexed_all_the_same, {
    begin();
    add("main.ft", "import nothere;\nfn i32 twice(i32 n) {\n    return n + n;\n}\n");
    TEST_ASSERT_FALSE(check_entry("main.ft"));
    index_closure();
    // The loader never ordered it, but the checker checked it, so the file a
    // reader is editing is indexed whatever its imports do.
    // D14.2, D20.1
    TEST_ASSERT_EQ_STR(file_text("main.ft"), "twice@2:8\nn@2:18\nn@3:12\nn@3:16");
})

// ---- the names of the other constructs -------------------------------------------
// D20.3

TEST(an_alias_import_records_the_declaration_and_the_alias, {
    begin();
    add("util.ft", "fn i32 twice(i32 n) { return n + n; }\n");
    add("main.ft", "import util.twice as double;\nfn i32 main() { return double(2); }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    index_closure();
    // The path's last segment is the declaration it binds and the alias
    // denotes the same declaration under its own name.
    // D9.3
    TEST_ASSERT_EQ_STR(text_in("main.ft", 1, 13),
                       "main.ft:1:13-1:18 fn twice 'fn i32(i32)' use util.ft:1:8");
    // A record names the identifier as it is spelled there, and the alias
    // declares that name in this module while pointing at the declaration it
    // binds, so a rename starts here and a jump lands there.
    // D9.3, D20.3
    TEST_ASSERT_EQ_STR(text_in("main.ft", 1, 22),
                       "main.ft:1:22-1:28 fn double 'fn i32(i32)' decl util.ft:1:8");
    TEST_ASSERT_EQ_STR(text_in("main.ft", 2, 24),
                       "main.ft:2:24-2:30 fn double 'fn i32(i32)' use util.ft:1:8");
})

TEST(a_whole_module_alias_declares_its_name_here, {
    begin();
    add("util.ft", "fn i32 twice(i32 n) { return n + n; }\n");
    add("main.ft", "import util as u;\nfn i32 main() { return u.twice(1); }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    index_closure();
    // `import util as u;` names the module `u` in this file, so the alias
    // declares that name while the path segment stays a use of the module and
    // both point at the module's file.
    // D9.3, D20.3
    TEST_ASSERT_EQ_STR(text_in("main.ft", 1, 8), "main.ft:1:8-1:12 module util '' use util.ft:1:1");
    TEST_ASSERT_EQ_STR(text_in("main.ft", 1, 16), "main.ft:1:16-1:17 module u '' decl util.ft:1:1");
    TEST_ASSERT_EQ_STR(text_in("main.ft", 2, 24), "main.ft:2:24-2:25 module u '' use util.ft:1:1");
})

TEST(one_module_imported_under_two_names_declares_both, {
    begin();
    add("util.ft", "fn i32 twice(i32 n) { return n + n; }\n");
    add("main.ft", "import util;\nimport util as u;\nfn i32 main() { return u.twice(1); }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    index_closure();
    // A module may be imported under several names (module-system.md 3); the
    // alias is a declaration here and the plain import is not, since it
    // introduces the name the module already has.
    // D20.3
    TEST_ASSERT_EQ_STR(text_in("main.ft", 1, 8), "main.ft:1:8-1:12 module util '' use util.ft:1:1");
    TEST_ASSERT_EQ_STR(text_in("main.ft", 2, 16), "main.ft:2:16-2:17 module u '' decl util.ft:1:1");
})

TEST(an_item_list_records_the_module_and_every_item, {
    begin();
    add("util.ft", "fn i32 twice(i32 n) { return n + n; }\ni32 LIMIT = 4;\n");
    add("main.ft", "import util.{twice, LIMIT as CAP};\nfn i32 main() { return twice(CAP); }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    index_closure();
    // The path of an item list names the module every item comes from.
    // D9.3
    TEST_ASSERT_EQ_STR(text_in("main.ft", 1, 8), "main.ft:1:8-1:12 module util '' use util.ft:1:1");
    TEST_ASSERT_EQ_STR(text_in("main.ft", 1, 14),
                       "main.ft:1:14-1:19 fn twice 'fn i32(i32)' use util.ft:1:8");
    TEST_ASSERT_EQ_STR(text_in("main.ft", 1, 21),
                       "main.ft:1:21-1:26 constant LIMIT 'i32' use util.ft:2:5");
    // The alias of an item declares its name here too; the item without one
    // introduces the name its declaration already has.
    // D9.3
    TEST_ASSERT_EQ_STR(text_in("main.ft", 1, 30),
                       "main.ft:1:30-1:33 constant CAP 'i32' decl util.ft:2:5");
})

TEST(a_designator_records_the_field_it_names, {
    want_main = false;
    TEST_ASSERT_TRUE(index_src("struct p {\n    i32 x;\n}\n"
                               "fn p make() {\n    return p{.x = 1};\n}\n"));
    // `.x = v` names the field of the struct being built.
    // D6.5
    TEST_ASSERT_EQ_STR(text_at(5, 15), "main.ft:5:15-5:16 field x 'i32' use main.ft:2:9");
})

TEST(a_shadowing_local_hides_the_module_level_name, {
    TEST_ASSERT_TRUE(index_src("i32 n = 1;\nfn i32 main() {\n    i32 n = 2;\n    return n;\n}\n"));
    // A local may shadow a module-level name, which is then inaccessible in
    // its scope, so the use denotes the local.
    // D7.9
    TEST_ASSERT_EQ_STR(text_at(4, 12), "main.ft:4:12-4:13 local n 'i32' use main.ft:3:9");
})

TEST(a_type_name_in_sizeof_a_cast_and_new_is_recorded, {
    want_main = false;
    TEST_ASSERT_TRUE(index_src("struct p {\n    i32 x;\n}\n"
                               "fn u64 f() {\n    p mut* own q = new(p);\n"
                               "    del(q);\n    return sizeof(p);\n}\n"));
    TEST_ASSERT_EQ_STR(text_at(5, 5), "main.ft:5:5-5:6 struct p '' use main.ft:1:8");
    TEST_ASSERT_EQ_STR(text_at(5, 24), "main.ft:5:24-5:25 struct p '' use main.ft:1:8");
    TEST_ASSERT_EQ_STR(text_at(7, 19), "main.ft:7:19-7:20 struct p '' use main.ft:1:8");
})

TEST(the_variable_of_a_range_loop_is_a_local, {
    TEST_ASSERT_TRUE(index_src("fn i32 main() {\n    i32[2] a = {1, 2};\n"
                               "    i32 mut s = 0;\n    for (i32 v : a) {\n        s += v;\n"
                               "    }\n    return s;\n}\n"));
    // The element of a range loop is declared by the loop.
    // D7.5
    TEST_ASSERT_EQ_STR(text_at(4, 14), "main.ft:4:14-4:15 local v 'i32' decl main.ft:4:14");
    TEST_ASSERT_EQ_STR(text_at(5, 14), "main.ft:5:14-5:15 local v 'i32' use main.ft:4:14");
})

TEST(a_function_that_calls_itself_records_both_occurrences, {
    want_main = false;
    TEST_ASSERT_TRUE(index_src("fn i32 down(i32 n) {\n    if (n <= 0) { return 0; }\n"
                               "    return down(n - 1);\n}\n"));
    TEST_ASSERT_EQ_STR(text_at(1, 8), "main.ft:1:8-1:12 fn down 'fn i32(i32)' decl main.ft:1:8");
    TEST_ASSERT_EQ_STR(text_at(3, 12), "main.ft:3:12-3:16 fn down 'fn i32(i32)' use main.ft:1:8");
})

TEST(a_pseudo_field_denotes_no_declaration, {
    TEST_ASSERT_TRUE(index_src("fn i32 main() {\n    i32[2] a = {1, 2};\n"
                               "    return cast(a.len, i32);\n}\n"));
    // `.len` is a read-only pseudo-field of the type rather than a
    // declaration, so the checker resolves no symbol for it.
    // D3.4
    TEST_ASSERT_EQ_STR(text_at(3, 19), "<none>");
    TEST_ASSERT_EQ_STR(text_at(3, 17), "main.ft:3:17-3:18 local a 'i32[2]' use main.ft:2:12");
})

TEST(an_enum_member_in_a_case_label_is_recorded, {
    want_main = false;
    TEST_ASSERT_TRUE(index_src("enum color {\n    red,\n    green,\n}\n"
                               "fn i32 rank(color c) {\n    switch (c) {\n"
                               "    case color.red:\n        return 0;\n"
                               "    default:\n        return 1;\n    }\n}\n"));
    // A case label is an expression like any other, and `color.red` is the
    // only spelling of a member.
    // D3.9, D7.6
    TEST_ASSERT_EQ_STR(text_at(7, 10), "main.ft:7:10-7:15 enum color '' use main.ft:1:6");
    TEST_ASSERT_EQ_STR(text_at(7, 16), "main.ft:7:16-7:19 enum member red 'color' use main.ft:2:5");
})

TEST(a_function_pointer_local_carries_its_signature, {
    want_main = false;
    TEST_ASSERT_TRUE(index_src("fn i32 add(i32 a, i32 b) { return a + b; }\n"
                               "fn i32 use() {\n    fn i32(i32, i32) op = add;\n"
                               "    return op(1, 2);\n}\n"));
    // The type is the function type as a declaration spells it.
    // D3.10
    TEST_ASSERT_EQ_STR(text_at(3, 22),
                       "main.ft:3:22-3:24 local op 'fn i32(i32, i32)' decl main.ft:3:22");
    TEST_ASSERT_EQ_STR(text_at(3, 27),
                       "main.ft:3:27-3:30 fn add 'fn i32(i32, i32)' use main.ft:1:8");
})

TEST(a_call_of_an_extern_function_is_recorded, {
    want_main = false;
    TEST_ASSERT_TRUE(index_src("extern fn i32 abs(i32 n);\n"
                               "fn i32 f(i32 n) {\n    return abs(n);\n}\n"));
    TEST_ASSERT_EQ_STR(text_at(3, 12),
                       "main.ft:3:12-3:15 extern fn abs 'fn i32(i32)' use main.ft:1:15");
})

TEST(a_write_to_a_global_is_recorded_where_it_stands, {
    TEST_ASSERT_TRUE(index_src("i32 mut seen = 0;\nfn i32 main() {\n    seen = 1;\n"
                               "    return seen;\n}\n"));
    TEST_ASSERT_EQ_STR(text_at(3, 5), "main.ft:3:5-3:9 global seen 'i32 mut' use main.ft:1:9");
    TEST_ASSERT_EQ_STR(text_at(4, 12), "main.ft:4:12-4:16 global seen 'i32 mut' use main.ft:1:9");
})

TEST(a_name_in_a_deferred_statement_is_recorded, {
    want_main = false;
    TEST_ASSERT_TRUE(index_src("fn void f() {\n    i32 mut* own p = new(i32);\n"
                               "    defer del(p);\n}\n"));
    // A deferred statement is a statement of the block, so its names are
    // occurrences like any other.
    // D7.8
    TEST_ASSERT_EQ_STR(text_at(3, 15), "main.ft:3:15-3:16 local p 'i32 mut* own' use main.ft:2:18");
})

TEST(a_field_of_a_struct_type_records_its_type_name, {
    want_main = false;
    TEST_ASSERT_TRUE(index_src("struct inner {\n    i32 x;\n}\n"
                               "struct outer {\n    inner in;\n}\n"));
    TEST_ASSERT_EQ_STR(text_at(5, 5), "main.ft:5:5-5:10 struct inner '' use main.ft:1:8");
    TEST_ASSERT_EQ_STR(text_at(5, 11), "main.ft:5:11-5:13 field in 'inner' decl main.ft:5:11");
})

TEST(a_local_of_an_inner_block_is_its_own_declaration, {
    TEST_ASSERT_TRUE(index_src("fn i32 main() {\n    {\n        i32 n = 1;\n    }\n"
                               "    i32 n = 2;\n    return n;\n}\n"));
    // Two scopes that do not enclose one another may reuse a name, so each
    // occurrence says which declaration it denotes.
    // D7.9
    TEST_ASSERT_EQ_STR(text_at(3, 13), "main.ft:3:13-3:14 local n 'i32' decl main.ft:3:13");
    TEST_ASSERT_EQ_STR(text_at(6, 12), "main.ft:6:12-6:13 local n 'i32' use main.ft:5:9");
})

// ---- the index itself ------------------------------------------------------------

// A module imported by two others: it is read once, so it is walked once
// whichever importer the walk reaches first.
// D9.2, D20.3
static bool index_diamond(void) {
    begin();
    add("base.ft", "i32 LIMIT = 4;\n");
    add("left.ft", "import base;\nfn i32 left() { return base.LIMIT; }\n");
    add("right.ft", "import base;\nfn i32 right() { return base.LIMIT; }\n");
    add("main.ft",
        "import left;\nimport right;\n"
        "fn i32 main() { return left.left() + right.right(); }\n");
    const bool ok = check_entry("main.ft");
    index_closure();
    return ok;
}

TEST(every_file_of_the_closure_is_indexed_once, {
    TEST_ASSERT_TRUE(index_diamond());
    // `base` is imported twice and read once, so its one declaration has one
    // record.
    // D9.2
    TEST_ASSERT_EQ_UINT64(file_count("base.ft"), (uint64_t)1);
    TEST_ASSERT_EQ_STR(file_text("base.ft"), "LIMIT@1:5");
})

TEST(an_imported_module_comes_before_every_importer, {
    TEST_ASSERT_TRUE(index_diamond());
    // Every module is after the ones it imports, so a client that reads the
    // array in order sees a declaration before its users.
    // D9.10
    TEST_ASSERT_EQ_STR(rel_file(index_at(&ix, 0)->loc.file), "base.ft");
    TEST_ASSERT_EQ_STR(rel_file(index_at(&ix, index_count(&ix) - 1)->loc.file), "main.ft");
})

TEST(an_importer_of_a_broken_module_is_indexed_all_the_same, {
    begin();
    add("util.ft", "fn i32 twice(i32 n) { return n + n\n");
    add("main.ft", "import util;\nfn i32 main() { return 0; }\n");
    TEST_UNUSED(check_entry("main.ft"));
    index_closure();
    // The loader never ordered the importer of a file that did not parse, and
    // the checker checked it anyway, so its own names are indexed while the
    // import binds nothing.
    // D14.2, D20.1
    TEST_ASSERT_EQ_STR(file_text("main.ft"), "main@2:8");
    TEST_ASSERT_EQ_UINT64(file_count("util.ft"), (uint64_t)0);
})

TEST(a_struct_literal_and_an_array_literal_record_their_type_names, {
    want_main = false;
    TEST_ASSERT_TRUE(index_src("struct p {\n    i32 x;\n}\n"
                               "fn i32 f() {\n    p v = p{1};\n"
                               "    i32[2] a = i32[2]{1, 2};\n    return v.x + a[0];\n}\n"));
    // The written type of a literal is a type position like any other.
    // D6.5
    TEST_ASSERT_EQ_STR(text_at(5, 11), "main.ft:5:11-5:12 struct p '' use main.ft:1:8");
    TEST_ASSERT_EQ_STR(text_at(5, 5), "main.ft:5:5-5:6 struct p '' use main.ft:1:8");
})

TEST(an_entry_that_did_not_parse_yields_an_empty_index, {
    begin();
    add("main.ft", "fn i32 main() { return 0\n");
    TEST_UNUSED(check_entry("main.ft"));
    index_closure();
    // A file with a syntax error is not checked, so there is no annotation to
    // read and the index is empty rather than partial.
    // D14.2
    TEST_ASSERT_EQ_UINT64(index_count(&ix), (uint64_t)0);
})

static void pass_past_the_end(void) {
    module_set_t empty;
    module_set_init(&empty);
    TEST_UNUSED(module_set_pass_at(&empty, 0));
    module_set_free(&empty);
}

TEST(a_module_past_the_end_of_the_pass_order_is_an_internal_error, {
    char err[ERR_CAP];
    // The order the checker and the index walk share is asked for by index,
    // so asking past its end is a bug like any other bounds failure.
    const int status = run_forked(pass_past_the_end, err, sizeof err);
    TEST_ASSERT_EQ_INT32(status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err,
                       "fort: error: internal error: module_set_pass_at: index out of range\n");
})

static void index_past_the_end(void) {
    index_t empty;
    index_init(&empty);
    TEST_UNUSED(index_at(&empty, 0));
    index_free(&empty);
}

TEST(a_record_past_the_end_is_an_internal_error, {
    char err[ERR_CAP];
    const int status = run_forked(index_past_the_end, err, sizeof err);
    TEST_ASSERT_EQ_INT32(status, FATAL_EXIT_STATUS);
    TEST_ASSERT_EQ_STR(err, "fort: error: internal error: index_at: index out of range\n");
})

TEST(an_empty_index_holds_nothing, {
    index_reset();
    index_init(&ix);
    index_live = true;
    TEST_ASSERT_EQ_UINT64(index_count(&ix), (uint64_t)0);
})

TEST(freeing_an_index_leaves_it_usable, {
    TEST_ASSERT_TRUE(index_src("fn i32 main() { return 0; }\n"));
    TEST_ASSERT_EQ_UINT64(index_count(&ix), (uint64_t)1);
    index_free(&ix);
    // Freeing empties it and leaves it usable, as every container does.
    TEST_ASSERT_EQ_UINT64(index_count(&ix), (uint64_t)0);
    index_build(&ix, &set);
    TEST_ASSERT_EQ_UINT64(index_count(&ix), (uint64_t)1);
})

TEST(a_second_walk_appends_to_the_first, {
    TEST_ASSERT_TRUE(index_src("fn i32 main() { return 0; }\n"));
    index_build(&ix, &set);
    // The records are appended to what the index holds, so a caller that
    // wants one index of two closures builds both into it.
    TEST_ASSERT_EQ_UINT64(index_count(&ix), (uint64_t)2);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("index", argc, argv);
    TEST_RUN(a_declaration_is_recorded_at_its_own_name);
    TEST_RUN(a_use_carries_the_declarations_range);
    TEST_RUN(a_local_is_a_local_and_a_parameter_a_parameter);
    TEST_RUN(a_locals_type_carries_its_level_0_mutability);
    TEST_RUN(a_builtin_has_no_declaration_to_jump_to);
    TEST_RUN(a_struct_name_and_an_enum_name_have_no_type);
    TEST_RUN(a_field_and_an_enum_member_carry_their_types);
    TEST_RUN(a_constant_and_a_global_are_told_apart);
    TEST_RUN(an_extern_function_is_an_extern_fn);
    TEST_RUN(a_qualified_use_records_the_module_and_the_declaration);
    TEST_RUN(the_last_segment_of_an_import_path_is_the_module);
    TEST_RUN(a_field_access_records_the_field_of_the_struct);
    TEST_RUN(an_enum_member_is_reached_through_its_enum);
    TEST_RUN(an_imported_module_comes_before_its_importer);
    TEST_RUN(the_records_of_a_file_are_in_position_order);
    TEST_RUN(an_operand_comes_before_the_field_it_is_read_through);
    TEST_RUN(the_records_of_one_line_are_in_column_order);
    TEST_RUN(a_directory_segment_of_an_import_path_carries_no_record);
    TEST_RUN(an_alias_of_an_import_that_failed_carries_no_record);
    TEST_RUN(the_module_node_itself_is_not_an_occurrence);
    TEST_RUN(a_name_that_did_not_resolve_carries_no_record);
    TEST_RUN(a_declaration_that_failed_to_check_has_no_type_to_show);
    TEST_RUN(a_module_that_did_not_parse_contributes_nothing);
    TEST_RUN(a_module_whose_import_failed_is_indexed_all_the_same);
    TEST_RUN(an_alias_import_records_the_declaration_and_the_alias);
    TEST_RUN(a_whole_module_alias_declares_its_name_here);
    TEST_RUN(one_module_imported_under_two_names_declares_both);
    TEST_RUN(an_item_list_records_the_module_and_every_item);
    TEST_RUN(a_designator_records_the_field_it_names);
    TEST_RUN(a_shadowing_local_hides_the_module_level_name);
    TEST_RUN(a_type_name_in_sizeof_a_cast_and_new_is_recorded);
    TEST_RUN(the_variable_of_a_range_loop_is_a_local);
    TEST_RUN(a_function_that_calls_itself_records_both_occurrences);
    TEST_RUN(a_pseudo_field_denotes_no_declaration);
    TEST_RUN(an_enum_member_in_a_case_label_is_recorded);
    TEST_RUN(a_function_pointer_local_carries_its_signature);
    TEST_RUN(a_call_of_an_extern_function_is_recorded);
    TEST_RUN(a_write_to_a_global_is_recorded_where_it_stands);
    TEST_RUN(a_name_in_a_deferred_statement_is_recorded);
    TEST_RUN(a_field_of_a_struct_type_records_its_type_name);
    TEST_RUN(a_local_of_an_inner_block_is_its_own_declaration);
    TEST_RUN(every_file_of_the_closure_is_indexed_once);
    TEST_RUN(an_imported_module_comes_before_every_importer);
    TEST_RUN(an_importer_of_a_broken_module_is_indexed_all_the_same);
    TEST_RUN(a_struct_literal_and_an_array_literal_record_their_type_names);
    TEST_RUN(an_entry_that_did_not_parse_yields_an_empty_index);
    TEST_RUN(a_module_past_the_end_of_the_pass_order_is_an_internal_error);
    TEST_RUN(a_record_past_the_end_is_an_internal_error);
    TEST_RUN(an_empty_index_holds_nothing);
    TEST_RUN(freeing_an_index_leaves_it_usable);
    TEST_RUN(a_second_walk_appends_to_the_first);
    index_reset();
    check_reset();
    done();
    TEST_EXIT();
}

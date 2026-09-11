// Unit tests of module resolution (module-system.md 1, 2, 3, 13;
// D9.1 to D9.4): the search roots, the two readings of an import path, the
// standard library root, and the ambiguity and not-found rules. The closure,
// its order, the namespaces and the extern declarations are the other half,
// in modules_closure_test.c.
#include "modules.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "modules_helpers.h"
#include "scope.h"
#include "str.h"

#include "test.h"

// ---- the entry file (module-system.md 2) --------------------------------------------

TEST(the_entry_module_path_is_its_base_name, {
    begin();
    add("main.ft", src_main());
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_EQ_UINT64(module_set_count(&set), (uint64_t)1);
    TEST_ASSERT_EQ_STR(ordered(0), "main");
    TEST_ASSERT_TRUE(module_set_entry(&set) == module_set_at(&set, 0));
})

TEST(an_entry_file_in_a_directory_keeps_only_its_base_name, {
    begin();
    add("src/app.ft", src_main());
    TEST_ASSERT_TRUE(load("src/app.ft"));
    TEST_ASSERT_EQ_STR(ordered(0), "app");
})

TEST(the_entry_directory_is_the_first_search_root, {
    begin();
    add("src/app.ft", "import util;\nfn i32 main() { return util.add(1, 2); }\n");
    add("src/util.ft", src_add());
    TEST_ASSERT_TRUE(load("src/app.ft"));
    TEST_ASSERT_EQ_STR(ordered(0), "util");
    TEST_ASSERT_EQ_STR(ordered(1), "app");
})

TEST(an_entry_base_name_that_is_no_identifier_is_taken_as_it_is, {
    begin();
    add("001_leading_zero.ft", src_main());
    // module-system.md 2 asks the entry base name to be a module path
    // segment, which the test corpus D14.4 fixes (`NNN_name.ft`, compiled as
    // the entry file) cannot satisfy; decisions.md wins (D1.2). Such a name
    // only makes the module unreachable by an import, which cannot spell it.
    TEST_ASSERT_TRUE(load("001_leading_zero.ft"));
    TEST_ASSERT_EQ_STR(ordered(0), "001_leading_zero");
})

TEST(an_unreadable_entry_file_is_reported, {
    begin();
    TEST_ASSERT_FALSE(load("missing.ft"));
    TEST_ASSERT_TRUE(said("cannot read"));
})

TEST(a_lexical_error_stops_the_file, {
    begin();
    add("main.ft", "fn i32 main() {\n    i32 x = 08;\n    return x;\n}\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)1);
})

TEST(a_syntax_error_stops_the_file, {
    begin();
    add("main.ft", "fn i32 main() {\n    i32 x = 1\n    return x;\n}\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("error:"));
})

TEST(a_syntax_error_in_an_imported_module_stops_the_compilation, {
    begin();
    add("main.ft", "import util;\nfn i32 main() { return util.add(1, 2); }\n");
    add("util.ft", "fn i32 add(i32 a i32 b) { return a; }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("util.ft:1:"));
})

// ---- the module reading (module-system.md 3) ----------------------------------------

TEST(an_import_binds_the_short_name_to_the_module, {
    begin();
    add("main.ft", "import util;\nfn i32 main() { return util.add(1, 2); }\n");
    add("util.ft", src_add());
    TEST_ASSERT_TRUE(load("main.ft"));
    const binding_t* b = bound("main", "util");
    TEST_ASSERT_NONNULL(b);
    TEST_ASSERT_EQ_INT32((int32_t)b->kind, (int32_t)BIND_MODULE);
    TEST_ASSERT_TRUE(b->module == module_set_find(&set, str_from_cstr("util")));
})

TEST(a_nested_path_names_a_file_in_a_directory, {
    begin();
    add("main.ft", "import geom::vec;\nfn i32 main() { return 0; }\n");
    add("geom/vec.ft", "struct vec2 {\n    i64 x;\n    i64 y;\n}\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_EQ_STR(ordered(0), "geom::vec");
    TEST_ASSERT_NONNULL(bound("main", "vec"));
})

TEST(as_renames_a_module_binding, {
    begin();
    add("main.ft", "import util as u;\nfn i32 main() { return u.add(1, 2); }\n");
    add("util.ft", src_add());
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_NONNULL(bound("main", "u"));
    TEST_ASSERT_NULL(bound("main", "util"));
})

TEST(import_paths_are_root_relative_and_not_file_relative, {
    begin();
    add("main.ft", "import util::a;\nfn i32 main() { return 0; }\n");
    add("util/a.ft", "import util::b;\nfn i32 one() { return b.two(); }\n");
    add("util/b.ft", "fn i32 two() { return 2; }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_EQ_STR(ordered(0), "util::b");
    TEST_ASSERT_EQ_STR(ordered(1), "util::a");
})

TEST(a_sibling_is_not_reachable_by_its_short_name, {
    begin();
    add("main.ft", "import util::a;\nfn i32 main() { return 0; }\n");
    add("util/a.ft", "import b;\nfn i32 one() { return b.two(); }\n");
    add("util/b.ft", "fn i32 two() { return 2; }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("module 'b' not found"));
})

TEST(an_include_root_is_searched_after_the_entry_directory, {
    begin();
    add("src/main.ft", "import util;\nfn i32 main() { return 0; }\n");
    add("lib/util.ft", src_add());
    root("lib");
    TEST_ASSERT_TRUE(load("src/main.ft"));
    TEST_ASSERT_EQ_STR(ordered(0), "util");
})

TEST(the_entry_directory_wins_over_an_include_root, {
    begin();
    add("src/main.ft", "import util;\nfn i32 main() { return 0; }\n");
    add("src/util.ft", src_add());
    add("lib/util.ft", "fn i32 add(i32 a, i32 b) { return 0; }\n");
    root("lib");
    TEST_ASSERT_TRUE(load("src/main.ft"));
    const module_t* util = module_set_find(&set, str_from_cstr("util"));
    TEST_ASSERT_NONNULL(util);
    TEST_ASSERT_NONNULL(strstr(util->file.ptr, "src/util.ft"));
})

TEST(include_roots_are_searched_in_command_line_order, {
    begin();
    add("main.ft", "import util;\nfn i32 main() { return 0; }\n");
    add("first/util.ft", src_add());
    add("second/util.ft", src_add());
    root("first");
    root("second");
    TEST_ASSERT_TRUE(load("main.ft"));
    const module_t* util = module_set_find(&set, str_from_cstr("util"));
    TEST_ASSERT_NONNULL(util);
    TEST_ASSERT_NONNULL(strstr(util->file.ptr, "first/util.ft"));
})

// ---- the standard library root (D9.2) -----------------------------------------------

TEST(a_std_path_is_looked_up_in_the_standard_library_directory, {
    begin();
    add("main.ft", "import std::io;\nfn i32 main() { return 0; }\n");
    add("lib/io.ft", "fn i32 close(i32 fd) { return fd; }\n");
    std_dir("lib");
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_EQ_STR(ordered(0), "std::io");
    TEST_ASSERT_NONNULL(bound("main", "io"));
})

TEST(a_std_path_is_not_looked_up_under_any_other_root, {
    begin();
    add("main.ft", "import std::io;\nfn i32 main() { return 0; }\n");
    add("std/io.ft", "fn i32 close(i32 fd) { return fd; }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("module 'std::io' not found"));
})

TEST(a_path_that_does_not_begin_with_std_never_reaches_the_library, {
    begin();
    add("main.ft", "import io;\nfn i32 main() { return 0; }\n");
    add("lib/io.ft", "fn i32 close(i32 fd) { return fd; }\n");
    std_dir("lib");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("module 'io' not found"));
})

TEST(std_alone_is_a_directory_and_not_a_module, {
    begin();
    add("main.ft", "import std;\nfn i32 main() { return 0; }\n");
    add("lib/io.ft", "fn i32 close(i32 fd) { return fd; }\n");
    std_dir("lib");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("module 'std' not found"));
})

// ---- the symbol reading (module-system.md 3) ----------------------------------------

TEST(a_trailing_declaration_is_bound_to_that_declaration, {
    begin();
    add("main.ft", "import util::add;\nfn i32 main() { return add(1, 2); }\n");
    add("util.ft", src_add());
    TEST_ASSERT_TRUE(load("main.ft"));
    const binding_t* b = bound("main", "add");
    TEST_ASSERT_NONNULL(b);
    TEST_ASSERT_EQ_INT32((int32_t)b->kind, (int32_t)BIND_SYMBOL);
    TEST_ASSERT_TRUE(b->to == bound("util", "add"));
})

TEST(as_renames_an_imported_declaration, {
    begin();
    add("main.ft",
        "import util::add as plus;\n"
        "fn i32 main() { return plus(1, 2); }\n");
    add("util.ft", src_add());
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_NONNULL(bound("main", "plus"));
    TEST_ASSERT_NULL(bound("main", "add"));
})

TEST(braces_are_sugar_for_independent_symbol_imports, {
    begin();
    add("main.ft",
        "import util::{add, mul as times};\n"
        "fn i32 main() { return add(1, times(2, 3)); }\n");
    add("util.ft",
        "fn i32 add(i32 a, i32 b) { return a + b; }\n"
        "fn i32 mul(i32 a, i32 b) { return a * b; }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_NONNULL(bound("main", "add"));
    TEST_ASSERT_NONNULL(bound("main", "times"));
    TEST_ASSERT_NULL(bound("main", "mul"));
})

TEST(a_braced_import_whose_prefix_is_no_module_file_is_an_error, {
    begin();
    add("main.ft", "import std::{io, str};\nfn i32 main() { return 0; }\n");
    add("lib/io.ft", "fn i32 close(i32 fd) { return fd; }\n");
    std_dir("lib");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("module 'std' not found"));
})

TEST(a_declaration_the_module_lacks_is_an_error, {
    begin();
    add("main.ft", "import util::strngs;\nfn i32 main() { return 0; }\n");
    add("util.ft", src_add());
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("module 'util' has no declaration named 'strngs'"));
})

TEST(an_import_binding_of_another_module_is_not_importable, {
    begin();
    add("main.ft", "import util::other;\nfn i32 main() { return 0; }\n");
    add("util.ft", "import other;\nfn i32 add(i32 a) { return other.one(); }\n");
    add("other.ft", "fn i32 one() { return 1; }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("cannot import 'other': it is an import of module 'util'"));
})

TEST(a_one_segment_path_has_only_the_module_reading, {
    begin();
    add("main.ft", "import add;\nfn i32 main() { return 0; }\n");
    add("util.ft", src_add());
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("module 'add' not found"));
})

TEST(a_missing_module_lists_the_paths_it_looked_for, {
    begin();
    add("main.ft", "import util::strings;\nfn i32 main() { return 0; }\n");
    root("lib");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("module 'util::strings' not found"));
    TEST_ASSERT_TRUE(said("note: looked for"));
    TEST_ASSERT_TRUE(said("util/strings.ft"));
    TEST_ASSERT_TRUE(said("util.ft"));
})

// ---- ambiguity (module-system.md 3) -------------------------------------------------

TEST(both_readings_succeeding_is_ambiguous, {
    begin();
    add("main.ft", "import a::b::c;\nfn i32 main() { return 0; }\n");
    add("a/b.ft", "fn i32 c(i32 x) { return x; }\n");
    add("a/b/c.ft", "fn i32 f() { return 0; }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("ambiguous import 'a::b::c'"));
    TEST_ASSERT_TRUE(said("a/b/c.ft"));
    TEST_ASSERT_TRUE(said("a/b.ft"));
})

TEST(ambiguity_holds_whichever_roots_the_two_files_live_under, {
    begin();
    add("src/main.ft", "import a::b::c;\nfn i32 main() { return 0; }\n");
    add("src/a/b.ft", "fn i32 c(i32 x) { return x; }\n");
    add("lib/a/b/c.ft", "fn i32 f() { return 0; }\n");
    root("lib");
    TEST_ASSERT_FALSE(load("src/main.ft"));
    TEST_ASSERT_TRUE(said("ambiguous import 'a::b::c'"));
})

TEST(a_prefix_that_lacks_the_name_leaves_the_module_reading, {
    begin();
    add("main.ft", "import a::b::c;\nfn i32 main() { return 0; }\n");
    add("a/b.ft", "fn i32 other() { return 0; }\n");
    add("a/b/c.ft", "fn i32 f() { return 0; }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    const binding_t* b = bound("main", "c");
    TEST_ASSERT_NONNULL(b);
    TEST_ASSERT_EQ_INT32((int32_t)b->kind, (int32_t)BIND_MODULE);
})

TEST(a_missing_module_file_leaves_the_symbol_reading, {
    begin();
    add("main.ft", "import a::b::c;\nfn i32 main() { return 0; }\n");
    add("a/b.ft", "fn i32 c(i32 x) { return x; }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    const binding_t* b = bound("main", "c");
    TEST_ASSERT_NONNULL(b);
    TEST_ASSERT_EQ_INT32((int32_t)b->kind, (int32_t)BIND_SYMBOL);
})

// ---- the prefix of an import is read only when it is the module (10) -----------------

TEST(a_prefix_that_lacks_the_name_is_not_read_into_the_closure, {
    begin();
    add("main.ft", "import util::helper;\nfn i32 main() { return 0; }\n");
    add("util/helper.ft", "fn i32 help() { return 0; }\n");
    add("util.ft", "fn i32 unrelated() { return 0; }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    // The module reading won, so `util.ft` is a module no import reaches and
    // is never read into the closure (module-system.md 10).
    TEST_ASSERT_EQ_UINT64(module_set_count(&set), (uint64_t)2);
    TEST_ASSERT_NULL(module_set_find(&set, str_from_cstr("util")));
    TEST_ASSERT_EQ_STR(ordered(0), "util::helper");
})

TEST(a_syntax_error_in_a_prefix_the_module_reading_beats_is_not_reported, {
    begin();
    add("main.ft", "import util::helper;\nfn i32 main() { return 0; }\n");
    add("util/helper.ft", "fn i32 help() { return 0; }\n");
    add("util.ft", "fn i32 broken(i32 a i32 b) { return a; }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_EQ_STR(diags(), "");
})

TEST(an_import_of_the_entry_from_such_a_prefix_is_not_a_cycle, {
    begin();
    add("main.ft", "import util::helper;\nfn i32 main() { return 0; }\n");
    add("util/helper.ft", "fn i32 help() { return 0; }\n");
    add("util.ft", "import main;\nfn i32 unrelated() { return 0; }\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_FALSE(said("circular"));
})

TEST(an_extern_of_such_a_prefix_conflicts_with_nothing, {
    begin();
    add("main.ft",
        "import util::helper;\n"
        "extern fn i64 write(i32 fd, void* buf, u64 n);\n"
        "fn i32 main() { return 0; }\n");
    add("util/helper.ft", "fn i32 help() { return 0; }\n");
    add("util.ft", "extern fn i32 write(i32 fd, void* buf, u64 n);\n");
    TEST_ASSERT_TRUE(load("main.ft"));
    TEST_ASSERT_EQ_STR(diags(), "");
})

TEST(a_prefix_the_closure_already_holds_answers_from_its_namespace, {
    begin();
    add("main.ft",
        "import util;\n"
        "import util::add;\n"
        "fn i32 main() { return 0; }\n");
    add("util.ft", src_add());
    add("util/add.ft", "fn i32 f() { return 0; }\n");
    // `util` is in the closure, so the symbol reading is answered from its
    // namespace, and both readings succeeding is ambiguous (D9.3).
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("ambiguous import 'util::add'"));
})

TEST(every_failing_import_of_one_module_is_reported, {
    begin();
    add("main.ft",
        "import nothere;\n"
        "import alsomissing;\n"
        "fn i32 main() { return 0; }\n");
    // All the errors of the module that has them are reported, then
    // processing stops at the module boundary (D14.2).
    TEST_ASSERT_FALSE(load("main.ft"));
    TEST_ASSERT_TRUE(said("module 'nothere' not found"));
    TEST_ASSERT_TRUE(said("module 'alsomissing' not found"));
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)2);
})

TEST(no_further_module_is_read_after_an_import_failed, {
    begin();
    add("main.ft",
        "import nothere;\n"
        "import util;\n"
        "fn i32 main() { return 0; }\n");
    add("util.ft", "fn i32 broken(i32 a i32 b) { return a; }\n");
    TEST_ASSERT_FALSE(load("main.ft"));
    // The second import would have read a module with its own syntax error;
    // one module's errors appear together (D14.2), so it is left alone.
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)1);
    TEST_ASSERT_TRUE(said("module 'nothere' not found"));
})

int main(int argc, char** argv) {
    TEST_INIT("modules", argc, argv);
    TEST_RUN(the_entry_module_path_is_its_base_name);
    TEST_RUN(an_entry_file_in_a_directory_keeps_only_its_base_name);
    TEST_RUN(the_entry_directory_is_the_first_search_root);
    TEST_RUN(an_entry_base_name_that_is_no_identifier_is_taken_as_it_is);
    TEST_RUN(an_unreadable_entry_file_is_reported);
    TEST_RUN(a_lexical_error_stops_the_file);
    TEST_RUN(a_syntax_error_stops_the_file);
    TEST_RUN(a_syntax_error_in_an_imported_module_stops_the_compilation);
    TEST_RUN(an_import_binds_the_short_name_to_the_module);
    TEST_RUN(a_nested_path_names_a_file_in_a_directory);
    TEST_RUN(as_renames_a_module_binding);
    TEST_RUN(import_paths_are_root_relative_and_not_file_relative);
    TEST_RUN(a_sibling_is_not_reachable_by_its_short_name);
    TEST_RUN(an_include_root_is_searched_after_the_entry_directory);
    TEST_RUN(the_entry_directory_wins_over_an_include_root);
    TEST_RUN(include_roots_are_searched_in_command_line_order);
    TEST_RUN(a_std_path_is_looked_up_in_the_standard_library_directory);
    TEST_RUN(a_std_path_is_not_looked_up_under_any_other_root);
    TEST_RUN(a_path_that_does_not_begin_with_std_never_reaches_the_library);
    TEST_RUN(std_alone_is_a_directory_and_not_a_module);
    TEST_RUN(a_trailing_declaration_is_bound_to_that_declaration);
    TEST_RUN(as_renames_an_imported_declaration);
    TEST_RUN(braces_are_sugar_for_independent_symbol_imports);
    TEST_RUN(a_braced_import_whose_prefix_is_no_module_file_is_an_error);
    TEST_RUN(a_declaration_the_module_lacks_is_an_error);
    TEST_RUN(an_import_binding_of_another_module_is_not_importable);
    TEST_RUN(a_one_segment_path_has_only_the_module_reading);
    TEST_RUN(a_missing_module_lists_the_paths_it_looked_for);
    TEST_RUN(both_readings_succeeding_is_ambiguous);
    TEST_RUN(ambiguity_holds_whichever_roots_the_two_files_live_under);
    TEST_RUN(a_prefix_that_lacks_the_name_leaves_the_module_reading);
    TEST_RUN(a_missing_module_file_leaves_the_symbol_reading);
    TEST_RUN(a_prefix_that_lacks_the_name_is_not_read_into_the_closure);
    TEST_RUN(a_syntax_error_in_a_prefix_the_module_reading_beats_is_not_reported);
    TEST_RUN(an_import_of_the_entry_from_such_a_prefix_is_not_a_cycle);
    TEST_RUN(an_extern_of_such_a_prefix_conflicts_with_nothing);
    TEST_RUN(a_prefix_the_closure_already_holds_answers_from_its_namespace);
    TEST_RUN(every_failing_import_of_one_module_is_reported);
    TEST_RUN(no_further_module_is_read_after_an_import_failed);
    done();
    TEST_EXIT();
}

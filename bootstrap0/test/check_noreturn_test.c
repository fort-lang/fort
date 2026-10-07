// Tests that a written `noreturn` gets one diagnostic: a group, a suffix or a `mut` on it. Tests
// that a `mut` on a bare `void` return type gets the message of `i32 mut` there.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ast.h"
#include "check.h"
#include "common/check_helpers.h"

#include "common/test.h"

// NOLINTBEGIN(readability-magic-numbers) the sources below are the test data.

// Checks `decls` before an empty `main`. The check must fail with one diagnostic, `want`.
static bool reports_once(const char* decls, const char* want) {
    char text[256];
    TEST_UNUSED(snprintf(text, sizeof text, "%sfn main() i32 {\n    return 0;\n}\n", decls));
    return !check_src(text) && diag_lines() == 1 && said(want);
}

TEST(a_grouped_or_marked_noreturn_is_reported_once, {
    // In a group with a suffix, `noreturn` is not the return type, so base_type reports the
    // keyword. The suffix outside the group must not report a second time.
    const char* nr = "error: 'noreturn' is a return type";
    TEST_ASSERT_TRUE(reports_once("fn f() (noreturn*) {\n    return null;\n}\n", nr));
    TEST_ASSERT_TRUE(said("1:9: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() ((noreturn)*) {\n    return null;\n}\n", nr));
    TEST_ASSERT_TRUE(said("1:10: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() (noreturn[2]) {\n    panic(\"x\");\n}\n", nr));
    TEST_ASSERT_TRUE(said("1:9: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() (noreturn mut*) {\n    return null;\n}\n", nr));
    TEST_ASSERT_TRUE(said("1:9: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() (noreturn* own) {\n    return null;\n}\n", nr));
    TEST_ASSERT_TRUE(said("1:9: error"));
    TEST_ASSERT_TRUE(reports_once("fn () (noreturn*) V = null;\n", nr));
    TEST_ASSERT_TRUE(said("1:8: error"));
    // A group of a bare `noreturn` is the return type, and a suffix on it is the error of the
    // whole type. The `mut` inside the group adds no second report.
    TEST_ASSERT_TRUE(reports_once("fn f() (noreturn mut)* {\n    return null;\n}\n", nr));
    TEST_ASSERT_TRUE(said("1:8: error"));
    // A `mut` on a `noreturn` return type gets the message of `i32 mut` there. The checker builds
    // `noreturn` as `void`, so without this report the message would be about `void`.
    const char* nb = "error: a return type has no binding: remove the outermost 'mut'";
    TEST_ASSERT_TRUE(reports_once("fn f() noreturn mut {\n    panic(\"x\");\n}\n", nb));
    TEST_ASSERT_TRUE(said("1:8: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() (noreturn) mut {\n    panic(\"x\");\n}\n", nb));
    TEST_ASSERT_TRUE(said("1:8: error"));
    // The outermost type reports a `mut` in any of its groups, once and at its own position.
    TEST_ASSERT_TRUE(reports_once("fn f() (noreturn mut) {\n    panic(\"x\");\n}\n", nb));
    TEST_ASSERT_TRUE(said("1:8: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() ((noreturn) mut) {\n    panic(\"x\");\n}\n", nb));
    TEST_ASSERT_TRUE(said("1:8: error"));
    // The return type of a function type follows the same rule.
    TEST_ASSERT_TRUE(reports_once("fn () noreturn mut P = null;\n", nb));
    TEST_ASSERT_TRUE(said("1:7: error"));
    TEST_ASSERT_TRUE(reports_once("(fn () noreturn mut) Q = null;\n", nb));
    TEST_ASSERT_TRUE(said("1:8: error"));
    TEST_ASSERT_FALSE(check_body("    fn () noreturn mut p = null;"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_TRUE(said("2:11: error: a return type has no binding"));
    TEST_ASSERT_FALSE(said("'void'"));
    // The groups that hold a bare `noreturn` stay a return type.
    TEST_ASSERT_TRUE(check_src("fn f() ((noreturn)) {\n    panic(\"x\");\n}\n"
                               "fn main() i32 {\n    return 0;\n}\n"));
})

TEST(a_deeper_group_of_noreturn_is_reported_once, {
    const char* nr = "error: 'noreturn' is a return type";
    const char* nb = "error: a return type has no binding: remove the outermost 'mut'";
    // A `mut` at any depth of the groups is the `mut` of the return type, reported at its start.
    TEST_ASSERT_TRUE(reports_once("fn f() ((noreturn mut)) {\n    panic(\"x\");\n}\n", nb));
    TEST_ASSERT_TRUE(said("1:8: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() ((noreturn)) mut {\n    panic(\"x\");\n}\n", nb));
    TEST_ASSERT_TRUE(said("1:8: error"));
    TEST_ASSERT_TRUE(reports_once("fn () ((noreturn) mut) R = null;\n", nb));
    TEST_ASSERT_TRUE(said("1:7: error"));
    // A suffix outside the groups makes the whole type the error, whatever `mut` they hold.
    TEST_ASSERT_TRUE(reports_once("fn f() ((noreturn) mut)* {\n    return null;\n}\n", nr));
    TEST_ASSERT_TRUE(said("1:8: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() ((noreturn))[2] {\n    panic(\"x\");\n}\n", nr));
    TEST_ASSERT_TRUE(said("1:8: error"));
    TEST_ASSERT_TRUE(reports_once("fn () (noreturn mut)* S = null;\n", nr));
    TEST_ASSERT_TRUE(said("1:7: error"));
    // A group with a suffix inside makes `noreturn` the base of a value type, which base_type
    // reports at the keyword.
    TEST_ASSERT_TRUE(reports_once("fn f() ((noreturn mut)*) {\n    return null;\n}\n", nr));
    TEST_ASSERT_TRUE(said("1:10: error"));
    TEST_ASSERT_FALSE(said("'void'"));
})

TEST(each_group_of_a_noreturn_return_type_has_the_type_of_noreturn, {
    // The outermost type checks the groups itself. It gives each group and the keyword the
    // `void` that `noreturn` builds, as a check of each group did.
    TEST_ASSERT_TRUE(check_src("fn die() (((noreturn))) {\n    panic(\"x\");\n}\n"
                               "fn main() i32 {\n    fn () ((noreturn)) f = die;\n"
                               "    println(f);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(type_text(sym_main("die")->type), "fn () noreturn");
    TEST_ASSERT_EQ_STR(decl_type("f"), "fn () noreturn");
    const ast_node_t* decl = node_in_main(AST_FN_DECL, "die");
    TEST_ASSERT_TRUE(decl != NULL);
    uint64_t depth = 0;
    for (const ast_node_t* t = decl->a; t != NULL; t = t->a) {
        TEST_ASSERT_TRUE(t->type != NULL);
        TEST_ASSERT_EQ_STR(type_text(t->type), "void");
        depth++;
    }
    // The wrapper of the return type, three groups and the keyword.
    TEST_ASSERT_EQ_UINT64(depth, (uint64_t)5);
})

TEST(a_marked_void_return_type_has_no_binding, {
    // A `mut` on a bare `void` return type gets the message of `i32 mut` and `noreturn mut`
    // there, once and at the start of the return type. It is not the message about `void`.
    const char* nb = "error: a return type has no binding: remove the outermost 'mut'";
    TEST_ASSERT_TRUE(reports_once("fn f() void mut { }\n", nb));
    TEST_ASSERT_TRUE(said("1:8: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() (void) mut { }\n", nb));
    TEST_ASSERT_TRUE(said("1:8: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() (void mut) { }\n", nb));
    TEST_ASSERT_TRUE(said("1:8: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() ((void) mut) { }\n", nb));
    TEST_ASSERT_TRUE(said("1:8: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() ((void mut)) { }\n", nb));
    TEST_ASSERT_TRUE(said("1:8: error"));
    TEST_ASSERT_FALSE(said("'void'"));
    // The return type of a function type follows the same rule.
    TEST_ASSERT_TRUE(reports_once("fn () void mut P = null;\n", nb));
    TEST_ASSERT_TRUE(said("1:7: error"));
    TEST_ASSERT_TRUE(reports_once("(fn () (void) mut) Q = null;\n", nb));
    TEST_ASSERT_TRUE(said("1:8: error"));
    TEST_ASSERT_FALSE(check_body("    fn () void mut p = null;"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_TRUE(said("2:11: error: a return type has no binding"));
    TEST_ASSERT_FALSE(said("'void'"));
    // In other positions `void mut` keeps the message about `void`.
    const char* nv = "error: 'void' is only a return type or the base of 'void*'";
    TEST_ASSERT_TRUE(reports_once("fn f(void mut p) void { }\n", nv));
    TEST_ASSERT_TRUE(reports_once("fn f((void) mut p) void { }\n", nv));
    TEST_ASSERT_TRUE(said("1:7: error"));
})

TEST(a_suffix_on_a_group_of_void_keeps_the_void_message, {
    // `void` needs its `*` inside the same group (D3.11). A `mut` in that group does not make
    // the binding message: the innermost `mut`, or else the group, reports the `void`.
    const char* nv = "error: 'void' is only a return type or the base of 'void*'";
    TEST_ASSERT_TRUE(reports_once("fn f() (void)* {\n    return null;\n}\n", nv));
    TEST_ASSERT_TRUE(said("1:9: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() (void mut)* {\n    return null;\n}\n", nv));
    TEST_ASSERT_TRUE(said("1:9: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() ((void) mut)* {\n    return null;\n}\n", nv));
    TEST_ASSERT_TRUE(said("1:9: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() ((void mut))* {\n    return null;\n}\n", nv));
    TEST_ASSERT_TRUE(said("1:10: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() ((void))* {\n    return null;\n}\n", nv));
    TEST_ASSERT_TRUE(said("1:9: error"));
    TEST_ASSERT_TRUE(reports_once("fn f() (void)* mut {\n    return null;\n}\n", nv));
    TEST_ASSERT_TRUE(said("1:9: error"));
    TEST_ASSERT_FALSE(said("no binding"));
    // A `mut` on `void*` itself is the binding of a pointer, which `i32* mut` reports.
    TEST_ASSERT_TRUE(reports_once("fn f() void* mut {\n    return null;\n}\n",
                                  "error: a return type has no binding"));
    TEST_ASSERT_TRUE(said("1:8: error"));
})

TEST(each_group_of_a_void_return_type_has_the_type_void, {
    TEST_ASSERT_TRUE(check_src("fn f() ((void)) { }\nfn g() void mut* {\n    return null;\n}\n"
                               "fn main() i32 {\n    fn () ((void)) h = f;\n    f();\n"
                               "    println(h);\n    return 0;\n}\n"));
    TEST_ASSERT_EQ_STR(type_text(sym_main("f")->type), "fn () void");
    TEST_ASSERT_EQ_STR(type_text(sym_main("g")->type), "fn () void mut*");
    TEST_ASSERT_EQ_STR(decl_type("h"), "fn () void");
    const ast_node_t* decl = node_in_main(AST_FN_DECL, "f");
    TEST_ASSERT_TRUE(decl != NULL);
    uint64_t depth = 0;
    for (const ast_node_t* t = decl->a; t != NULL; t = t->a) {
        TEST_ASSERT_TRUE(t->type != NULL);
        TEST_ASSERT_EQ_STR(type_text(t->type), "void");
        depth++;
    }
    // The wrapper of the return type, two groups and the keyword.
    TEST_ASSERT_EQ_UINT64(depth, (uint64_t)4);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("check_noreturn", argc, argv);
    TEST_RUN(a_grouped_or_marked_noreturn_is_reported_once);
    TEST_RUN(a_deeper_group_of_noreturn_is_reported_once);
    TEST_RUN(each_group_of_a_noreturn_return_type_has_the_type_of_noreturn);
    TEST_RUN(a_marked_void_return_type_has_no_binding);
    TEST_RUN(a_suffix_on_a_group_of_void_keeps_the_void_message);
    TEST_RUN(each_group_of_a_void_return_type_has_the_type_void);
    check_reset();
    done();
    TEST_EXIT();
}

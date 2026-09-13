// Unit tests of `--ast` (toolchain.md 1): the option that lexes and parses
// the entry file alone -- resolving no import, checking nothing -- and writes
// its syntax tree to stdout as one S-expression.
// D14.1
//
// It is the observation point the self-hosted parser is verified at, so what
// the suite asserts is the contract tools/diff_ast.sh relies on: the tree is
// the whole of stdout and ends in one newline, a syntax error is reported on
// stderr and still leaves the file's tree written whole, and nothing else of
// the pipeline runs -- no `--cc` is spawned, no temporary is created, no
// import is opened. The sandbox is driver_helpers.h and test/fake_cc.sh is
// the `--cc` that must never run.
// D14.2
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "diag.h"
#include "driver.h"
#include "driver_helpers.h"
#include "str.h"

#include "test.h"

// The tree of the entry file the sandbox writes (driver_helpers.h):
// `fn i32 main() { return 0; }`.
static const char SANDBOX_ENTRY_TREE[] =
    "(module (fn (type (prim i32)) main (params) (block (return (int 0)))))\n";

// ---- the command line -------------------------------------------------------------
// D14.1

// `fort --ast main.ft`, parsed without running anything. A brace initializer
// inside a TEST body would split the body into two macro arguments, so the
// argument list stands here; driver_parse takes `char**`, so it is a variable
// and lower_case rather than a constant.
static char* ast_argv[] = {"fort", "--ast", "main.ft", NULL};

// The options --ast refuses to stand beside, and the line it refuses them
// with.
// D14.1
static const char* const REJECTED_WITH_AST[] = {"--tokens", "--check", "--json", "--index"};
static const char AST_CONFLICT[] =
    "fort: error: --ast does not combine with --tokens, --check, --json or --index\n"
    "usage: fort [options] entry.ft\n";

TEST(ast_is_parsed_as_a_flag, {
    driver_options_t opts;
    driver_options_init(&opts);
    int argc = 0;
    while (ast_argv[argc] != NULL) {
        argc++;
    }
    FILE* out = tmpfile();
    FILE* err = tmpfile();
    TEST_ASSERT_NONNULL(out);
    TEST_ASSERT_NONNULL(err);
    TEST_ASSERT_EQ_INT32(driver_parse(&opts, argc, ast_argv, out, err), DRIVER_PARSE_OK);
    TEST_ASSERT_TRUE(opts.ast);
    // It implies nothing: neither the lexer dump nor the front end is run.
    // D14.1
    TEST_ASSERT_FALSE(opts.tokens);
    TEST_ASSERT_FALSE(opts.check);
    TEST_ASSERT_FALSE(opts.json);
    TEST_ASSERT_FALSE(opts.index);
    TEST_ASSERT_EQ_STR(opts.entry, "main.ft");
    TEST_UNUSED(fclose(out));
    TEST_UNUSED(fclose(err));
    driver_options_free(&opts);
})

TEST(ast_is_off_by_default, {
    driver_options_t opts;
    driver_options_init(&opts);
    TEST_ASSERT_FALSE(opts.ast);
    driver_options_free(&opts);
})

// --ast stops before the checker and after the lexer, so there is no front
// end for --check to run and no token dump for --tokens to write.
// D14.1
TEST(ast_with_tokens_check_json_or_index_is_a_usage_error, {
    for (size_t i = 0; i < sizeof REJECTED_WITH_AST / sizeof REJECTED_WITH_AST[0]; i++) {
        char* option = (char*)REJECTED_WITH_AST[i];
        const run_t run = RUN("--ast", option, "main.ft");
        TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
        TEST_ASSERT_EQ_STR(run.err, AST_CONFLICT);
        TEST_ASSERT_EQ_SIZE(strlen(run.out), (size_t)0);
        // The order of the two options on the command line does not change
        // the answer.
        const run_t swapped = RUN(option, "--ast", "main.ft");
        TEST_ASSERT_EQ_INT32(swapped.status, FORT_EXIT_USAGE);
        TEST_ASSERT_EQ_STR(swapped.err, AST_CONFLICT);
    }
})

TEST(ast_without_an_entry_file_is_a_usage_error, {
    const run_t run = RUN("--ast");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_STR(run.err,
                       "fort: error: no entry file\n"
                       "usage: fort [options] entry.ft\n");
    TEST_ASSERT_EQ_SIZE(strlen(run.out), (size_t)0);
})

TEST(help_names_ast_with_the_other_options, {
    const run_t run = RUN("--help");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_NONNULL(strstr(run.out, "--ast"));
})

// ---- the tree (toolchain.md 1) ----------------------------------------------------

TEST(ast_writes_the_tree_and_nothing_else, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    const run_t run = RUN_CAPTURED("--ast", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(run.out, SANDBOX_ENTRY_TREE);
    TEST_ASSERT_EQ_SIZE(strlen(run.err), (size_t)0);
    TEST_ASSERT_EQ_STR(last_diags, "");
    sandbox_close(&box);
})

// An empty file is a module with no declaration, which is the shortest tree
// there is; the newline is the dump's and makes the output one line.
TEST(an_empty_file_is_an_empty_module, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, ""));
    const run_t run = RUN("--ast", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(run.out, "(module)\n");
    sandbox_close(&box);
})

// A syntax error is reported and the parser skips to the next boundary, so
// the tree still covers the file -- the error node standing for the skipped
// region -- and the status is the compile error.
// D14.2, D14.1
TEST(a_syntax_error_is_reported_and_the_file_is_still_dumped, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "fn void f() { x = ; }\nfn void g() { }\n"));
    const run_t run = RUN_CAPTURED("--ast", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    TEST_ASSERT_EQ_STR(run.out,
                       "(module (fn (type (void)) f (params) (block (error)))"
                       " (fn (type (void)) g (params) (block)))\n");
    char expected[PATH_CAP + 64];
    expect1(expected,
            sizeof expected,
            "%s:1:19: error: expected an expression, found ';'\n",
            box.entry);
    TEST_ASSERT_EQ_STR(last_diags, expected);
    sandbox_close(&box);
})

// A lexical error costs the lexer its line and the parser recovers from
// whatever that missing line broke, so a file with one is dumped as well; the
// status is again 1.
// D14.2
TEST(a_lexical_error_leaves_a_tree_of_the_rest, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "#\nfn void g() { }\n"));
    const run_t run = RUN_CAPTURED("--ast", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    TEST_ASSERT_EQ_STR(run.out, "(module (fn (type (void)) g (params) (block)))\n");
    char expected[PATH_CAP + 64];
    expect1(expected, sizeof expected, "%s:1:1: error: unexpected character '#'\n", box.entry);
    TEST_ASSERT_EQ_STR(last_diags, expected);
    sandbox_close(&box);
})

// No import is resolved, so a module that no root reaches is not an error
// here: the import is a node of the tree like any other.
// D14.1
TEST(an_unresolvable_import_is_just_a_node, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "import nothere;\n"));
    const run_t run = RUN_CAPTURED("--ast", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(run.out, "(module (import (path nothere) nil))\n");
    TEST_ASSERT_EQ_STR(last_diags, "");
    sandbox_close(&box);
})

// An unreadable entry file is exit 2 with the `cannot read` line of
// toolchain.md 1, as it is for a build.
TEST(an_unreadable_entry_file_is_a_toolchain_error, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char missing[PATH_CAP];
    join(missing, sizeof missing, box.dir, "nothere.ft");
    const run_t run = RUN("--ast", missing);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    char expected[PATH_CAP + 64];
    expect1(expected,
            sizeof expected,
            "fort: error: cannot read '%s': No such file or directory\n",
            missing);
    TEST_ASSERT_EQ_STR(run.err, expected);
    TEST_ASSERT_EQ_SIZE(strlen(run.out), (size_t)0);
    sandbox_close(&box);
})

// ---- what --ast does not do -------------------------------------------------------

// It stops after the parser: no `--cc` is spawned, no output file is written
// and no temporary directory is created, so the options that name them are
// unused, as they are under --check.
// D14.1, D20.1
TEST(ast_spawns_no_cc_and_leaves_no_file_behind, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    const run_t run = RUN("--ast", "--cc", FORT_FAKE_CC, "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(run.out, SANDBOX_ENTRY_TREE);
    TEST_ASSERT_EQ_INT32(access(box.log, F_OK), -1);
    TEST_ASSERT_EQ_INT32(access(box.out, F_OK), -1);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

// The other options are accepted and unused, the ones that would change a
// build included, so a command line an editor already has works.
TEST(the_other_options_are_accepted_and_unused, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    const run_t run = RUN("--ast",
                          "-S",
                          "-c",
                          "--release",
                          "--no-bounds-check",
                          "-I",
                          box.dir,
                          "-lm",
                          "-Xcc",
                          "-v",
                          "--target",
                          "aarch64-linux-gnu",
                          box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(run.out, SANDBOX_ENTRY_TREE);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

// The checker never runs, so a program the checker would refuse -- an
// undeclared name, a type error -- is dumped with no diagnostic at all: what
// --ast answers for is the syntax alone.
// D14.1
TEST(a_program_the_checker_would_refuse_still_dumps, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "fn void f() { nosuch(1); }\n"));
    const run_t run = RUN_CAPTURED("--ast", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(run.out,
                       "(module (fn (type (void)) f (params)"
                       " (block (call-stmt (call (ident nosuch) (int 1))))))\n");
    TEST_ASSERT_EQ_STR(last_diags, "");
    sandbox_close(&box);
})

// Two runs in one process answer alike: the sink is reset at the start of
// each, so the second does not inherit the first's diagnostics.
TEST(a_second_run_starts_from_an_empty_sink, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "fn void f() { x = ; }\n"));
    const run_t first = RUN_CAPTURED("--ast", box.entry);
    TEST_ASSERT_EQ_INT32(first.status, FORT_EXIT_COMPILE_ERROR);
    TEST_ASSERT_TRUE(write_source(box.entry, "fn void f() { }\n"));
    const run_t second = RUN_CAPTURED("--ast", box.entry);
    TEST_ASSERT_EQ_INT32(second.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(second.out, "(module (fn (type (void)) f (params) (block)))\n");
    TEST_ASSERT_EQ_STR(last_diags, "");
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)0);
    sandbox_close(&box);
})

int main(int argc, char** argv) {
    TEST_INIT("driver_ast", argc, argv);
    TEST_RUN(ast_is_parsed_as_a_flag);
    TEST_RUN(ast_is_off_by_default);
    TEST_RUN(ast_with_tokens_check_json_or_index_is_a_usage_error);
    TEST_RUN(ast_without_an_entry_file_is_a_usage_error);
    TEST_RUN(help_names_ast_with_the_other_options);
    TEST_RUN(ast_writes_the_tree_and_nothing_else);
    TEST_RUN(an_empty_file_is_an_empty_module);
    TEST_RUN(a_syntax_error_is_reported_and_the_file_is_still_dumped);
    TEST_RUN(a_lexical_error_leaves_a_tree_of_the_rest);
    TEST_RUN(an_unresolvable_import_is_just_a_node);
    TEST_RUN(an_unreadable_entry_file_is_a_toolchain_error);
    TEST_RUN(ast_spawns_no_cc_and_leaves_no_file_behind);
    TEST_RUN(the_other_options_are_accepted_and_unused);
    TEST_RUN(a_program_the_checker_would_refuse_still_dumps);
    TEST_RUN(a_second_run_starts_from_an_empty_sink);
    TEST_EXIT();
}

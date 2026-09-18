// Unit tests of `--tokens` (toolchain.md 1): the option that lexes the entry
// file alone -- resolving no import, parsing nothing -- and writes one line
// per token to stdout.
// D14.1
//
// The suite holds the lexer output contract: the dump
// is the whole of stdout, a lexical error is reported on stderr and still
// leaves the file dumped whole, and nothing else of the pipeline runs -- no
// `--cc` is spawned, no temporary is created, no import is opened. The
// sandbox is driver_helpers.h and test/fake_cc.sh is the `--cc` that must
// never run.
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

// The dump of the entry file the sandbox writes (driver_helpers.h):
// `fn main() i32 { return 0; }`.
static const char SANDBOX_ENTRY_DUMP[] = "1:1-1:3 0 \"fn\" fn\n"
                                         "1:4-1:8 0 \"main\" identifier\n"
                                         "1:8-1:9 0 \"(\" (\n"
                                         "1:9-1:10 0 \")\" )\n"
                                         "1:11-1:14 0 \"i32\" i32\n"
                                         "1:15-1:16 0 \"{\" {\n"
                                         "1:17-1:23 0 \"return\" return\n"
                                         "1:24-1:25 0 \"0\" integer literal\n"
                                         "1:25-1:26 0 \";\" ;\n"
                                         "1:27-1:28 0 \"}\" }\n"
                                         "2:1-2:1 0 \"\" end of file\n";

// ---- the command line -------------------------------------------------------------
// D14.1

// `fort --tokens main.ft`, parsed without running anything. A brace
// initializer inside a TEST body would split the body into two macro
// arguments, so the argument list stands here; driver_parse takes `char**`,
// so it is a variable and lower_case rather than a constant.
static char* tokens_argv[] = {"fort", "--tokens", "main.ft", NULL};

// The options --tokens refuses to stand beside, and the line it refuses them
// with.
// D14.1
static const char* const REJECTED_WITH_TOKENS[] = {"--check", "--json", "--index"};
static const char TOKENS_CONFLICT[] =
    "fort: error: --tokens does not combine with --check, --json or --index\n"
    "usage: fort [options] entry.ft\n";

TEST(tokens_is_parsed_as_a_flag, {
    driver_options_t opts;
    driver_options_init(&opts);
    int argc = 0;
    while (tokens_argv[argc] != NULL) {
        argc++;
    }
    FILE* out = tmpfile();
    FILE* err = tmpfile();
    TEST_ASSERT_NONNULL(out);
    TEST_ASSERT_NONNULL(err);
    TEST_ASSERT_EQ_INT32(driver_parse(&opts, argc, tokens_argv, out, err), DRIVER_PARSE_OK);
    TEST_ASSERT_TRUE(opts.tokens);
    // It implies nothing: the front end is not run.
    // D14.1
    TEST_ASSERT_FALSE(opts.check);
    TEST_ASSERT_FALSE(opts.json);
    TEST_ASSERT_FALSE(opts.index);
    TEST_ASSERT_EQ_STR(opts.entry, "main.ft");
    TEST_UNUSED(fclose(out));
    TEST_UNUSED(fclose(err));
    driver_options_free(&opts);
})

TEST(tokens_is_off_by_default, {
    driver_options_t opts;
    driver_options_init(&opts);
    TEST_ASSERT_FALSE(opts.tokens);
    driver_options_free(&opts);
})

// --tokens stops before the parser, so there is no front end for --check to
// run and no document for --json to write.
// D14.1
TEST(tokens_with_check_json_or_index_is_a_usage_error, {
    for (size_t i = 0; i < sizeof REJECTED_WITH_TOKENS / sizeof REJECTED_WITH_TOKENS[0]; i++) {
        char* option = (char*)REJECTED_WITH_TOKENS[i];
        const run_t run = RUN("--tokens", option, "main.ft");
        TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
        TEST_ASSERT_EQ_STR(run.err, TOKENS_CONFLICT);
        TEST_ASSERT_EQ_SIZE(strlen(run.out), (size_t)0);
        // The order of the two options on the command line does not change
        // the answer.
        const run_t swapped = RUN(option, "--tokens", "main.ft");
        TEST_ASSERT_EQ_INT32(swapped.status, FORT_EXIT_USAGE);
        TEST_ASSERT_EQ_STR(swapped.err, TOKENS_CONFLICT);
    }
})

TEST(tokens_without_an_entry_file_is_a_usage_error, {
    const run_t run = RUN("--tokens");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_STR(run.err,
                       "fort: error: no entry file\n"
                       "usage: fort [options] entry.ft\n");
    TEST_ASSERT_EQ_SIZE(strlen(run.out), (size_t)0);
})

TEST(help_names_tokens_with_the_other_options, {
    const run_t run = RUN("--help");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_NONNULL(strstr(run.out, "--tokens"));
})

// ---- the dump (toolchain.md 1) ----------------------------------------------------

TEST(tokens_writes_the_dump_and_nothing_else, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    const run_t run = RUN_CAPTURED("--tokens", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(run.out, SANDBOX_ENTRY_DUMP);
    TEST_ASSERT_EQ_SIZE(strlen(run.err), (size_t)0);
    TEST_ASSERT_EQ_STR(last_diags, "");
    sandbox_close(&box);
})

TEST(an_empty_file_dumps_its_end_of_file_token, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, ""));
    const run_t run = RUN("--tokens", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(run.out, "1:1-1:1 0 \"\" end of file\n");
    sandbox_close(&box);
})

// A lexical error is reported and lexing resumes at the next line, so the
// dump still covers the file and the status is the compile error.
// D14.2, D14.1
TEST(a_lexical_error_is_reported_and_the_file_is_still_dumped, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "a\n#\nb\n"));
    const run_t run = RUN_CAPTURED("--tokens", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    TEST_ASSERT_EQ_STR(run.out,
                       "1:1-1:2 0 \"a\" identifier\n"
                       "3:1-3:2 0 \"b\" identifier\n"
                       "4:1-4:1 0 \"\" end of file\n");
    char expected[PATH_CAP + 64];
    expect1(expected, sizeof expected, "%s:2:1: error: unexpected character '#'\n", box.entry);
    TEST_ASSERT_EQ_STR(last_diags, expected);
    sandbox_close(&box);
})

// The file is lexed, not parsed: a file no parser would accept still dumps.
TEST(a_file_that_would_not_parse_still_dumps, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "} fn fn ;\n"));
    const run_t run = RUN_CAPTURED("--tokens", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(run.out,
                       "1:1-1:2 0 \"}\" }\n"
                       "1:3-1:5 0 \"fn\" fn\n"
                       "1:6-1:8 0 \"fn\" fn\n"
                       "1:9-1:10 0 \";\" ;\n"
                       "2:1-2:1 0 \"\" end of file\n");
    TEST_ASSERT_EQ_STR(last_diags, "");
    sandbox_close(&box);
})

// No import is resolved, so a module that no root reaches is not an error
// here: the import is four tokens like any other.
// D14.1
TEST(an_unresolvable_import_is_just_tokens, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "import nothere;\n"));
    const run_t run = RUN_CAPTURED("--tokens", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(run.out,
                       "1:1-1:7 0 \"import\" import\n"
                       "1:8-1:15 0 \"nothere\" identifier\n"
                       "1:15-1:16 0 \";\" ;\n"
                       "2:1-2:1 0 \"\" end of file\n");
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
    const run_t run = RUN("--tokens", missing);
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

// ---- what --tokens does not do ----------------------------------------------------

// It stops after the lexer: no `--cc` is spawned, no output file is written
// and no temporary directory is created, so the options that name them are
// unused, as they are under --check.
// D14.1, D20.1
TEST(tokens_spawns_no_cc_and_leaves_no_file_behind, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    const run_t run = RUN("--tokens", "--cc", FORT_FAKE_CC, "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(run.out, SANDBOX_ENTRY_DUMP);
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
    const run_t run = RUN("--tokens",
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
    TEST_ASSERT_EQ_STR(run.out, SANDBOX_ENTRY_DUMP);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

// Two runs in one process answer alike: the sink is reset at the start of
// each, so the second does not inherit the first's diagnostics.
TEST(a_second_run_starts_from_an_empty_sink, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "#\n"));
    const run_t first = RUN_CAPTURED("--tokens", box.entry);
    TEST_ASSERT_EQ_INT32(first.status, FORT_EXIT_COMPILE_ERROR);
    TEST_ASSERT_TRUE(write_source(box.entry, "x\n"));
    const run_t second = RUN_CAPTURED("--tokens", box.entry);
    TEST_ASSERT_EQ_INT32(second.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(second.out,
                       "1:1-1:2 0 \"x\" identifier\n"
                       "2:1-2:1 0 \"\" end of file\n");
    TEST_ASSERT_EQ_STR(last_diags, "");
    TEST_ASSERT_EQ_UINT64(diag_count(), (uint64_t)0);
    sandbox_close(&box);
})

int main(int argc, char** argv) {
    TEST_INIT("driver_tokens", argc, argv);
    TEST_RUN(tokens_is_parsed_as_a_flag);
    TEST_RUN(tokens_is_off_by_default);
    TEST_RUN(tokens_with_check_json_or_index_is_a_usage_error);
    TEST_RUN(tokens_without_an_entry_file_is_a_usage_error);
    TEST_RUN(help_names_tokens_with_the_other_options);
    TEST_RUN(tokens_writes_the_dump_and_nothing_else);
    TEST_RUN(an_empty_file_dumps_its_end_of_file_token);
    TEST_RUN(a_lexical_error_is_reported_and_the_file_is_still_dumped);
    TEST_RUN(a_file_that_would_not_parse_still_dumps);
    TEST_RUN(an_unresolvable_import_is_just_tokens);
    TEST_RUN(an_unreadable_entry_file_is_a_toolchain_error);
    TEST_RUN(tokens_spawns_no_cc_and_leaves_no_file_behind);
    TEST_RUN(the_other_options_are_accepted_and_unused);
    TEST_RUN(a_second_run_starts_from_an_empty_sink);
    TEST_EXIT();
}

// Unit tests of the driver scaffold: the command line of toolchain.md 1 and the
// exit statuses of D14.1 as far as ticket T-003 implements them.
#include "driver.h"

#include <stdlib.h>

#include "test.h"

enum { CAPTURE_MAX = 1024 };

// One driver run with its captured stdout and stderr.
typedef struct {
    int status;
    char out[CAPTURE_MAX];
    char err[CAPTURE_MAX];
} run_t;

// Reads the whole of a temporary stream into buf as a NUL-terminated string.
static void slurp(FILE* stream, char* buf, size_t size) {
    TEST_UNUSED(fseek(stream, 0, SEEK_SET));
    const size_t got = fread(buf, 1, size - 1, stream);
    buf[got] = '\0';
}

// Runs driver_main on the NULL-terminated argument list, argv[0] included.
static run_t run_driver(char** argv) {
    run_t run;
    run.status = -1;
    run.out[0] = '\0';
    run.err[0] = '\0';
    int argc = 0;
    while (argv[argc] != NULL) {
        argc++;
    }
    FILE* out = tmpfile();
    FILE* err = tmpfile();
    if (out == NULL || err == NULL) {
        return run;
    }
    run.status = driver_main(argc, argv, out, err);
    slurp(out, run.out, sizeof run.out);
    slurp(err, run.err, sizeof run.err);
    TEST_UNUSED(fclose(out));
    TEST_UNUSED(fclose(err));
    return run;
}

#define RUN(...) run_driver((char*[]){"fort", __VA_ARGS__, NULL})

TEST(usage_line_is_the_one_of_toolchain_section_1,
     { TEST_ASSERT_EQ_INT32(strcmp(driver_usage_line(), "usage: fort [options] entry.ft"), 0); })

TEST(no_arguments_prints_usage_to_stderr_and_exits_2, {
    const run_t run = run_driver((char*[]){"fort", NULL});
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_INT32(strcmp(run.err, "usage: fort [options] entry.ft\n"), 0);
    TEST_ASSERT_EQ_SIZE(strlen(run.out), (size_t)0);
})

TEST(help_prints_usage_to_stdout_and_exits_0, {
    const run_t run = RUN("--help");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_INT32(strcmp(run.out, "usage: fort [options] entry.ft\n"), 0);
    TEST_ASSERT_EQ_SIZE(strlen(run.err), (size_t)0);
})

TEST(help_wins_before_a_later_bad_option, {
    const run_t run = RUN("--help", "--bogus");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_INT32(strcmp(run.out, "usage: fort [options] entry.ft\n"), 0);
})

TEST(version_prints_fort_and_the_version, {
    const run_t run = RUN("--version");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_INT32(strcmp(run.out, "fort 0.1.0\n"), 0);
    TEST_ASSERT_EQ_INT32(strcmp(run.out, "fort " FORT_VERSION_STRING "\n"), 0);
    TEST_ASSERT_EQ_SIZE(strlen(run.err), (size_t)0);
})

TEST(version_after_the_entry_file_still_wins, {
    const run_t run = RUN("main.ft", "--version");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_INT32(strcmp(run.out, "fort 0.1.0\n"), 0);
})

TEST(unknown_option_is_a_usage_error, {
    const run_t run = RUN("--bogus", "main.ft");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_INT32(strcmp(run.err,
                                "fort: error: unknown option '--bogus'\n"
                                "usage: fort [options] entry.ft\n"),
                         0);
    TEST_ASSERT_EQ_SIZE(strlen(run.out), (size_t)0);
})

TEST(unknown_short_option_is_a_usage_error, {
    const run_t run = RUN("main.ft", "-x");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_NONNULL(strstr(run.err, "fort: error: unknown option '-x'\n"));
})

TEST(bare_l_is_not_an_option, {
    const run_t run = RUN("-l", "main.ft");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_NONNULL(strstr(run.err, "fort: error: unknown option '-l'\n"));
})

// The options of toolchain.md 1 that take the following argument.
static const char* const OPTIONS_WITH_ARGUMENT[] = {"-o", "-I", "--std-dir", "--cc"};

TEST(option_missing_its_argument_is_a_usage_error, {
    for (size_t i = 0; i < sizeof OPTIONS_WITH_ARGUMENT / sizeof OPTIONS_WITH_ARGUMENT[0]; i++) {
        char* option = (char*)OPTIONS_WITH_ARGUMENT[i];
        const run_t run = RUN("main.ft", option);
        TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
        TEST_ASSERT_NONNULL(strstr(run.err, "fort: error: missing argument for option '"));
        TEST_ASSERT_NONNULL(strstr(run.err, option));
        TEST_ASSERT_NONNULL(strstr(run.err, "usage: fort [options] entry.ft\n"));
    }
})

TEST(option_argument_starting_with_dash_is_consumed, {
    const run_t run = RUN("-o", "-weird", "main.ft");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_INT32(strcmp(run.err, "fort: error: not implemented\n"), 0);
})

TEST(two_entry_files_are_a_usage_error, {
    const run_t run = RUN("a.ft", "b.ft");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_NONNULL(strstr(run.err, "fort: error: unexpected argument 'b.ft'\n"));
})

TEST(options_without_an_entry_file_are_a_usage_error, {
    const run_t run = RUN("-S", "--release");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_INT32(strcmp(run.err,
                                "fort: error: no entry file\n"
                                "usage: fort [options] entry.ft\n"),
                         0);
})

TEST(every_known_option_is_accepted_and_compilation_is_not_implemented, {
    const run_t run = RUN("-o",
                          "prog",
                          "-S",
                          "-c",
                          "-I",
                          "lib",
                          "-I",
                          "vendor",
                          "--std-dir",
                          "std",
                          "--release",
                          "--no-bounds-check",
                          "-lm",
                          "--cc",
                          "x86_64-linux-gnu-gcc",
                          "main.ft");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_INT32(strcmp(run.err, "fort: error: not implemented\n"), 0);
    TEST_ASSERT_EQ_SIZE(strlen(run.out), (size_t)0);
})

TEST(entry_file_alone_is_not_implemented, {
    const run_t run = RUN("main.ft");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_INT32(strcmp(run.err, "fort: error: not implemented\n"), 0);
})

TEST(exit_status_constants_match_d14_1, {
    TEST_ASSERT_EQ_INT32(FORT_EXIT_OK, 0);
    TEST_ASSERT_EQ_INT32(FORT_EXIT_COMPILE_ERROR, 1);
    TEST_ASSERT_EQ_INT32(FORT_EXIT_USAGE, 2);
})

int main(int argc, char** argv) {
    TEST_INIT("driver", argc, argv);
    TEST_RUN(usage_line_is_the_one_of_toolchain_section_1);
    TEST_RUN(no_arguments_prints_usage_to_stderr_and_exits_2);
    TEST_RUN(help_prints_usage_to_stdout_and_exits_0);
    TEST_RUN(help_wins_before_a_later_bad_option);
    TEST_RUN(version_prints_fort_and_the_version);
    TEST_RUN(version_after_the_entry_file_still_wins);
    TEST_RUN(unknown_option_is_a_usage_error);
    TEST_RUN(unknown_short_option_is_a_usage_error);
    TEST_RUN(bare_l_is_not_an_option);
    TEST_RUN(option_missing_its_argument_is_a_usage_error);
    TEST_RUN(option_argument_starting_with_dash_is_consumed);
    TEST_RUN(two_entry_files_are_a_usage_error);
    TEST_RUN(options_without_an_entry_file_are_a_usage_error);
    TEST_RUN(every_known_option_is_accepted_and_compilation_is_not_implemented);
    TEST_RUN(entry_file_alone_is_not_implemented);
    TEST_RUN(exit_status_constants_match_d14_1);
    TEST_EXIT();
}

// Unit tests of the driver: the command line of toolchain.md 1, the pipeline
// of toolchain.md 2 (the temporary directory and the clang invocation of
// D14.3) and the exit statuses of D14.1.
//
// The invocation is asserted through test/fake_cc.sh, which records the whole
// command line the driver spawned instead of compiling it (FORT_FAKE_CC is
// its path, from CMake), so the tests read the argv the compiler would hand
// to clang, argument for argument.
#include "driver.h"

#include <dirent.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "diag.h"
#include "str.h"

#include "test.h"

enum { CAPTURE_MAX = 8192, PATH_CAP = 512, ARGV_CAP = 32 };

// ---- running the driver ---------------------------------------------------------

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

// Parses a command line into `opts` without running anything; the caller has
// initialized `opts` and frees it.
static int parse(driver_options_t* opts, char** argv, run_t* run) {
    int argc = 0;
    while (argv[argc] != NULL) {
        argc++;
    }
    run->status = -1;
    run->out[0] = '\0';
    run->err[0] = '\0';
    FILE* out = tmpfile();
    FILE* err = tmpfile();
    if (out == NULL || err == NULL) {
        return DRIVER_PARSE_ERROR;
    }
    const int parsed = driver_parse(opts, argc, argv, out, err);
    slurp(out, run->out, sizeof run->out);
    slurp(err, run->err, sizeof run->err);
    TEST_UNUSED(fclose(out));
    TEST_UNUSED(fclose(err));
    return parsed;
}

#define PARSE(opts, run, ...) parse((opts), (char*[]){"fort", __VA_ARGS__, NULL}, (run))

// ---- the file-system sandbox of one run ------------------------------------------

// Joins a directory and a name into `dst`.
static void join(char* dst, size_t size, const char* dir, const char* name) {
    TEST_UNUSED(snprintf(dst, size, "%s/%s", dir, name));
}

// Removes a directory and everything below it: the sandbox of one run.
static void remove_tree(const char* path) {
    DIR* dir = opendir(path);
    if (dir == NULL) {
        TEST_UNUSED(unlink(path));
        return;
    }
    const struct dirent* entry = readdir(dir);
    while (entry != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char child[PATH_CAP];
            join(child, sizeof child, path, entry->d_name);
            remove_tree(child);
        }
        entry = readdir(dir);
    }
    TEST_UNUSED(closedir(dir));
    TEST_UNUSED(rmdir(path));
}

// The number of entries in a directory, `.` and `..` apart, or -1 when it
// cannot be read.
static int count_entries(const char* path) {
    DIR* dir = opendir(path);
    if (dir == NULL) {
        return -1;
    }
    int count = 0;
    const struct dirent* entry = readdir(dir);
    while (entry != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            count++;
        }
        entry = readdir(dir);
    }
    TEST_UNUSED(closedir(dir));
    return count;
}

// A private directory for one driver run: the entry file, the fake cc's log
// and the $TMPDIR the driver must leave empty (toolchain.md 2).
typedef struct {
    bool ok;
    char dir[PATH_CAP];
    char tmp[PATH_CAP];   // $TMPDIR of the run
    char log[PATH_CAP];   // where fake_cc.sh records its command line
    char entry[PATH_CAP]; // <dir>/main.ft
    char out[PATH_CAP];   // <dir>/prog, the -o argument
} sandbox_t;

// Clears every variable these tests and the driver read (toolchain.md 1),
// so that a test which returns early on a failed assertion cannot leave one
// behind for the next: each test establishes its own environment first.
static void env_reset(void) {
    TEST_UNUSED(unsetenv("TMPDIR"));
    TEST_UNUSED(unsetenv("FORT_STD_DIR"));
    TEST_UNUSED(unsetenv("FORT_FAKE_CC_LOG"));
    TEST_UNUSED(unsetenv("FORT_FAKE_CC_STATUS"));
    TEST_UNUSED(unsetenv("FORT_FAKE_CC_SIGNAL"));
}

static sandbox_t sandbox_open(void) {
    env_reset();
    sandbox_t box;
    box.ok = false;
    box.dir[0] = '\0';
    char pattern[] = "/tmp/fort-driver-test-XXXXXX";
    if (mkdtemp(pattern) == NULL) {
        return box;
    }
    TEST_UNUSED(snprintf(box.dir, sizeof box.dir, "%s", pattern));
    join(box.tmp, sizeof box.tmp, box.dir, "tmp");
    join(box.log, sizeof box.log, box.dir, "cc.log");
    join(box.entry, sizeof box.entry, box.dir, "main.ft");
    join(box.out, sizeof box.out, box.dir, "prog");
    if (mkdir(box.tmp, S_IRWXU) != 0) {
        return box;
    }
    FILE* entry = fopen(box.entry, "wb");
    if (entry == NULL) {
        return box;
    }
    TEST_UNUSED(fputs("fn i32 main() { return 0; }\n", entry));
    TEST_UNUSED(fclose(entry));
    // The driver reads TMPDIR (toolchain.md 1) and fake_cc.sh reads the
    // FORT_FAKE_CC variables; env_reset above cleared everything else.
    TEST_UNUSED(setenv("TMPDIR", box.tmp, 1));
    TEST_UNUSED(setenv("FORT_FAKE_CC_LOG", box.log, 1));
    box.ok = true;
    return box;
}

// Removes the sandbox. A test that fails an assertion returns before this,
// which is why the next sandbox_open resets the environment rather than
// trusting this one to have run.
static void sandbox_close(sandbox_t* box) {
    env_reset();
    if (box->dir[0] != '\0') {
        remove_tree(box->dir);
    }
}

// ---- the recorded command line ---------------------------------------------------

// What fake_cc.sh wrote: its own path first, then one argument per line.
typedef struct {
    char text[CAPTURE_MAX];     // the file, split into NUL-terminated arguments
    const char* args[ARGV_CAP]; // the arguments, the program name first
    size_t count;
    char joined[CAPTURE_MAX]; // the arguments again, one per line, after masking
    char tmp_dir[PATH_CAP];   // the temporary directory the module was written in
    char mask[PATH_CAP];      // what the module argument was replaced by
} cc_log_t;

// The name a temporary module gets for an entry called main.ft, and the text
// its directory is replaced by, since the directory mkdtemp chose is not
// known in advance.
static const char MODULE_NAME[] = "/main.ll";
static const char MODULE_MASK_DIR[] = "<tmp>";

static bool cc_log_read(const char* path, cc_log_t* log) {
    log->count = 0;
    log->joined[0] = '\0';
    log->tmp_dir[0] = '\0';
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    const size_t got = fread(log->text, 1, sizeof log->text - 1, file);
    TEST_UNUSED(fclose(file));
    log->text[got] = '\0';
    char* cursor = log->text;
    while (*cursor != '\0' && log->count < ARGV_CAP) {
        log->args[log->count] = cursor;
        log->count++;
        char* end = strchr(cursor, '\n');
        if (end == NULL) {
            break;
        }
        *end = '\0';
        cursor = end + 1;
    }
    return true;
}

// Whether `s` ends with `suffix`.
static bool ends_with(const char* s, const char* suffix) {
    const size_t len = strlen(s);
    const size_t tail = strlen(suffix);
    return len >= tail && strcmp(s + len - tail, suffix) == 0;
}

// Replaces the argument naming the temporary module by `<tmp>/<name>` and
// keeps the directory it named, so that the rest of the command line is one
// string comparison. False when no argument ends in `name`.
static bool cc_log_mask_module(cc_log_t* log, const char* name) {
    for (size_t i = 0; i < log->count; i++) {
        if (ends_with(log->args[i], name)) {
            const size_t dir_len = strlen(log->args[i]) - strlen(name);
            if (dir_len >= sizeof log->tmp_dir) {
                return false;
            }
            TEST_UNUSED(memcpy(log->tmp_dir, log->args[i], dir_len));
            log->tmp_dir[dir_len] = '\0';
            TEST_UNUSED(snprintf(log->mask, sizeof log->mask, "%s%s", MODULE_MASK_DIR, name));
            log->args[i] = log->mask;
            return true;
        }
    }
    return false;
}

// The arguments as one newline-terminated line each, the form the expected
// command lines are written in.
static void cc_log_join(cc_log_t* log) {
    size_t used = 0;
    for (size_t i = 0; i < log->count && used + 1 < sizeof log->joined; i++) {
        const int written =
            snprintf(log->joined + used, sizeof log->joined - used, "%s\n", log->args[i]);
        if (written < 0) {
            break;
        }
        used += (size_t)written;
    }
    log->joined[used] = '\0';
}

// Reads the log, masks the module named `name` and joins the arguments.
static bool cc_log_load_named(const char* path, cc_log_t* log, const char* name) {
    if (!cc_log_read(path, log) || !cc_log_mask_module(log, name)) {
        return false;
    }
    cc_log_join(log);
    return true;
}

// The same for the usual entry, main.ft.
static bool cc_log_load(const char* path, cc_log_t* log) {
    return cc_log_load_named(path, log, MODULE_NAME);
}

// The expected command lines and messages hold paths the sandbox chose, so
// they are written as a format taking one or two of them.
static void expect1(char* dst, size_t size, const char* format, const char* a) {
    TEST_UNUSED(snprintf(dst, size, format, a));
}

static void expect2(char* dst, size_t size, const char* format, const char* a, const char* b) {
    TEST_UNUSED(snprintf(dst, size, format, a, b));
}

// ---- the command line (toolchain.md 1) --------------------------------------------

TEST(usage_line_is_the_one_of_toolchain_section_1,
     { TEST_ASSERT_EQ_STR(driver_usage_line(), "usage: fort [options] entry.ft"); })

TEST(no_arguments_prints_usage_to_stderr_and_exits_2, {
    const run_t run = run_driver((char*[]){"fort", NULL});
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_STR(run.err, "usage: fort [options] entry.ft\n");
    TEST_ASSERT_EQ_SIZE(strlen(run.out), (size_t)0);
})

TEST(help_prints_the_usage_line_first_and_exits_0, {
    const run_t run = RUN("--help");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_TRUE(strncmp(run.out,
                             "usage: fort [options] entry.ft\noptions:\n",
                             strlen("usage: fort [options] entry.ft\noptions:\n")) == 0);
    TEST_ASSERT_EQ_SIZE(strlen(run.err), (size_t)0);
})

// Every option of toolchain.md 1; --help lists them all.
static const char* const EVERY_OPTION[] = {"-o <file>",
                                           "-S",
                                           "-c",
                                           "-I <dir>",
                                           "--std-dir <dir>",
                                           "--release",
                                           "--no-bounds-check",
                                           "-l<lib>",
                                           "--cc <path>",
                                           "--target <triple>",
                                           "-Xcc <arg>",
                                           "--help",
                                           "--version"};

TEST(help_lists_every_option_of_the_table, {
    const run_t run = RUN("--help");
    for (size_t i = 0; i < sizeof EVERY_OPTION / sizeof EVERY_OPTION[0]; i++) {
        const char* option = EVERY_OPTION[i];
        TEST_ASSERT_NONNULL(strstr(run.out, option));
    }
})

TEST(help_wins_before_a_later_bad_option, {
    const run_t run = RUN("--help", "--bogus");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_NONNULL(strstr(run.out, "usage: fort [options] entry.ft\n"));
})

TEST(version_prints_fort_and_the_version, {
    const run_t run = RUN("--version");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(run.out, "fort 0.1.0\n");
    TEST_ASSERT_EQ_STR(run.out, "fort " FORT_VERSION_STRING "\n");
    TEST_ASSERT_EQ_SIZE(strlen(run.err), (size_t)0);
})

TEST(version_after_the_entry_file_still_wins, {
    const run_t run = RUN("main.ft", "--version");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_STR(run.out, "fort 0.1.0\n");
})

TEST(unknown_option_is_a_usage_error, {
    const run_t run = RUN("--bogus", "main.ft");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_STR(run.err,
                       "fort: error: unknown option '--bogus'\n"
                       "usage: fort [options] entry.ft\n");
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
static const char* const OPTIONS_WITH_ARGUMENT[] = {
    "-o", "-I", "--std-dir", "--cc", "--target", "-Xcc"};

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

TEST(two_entry_files_are_a_usage_error, {
    const run_t run = RUN("a.ft", "b.ft");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_NONNULL(strstr(run.err, "fort: error: unexpected argument 'b.ft'\n"));
})

TEST(options_without_an_entry_file_are_a_usage_error, {
    const run_t run = RUN("-S", "--release");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_STR(run.err,
                       "fort: error: no entry file\n"
                       "usage: fort [options] entry.ft\n");
})

// ---- parsing (toolchain.md 1) ------------------------------------------------------

TEST(defaults_are_clang_the_x86_64_triple_and_checked_mode, {
    driver_options_t opts;
    driver_options_init(&opts);
    run_t run;
    TEST_ASSERT_EQ_INT32(PARSE(&opts, &run, "main.ft"), DRIVER_PARSE_OK);
    TEST_ASSERT_EQ_STR(opts.entry, "main.ft");
    TEST_ASSERT_NULL(opts.output);
    TEST_ASSERT_NULL(opts.std_dir);
    TEST_ASSERT_EQ_STR(opts.cc, "clang");
    TEST_ASSERT_EQ_STR(opts.target, "x86_64-linux-gnu");
    TEST_ASSERT_FALSE(opts.emit_ir);
    TEST_ASSERT_FALSE(opts.compile_only);
    TEST_ASSERT_FALSE(opts.release);
    TEST_ASSERT_FALSE(opts.no_bounds_check);
    TEST_ASSERT_EQ_UINT64(opts.includes.len, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(opts.libs.len, (uint64_t)0);
    TEST_ASSERT_EQ_UINT64(opts.cc_args.len, (uint64_t)0);
    driver_options_free(&opts);
})

TEST(the_defaults_are_the_ones_the_header_names, {
    TEST_ASSERT_EQ_STR(FORT_DEFAULT_CC, "clang");
    TEST_ASSERT_EQ_STR(FORT_DEFAULT_TARGET, "x86_64-linux-gnu");
    TEST_ASSERT_EQ_STR(FORT_DEFAULT_OUTPUT, "a.out");
    TEST_ASSERT_EQ_STR(FORT_RUNTIME_OBJECT, "fort_rt.o");
})

TEST(every_flag_sets_its_option, {
    driver_options_t opts;
    driver_options_init(&opts);
    run_t run;
    TEST_ASSERT_EQ_INT32(
        PARSE(&opts, &run, "-S", "-c", "--release", "--no-bounds-check", "main.ft"),
        DRIVER_PARSE_OK);
    TEST_ASSERT_TRUE(opts.emit_ir);
    TEST_ASSERT_TRUE(opts.compile_only);
    TEST_ASSERT_TRUE(opts.release);
    TEST_ASSERT_TRUE(opts.no_bounds_check);
    driver_options_free(&opts);
})

TEST(the_last_output_cc_and_target_win, {
    driver_options_t opts;
    driver_options_init(&opts);
    run_t run;
    TEST_ASSERT_EQ_INT32(PARSE(&opts,
                               &run,
                               "-o",
                               "first",
                               "--cc",
                               "clang-17",
                               "--target",
                               "aarch64-linux-gnu",
                               "-o",
                               "last",
                               "--cc",
                               "clang-18",
                               "--target",
                               "x86_64-linux-musl",
                               "main.ft"),
                         DRIVER_PARSE_OK);
    TEST_ASSERT_EQ_STR(opts.output, "last");
    TEST_ASSERT_EQ_STR(opts.cc, "clang-18");
    TEST_ASSERT_EQ_STR(opts.target, "x86_64-linux-musl");
    driver_options_free(&opts);
})

TEST(roots_libraries_and_cc_arguments_keep_their_order, {
    driver_options_t opts;
    driver_options_init(&opts);
    run_t run;
    TEST_ASSERT_EQ_INT32(PARSE(&opts,
                               &run,
                               "-I",
                               "lib",
                               "-lm",
                               "-Xcc",
                               "-fuse-ld=lld",
                               "-I",
                               "vendor",
                               "-lz",
                               "-Xcc",
                               "-static",
                               "main.ft"),
                         DRIVER_PARSE_OK);
    TEST_ASSERT_EQ_UINT64(opts.includes.len, (uint64_t)2);
    TEST_ASSERT_EQ_STR((const char*)opts.includes.items[0], "lib");
    TEST_ASSERT_EQ_STR((const char*)opts.includes.items[1], "vendor");
    TEST_ASSERT_EQ_UINT64(opts.libs.len, (uint64_t)2);
    TEST_ASSERT_EQ_STR((const char*)opts.libs.items[0], "-lm");
    TEST_ASSERT_EQ_STR((const char*)opts.libs.items[1], "-lz");
    TEST_ASSERT_EQ_UINT64(opts.cc_args.len, (uint64_t)2);
    TEST_ASSERT_EQ_STR((const char*)opts.cc_args.items[0], "-fuse-ld=lld");
    TEST_ASSERT_EQ_STR((const char*)opts.cc_args.items[1], "-static");
    driver_options_free(&opts);
})

TEST(an_option_argument_may_start_with_a_dash, {
    driver_options_t opts;
    driver_options_init(&opts);
    run_t run;
    TEST_ASSERT_EQ_INT32(PARSE(&opts, &run, "-o", "-weird", "-Xcc", "-static", "main.ft"),
                         DRIVER_PARSE_OK);
    TEST_ASSERT_EQ_STR(opts.output, "-weird");
    TEST_ASSERT_EQ_STR(opts.entry, "main.ft");
    TEST_ASSERT_EQ_STR((const char*)opts.cc_args.items[0], "-static");
    driver_options_free(&opts);
})

TEST(the_entry_file_may_come_first, {
    driver_options_t opts;
    driver_options_init(&opts);
    run_t run;
    TEST_ASSERT_EQ_INT32(PARSE(&opts, &run, "main.ft", "-o", "prog", "--release"), DRIVER_PARSE_OK);
    TEST_ASSERT_EQ_STR(opts.entry, "main.ft");
    TEST_ASSERT_EQ_STR(opts.output, "prog");
    TEST_ASSERT_TRUE(opts.release);
    driver_options_free(&opts);
})

TEST(help_and_version_stop_the_parse, {
    driver_options_t opts;
    driver_options_init(&opts);
    run_t run;
    TEST_ASSERT_EQ_INT32(PARSE(&opts, &run, "--version", "main.ft"), DRIVER_PARSE_DONE);
    TEST_ASSERT_NULL(opts.entry);
    driver_options_free(&opts);
})

// ---- paths (toolchain.md 1) ---------------------------------------------------------

TEST(the_entry_base_drops_the_directory_and_the_ft_suffix, {
    TEST_ASSERT_TRUE(str_eq(driver_entry_base("main.ft"), str_from_cstr("main")));
    TEST_ASSERT_TRUE(str_eq(driver_entry_base("lib/main.ft"), str_from_cstr("main")));
    TEST_ASSERT_TRUE(str_eq(driver_entry_base("/a/b/c.ft"), str_from_cstr("c")));
    TEST_ASSERT_TRUE(str_eq(driver_entry_base("main"), str_from_cstr("main")));
    TEST_ASSERT_TRUE(str_eq(driver_entry_base("a.ft.ft"), str_from_cstr("a.ft")));
    TEST_ASSERT_TRUE(str_eq(driver_entry_base(".ft"), str_from_cstr(".ft")));
})

TEST(the_default_output_is_a_out_then_base_o_then_base_ll, {
    str_pool_t pool;
    str_pool_init(&pool);
    driver_options_t opts;
    driver_options_init(&opts);
    opts.entry = "lib/main.ft";
    TEST_ASSERT_EQ_STR(driver_default_output(&opts, &pool).ptr, "a.out");
    opts.compile_only = true;
    TEST_ASSERT_EQ_STR(driver_default_output(&opts, &pool).ptr, "main.o");
    opts.emit_ir = true;
    TEST_ASSERT_EQ_STR(driver_default_output(&opts, &pool).ptr, "main.ll");
    opts.compile_only = false;
    TEST_ASSERT_EQ_STR(driver_default_output(&opts, &pool).ptr, "main.ll");
    driver_options_free(&opts);
    str_pool_free(&pool);
})

// `std` beside the running binary, computed from /proc/self/exe the way the
// driver must: the last component of the path replaced by `std`.
static bool std_beside_this_test(char* dst, size_t size) {
    char exe[PATH_CAP];
    const ssize_t len = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (len <= 0) {
        return false;
    }
    exe[len] = '\0';
    char* slash = strrchr(exe, '/');
    if (slash == NULL) {
        return false;
    }
    slash[0] = '\0';
    TEST_UNUSED(snprintf(dst, size, "%s/std", exe));
    return true;
}

TEST(std_beside_a_binary_replaces_its_last_component, {
    str_pool_t pool;
    str_pool_init(&pool);
    TEST_ASSERT_EQ_STR(driver_std_dir_beside("/usr/local/bin/fort", &pool).ptr,
                       "/usr/local/bin/std");
    TEST_ASSERT_EQ_STR(driver_std_dir_beside("build/debug/fort", &pool).ptr, "build/debug/std");
    // Without a directory part there is nothing to be beside: the last
    // resort, when /proc is not there either, is the current directory.
    TEST_ASSERT_EQ_STR(driver_std_dir_beside("fort", &pool).ptr, "std");
    str_pool_free(&pool);
})

TEST(the_std_dir_option_beats_the_environment_which_beats_the_binary, {
    env_reset();
    str_pool_t pool;
    str_pool_init(&pool);
    driver_options_t opts;
    driver_options_init(&opts);
    opts.entry = "main.ft";
    opts.std_dir = "/opt/fort/std";
    TEST_UNUSED(setenv("FORT_STD_DIR", "/env/std", 1));
    TEST_ASSERT_EQ_STR(driver_std_dir(&opts, "build/debug/fort", &pool).ptr, "/opt/fort/std");
    opts.std_dir = NULL;
    TEST_ASSERT_EQ_STR(driver_std_dir(&opts, "build/debug/fort", &pool).ptr, "/env/std");
    TEST_UNUSED(unsetenv("FORT_STD_DIR"));
    // With neither, the directory containing the binary, which /proc/self/exe
    // names whatever argv[0] is (D14.1).
    char expected[PATH_CAP];
    TEST_ASSERT_TRUE(std_beside_this_test(expected, sizeof expected));
    TEST_ASSERT_EQ_STR(driver_std_dir(&opts, "fort", &pool).ptr, expected);
    TEST_ASSERT_EQ_STR(driver_std_dir(&opts, "/nowhere/fort", &pool).ptr, expected);
    driver_options_free(&opts);
    str_pool_free(&pool);
})

// ---- the clang invocation (D14.3, toolchain.md 2) ------------------------------------

// The command line driver_cc_argv built, one argument per line.
static void argv_lines(const ptrvec_t* argv, char* buf, size_t size) {
    size_t used = 0;
    for (uint64_t i = 0; i + 1 < argv->len && used + 1 < size; i++) {
        const int written = snprintf(buf + used, size - used, "%s\n", (const char*)argv->items[i]);
        if (written < 0) {
            break;
        }
        used += (size_t)written;
    }
    buf[used] = '\0';
}

// Builds the invocation for a command line, with a fixed module and output.
static void build_argv(char** command, char* buf, size_t size) {
    driver_options_t opts;
    driver_options_init(&opts);
    run_t run;
    ptrvec_t argv;
    ptrvec_init(&argv);
    str_pool_t pool;
    str_pool_init(&pool);
    if (parse(&opts, command, &run) == DRIVER_PARSE_OK) {
        driver_cc_argv(&opts, "/tmp/t/main.ll", "prog", "/std", &pool, &argv);
        argv_lines(&argv, buf, size);
    } else {
        TEST_UNUSED(snprintf(buf, size, "parse error: %s", run.err));
    }
    ptrvec_free(&argv);
    str_pool_free(&pool);
    driver_options_free(&opts);
}

#define BUILD_ARGV(buf, ...) build_argv((char*[]){"fort", __VA_ARGS__, NULL}, (buf), sizeof(buf))

TEST(the_checked_invocation_is_the_line_of_toolchain_2, {
    char line[CAPTURE_MAX];
    BUILD_ARGV(line, "main.ft");
    TEST_ASSERT_EQ_STR(line,
                       "clang\n"
                       "--target=x86_64-linux-gnu\n"
                       "-O1\n"
                       "-fPIE\n"
                       "-pie\n"
                       "-Wno-override-module\n"
                       "-o\n"
                       "prog\n"
                       "/tmp/t/main.ll\n"
                       "/std/fort_rt.o\n");
})

TEST(release_compiles_the_module_with_o2, {
    char line[CAPTURE_MAX];
    BUILD_ARGV(line, "--release", "main.ft");
    TEST_ASSERT_NONNULL(strstr(line, "\n-O2\n"));
    TEST_ASSERT_NULL(strstr(line, "\n-O1\n"));
})

TEST(the_target_option_becomes_the_target_argument, {
    char line[CAPTURE_MAX];
    BUILD_ARGV(line, "--target", "x86_64-linux-musl", "--cc", "clang-18", "main.ft");
    TEST_ASSERT_TRUE(strncmp(line,
                             "clang-18\n--target=x86_64-linux-musl\n",
                             strlen("clang-18\n--target=x86_64-linux-musl\n")) == 0);
})

TEST(libraries_then_cc_arguments_close_the_line, {
    char line[CAPTURE_MAX];
    BUILD_ARGV(line, "-lm", "-Xcc", "-fuse-ld=lld", "-lz", "-Xcc", "-static", "main.ft");
    TEST_ASSERT_EQ_STR(line,
                       "clang\n"
                       "--target=x86_64-linux-gnu\n"
                       "-O1\n"
                       "-fPIE\n"
                       "-pie\n"
                       "-Wno-override-module\n"
                       "-o\n"
                       "prog\n"
                       "/tmp/t/main.ll\n"
                       "/std/fort_rt.o\n"
                       "-lm\n"
                       "-lz\n"
                       "-fuse-ld=lld\n"
                       "-static\n");
})

TEST(compile_only_puts_c_before_o_and_drops_the_link_arguments, {
    char line[CAPTURE_MAX];
    BUILD_ARGV(line, "-c", "-lm", "-Xcc", "-static", "main.ft");
    TEST_ASSERT_EQ_STR(line,
                       "clang\n"
                       "--target=x86_64-linux-gnu\n"
                       "-O1\n"
                       "-fPIE\n"
                       "-Wno-override-module\n"
                       "-c\n"
                       "-o\n"
                       "prog\n"
                       "/tmp/t/main.ll\n"
                       "-static\n");
})

TEST(the_module_is_never_introduced_by_x_ir, {
    char line[CAPTURE_MAX];
    BUILD_ARGV(line, "main.ft");
    TEST_ASSERT_NULL(strstr(line, "\n-x\n"));
})

// ---- the pipeline end to end (toolchain.md 2) ----------------------------------------

TEST(a_checked_build_spawns_the_invocation_and_removes_the_temporary, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    const run_t run = RUN("--cc", FORT_FAKE_CC, "--std-dir", "/std", "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_SIZE(strlen(run.err), (size_t)0);
    cc_log_t log;
    TEST_ASSERT_TRUE(cc_log_load(box.log, &log));
    char expected[CAPTURE_MAX];
    expect2(expected,
            sizeof expected,
            "%s\n"
            "--target=x86_64-linux-gnu\n"
            "-O1\n"
            "-fPIE\n"
            "-pie\n"
            "-Wno-override-module\n"
            "-o\n"
            "%s\n"
            "<tmp>/main.ll\n"
            "/std/fort_rt.o\n",
            FORT_FAKE_CC,
            box.out);
    TEST_ASSERT_EQ_STR(log.joined, expected);
    // The module lived in a directory of its own under $TMPDIR, and both are
    // gone once the driver returns (toolchain.md 2).
    TEST_ASSERT_TRUE(strncmp(log.tmp_dir, box.tmp, strlen(box.tmp)) == 0);
    TEST_ASSERT_EQ_INT32(access(log.tmp_dir, F_OK), -1);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

TEST(a_release_build_passes_o2_the_target_the_libraries_and_the_cc_arguments, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    const run_t run = RUN("--release",
                          "--no-bounds-check",
                          "-I",
                          "lib",
                          "--cc",
                          FORT_FAKE_CC,
                          "--target",
                          "x86_64-linux-musl",
                          "--std-dir",
                          "/std",
                          "-lm",
                          "-Xcc",
                          "-fuse-ld=lld",
                          "-o",
                          box.out,
                          box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    cc_log_t log;
    TEST_ASSERT_TRUE(cc_log_load(box.log, &log));
    char expected[CAPTURE_MAX];
    expect2(expected,
            sizeof expected,
            "%s\n"
            "--target=x86_64-linux-musl\n"
            "-O2\n"
            "-fPIE\n"
            "-pie\n"
            "-Wno-override-module\n"
            "-o\n"
            "%s\n"
            "<tmp>/main.ll\n"
            "/std/fort_rt.o\n"
            "-lm\n"
            "-fuse-ld=lld\n",
            FORT_FAKE_CC,
            box.out);
    TEST_ASSERT_EQ_STR(log.joined, expected);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

TEST(compile_only_stops_at_the_object_and_names_it_after_the_entry, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    const run_t run =
        RUN("-c", "--cc", FORT_FAKE_CC, "--std-dir", "/std", "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    cc_log_t log;
    TEST_ASSERT_TRUE(cc_log_load(box.log, &log));
    char expected[CAPTURE_MAX];
    expect2(expected,
            sizeof expected,
            "%s\n"
            "--target=x86_64-linux-gnu\n"
            "-O1\n"
            "-fPIE\n"
            "-Wno-override-module\n"
            "-c\n"
            "-o\n"
            "%s\n"
            "<tmp>/main.ll\n",
            FORT_FAKE_CC,
            box.out);
    TEST_ASSERT_EQ_STR(log.joined, expected);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

TEST(the_std_dir_environment_variable_locates_the_runtime_object, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_UNUSED(setenv("FORT_STD_DIR", "/env/std", 1));
    const run_t run = RUN("--cc", FORT_FAKE_CC, "-o", box.out, box.entry);
    TEST_UNUSED(unsetenv("FORT_STD_DIR"));
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    cc_log_t log;
    TEST_ASSERT_TRUE(cc_log_load(box.log, &log));
    TEST_ASSERT_NONNULL(strstr(log.joined, "\n/env/std/fort_rt.o\n"));
    sandbox_close(&box);
})

TEST(emitting_the_module_stops_before_the_compiler, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char module[PATH_CAP];
    join(module, sizeof module, box.dir, "out.ll");
    const run_t run = RUN("-S", "--cc", FORT_FAKE_CC, "-o", module, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_INT32(access(module, F_OK), 0);
    // -S needs no temporary and runs no compiler (toolchain.md 2).
    TEST_ASSERT_EQ_INT32(access(box.log, F_OK), -1);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

TEST(the_default_module_is_named_after_the_entry_in_the_current_directory, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char cwd[PATH_CAP];
    TEST_ASSERT_NONNULL(getcwd(cwd, sizeof cwd));
    TEST_ASSERT_EQ_INT32(chdir(box.dir), 0);
    const run_t run = RUN("-S", "-c", "--cc", FORT_FAKE_CC, "main.ft");
    const int back = chdir(cwd);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_INT32(back, 0);
    char module[PATH_CAP];
    join(module, sizeof module, box.dir, "main.ll");
    TEST_ASSERT_EQ_INT32(access(module, F_OK), 0);
    TEST_ASSERT_EQ_INT32(access(box.log, F_OK), -1);
    sandbox_close(&box);
})

TEST(a_failing_compiler_is_exit_2_and_still_removes_the_temporary, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_UNUSED(setenv("FORT_FAKE_CC_STATUS", "3", 1));
    const run_t run = RUN("--cc", FORT_FAKE_CC, "--std-dir", "/std", "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_STR(run.err, "fort: error: cc failed with status 3\n");
    cc_log_t log;
    TEST_ASSERT_TRUE(cc_log_load(box.log, &log));
    TEST_ASSERT_EQ_INT32(access(log.tmp_dir, F_OK), -1);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

TEST(a_compiler_killed_by_a_signal_is_exit_2_and_says_so, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_UNUSED(setenv("FORT_FAKE_CC_SIGNAL", "TERM", 1));
    const run_t run = RUN("--cc", FORT_FAKE_CC, "--std-dir", "/std", "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    char expected[CAPTURE_MAX];
    TEST_UNUSED(
        snprintf(expected, sizeof expected, "fort: error: cc failed with signal %d\n", SIGTERM));
    TEST_ASSERT_EQ_STR(run.err, expected);
    cc_log_t log;
    TEST_ASSERT_TRUE(cc_log_load(box.log, &log));
    TEST_ASSERT_EQ_INT32(access(log.tmp_dir, F_OK), -1);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

TEST(an_empty_tmpdir_variable_counts_as_unset, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_UNUSED(setenv("TMPDIR", "", 1));
    const run_t run = RUN("--cc", FORT_FAKE_CC, "--std-dir", "/std", "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    cc_log_t log;
    TEST_ASSERT_TRUE(cc_log_load(box.log, &log));
    TEST_ASSERT_TRUE(strncmp(log.tmp_dir, "/tmp/fort-", strlen("/tmp/fort-")) == 0);
    TEST_ASSERT_EQ_INT32(access(log.tmp_dir, F_OK), -1);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

TEST(an_entry_without_the_ft_suffix_keeps_its_whole_name, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char entry[PATH_CAP];
    join(entry, sizeof entry, box.dir, "prog.src");
    FILE* source = fopen(entry, "wb");
    TEST_ASSERT_NONNULL(source);
    TEST_UNUSED(fputs("fn i32 main() { return 0; }\n", source));
    TEST_UNUSED(fclose(source));
    // Only a `.ft` suffix is dropped from the base name (toolchain.md 1).
    const run_t run = RUN("--cc", FORT_FAKE_CC, "--std-dir", "/std", "-o", box.out, entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    cc_log_t log;
    TEST_ASSERT_TRUE(cc_log_load_named(box.log, &log, "/prog.src.ll"));
    char expected[CAPTURE_MAX];
    expect2(expected,
            sizeof expected,
            "%s\n"
            "--target=x86_64-linux-gnu\n"
            "-O1\n"
            "-fPIE\n"
            "-pie\n"
            "-Wno-override-module\n"
            "-o\n"
            "%s\n"
            "<tmp>/prog.src.ll\n"
            "/std/fort_rt.o\n",
            FORT_FAKE_CC,
            box.out);
    TEST_ASSERT_EQ_STR(log.joined, expected);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

TEST(the_default_module_of_an_entry_without_a_suffix_keeps_its_whole_name, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char entry[PATH_CAP];
    join(entry, sizeof entry, box.dir, "prog.src");
    FILE* source = fopen(entry, "wb");
    TEST_ASSERT_NONNULL(source);
    TEST_UNUSED(fputs("fn i32 main() { return 0; }\n", source));
    TEST_UNUSED(fclose(source));
    char cwd[PATH_CAP];
    TEST_ASSERT_NONNULL(getcwd(cwd, sizeof cwd));
    TEST_ASSERT_EQ_INT32(chdir(box.dir), 0);
    const run_t run = RUN("-S", "prog.src");
    const int back = chdir(cwd);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_INT32(back, 0);
    char module[PATH_CAP];
    join(module, sizeof module, box.dir, "prog.src.ll");
    TEST_ASSERT_EQ_INT32(access(module, F_OK), 0);
    sandbox_close(&box);
})

TEST(a_compiler_that_cannot_be_run_is_exit_2, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    const run_t run =
        RUN("--cc", "/nonexistent/clang", "--std-dir", "/std", "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_STR(run.err,
                       "fort: error: cannot run '/nonexistent/clang': "
                       "No such file or directory\n");
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

TEST(an_unreadable_entry_file_is_exit_2_and_removes_the_temporary, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char missing[PATH_CAP];
    join(missing, sizeof missing, box.dir, "gone.ft");
    const run_t run = RUN("--cc", FORT_FAKE_CC, "-o", box.out, missing);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    char expected[CAPTURE_MAX];
    expect1(expected,
            sizeof expected,
            "fort: error: cannot read '%s': No such file or directory\n",
            missing);
    TEST_ASSERT_EQ_STR(run.err, expected);
    TEST_ASSERT_EQ_INT32(access(box.log, F_OK), -1);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

TEST(the_object_of_c_is_named_after_the_entry_in_the_current_directory, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    // The entry lives in the sandbox, but the default output is placed in the
    // current directory as cc does (toolchain.md 1).
    const run_t run = RUN("-c", "--cc", FORT_FAKE_CC, "--std-dir", "/std", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    cc_log_t log;
    TEST_ASSERT_TRUE(cc_log_load(box.log, &log));
    char expected[CAPTURE_MAX];
    expect1(expected,
            sizeof expected,
            "%s\n"
            "--target=x86_64-linux-gnu\n"
            "-O1\n"
            "-fPIE\n"
            "-Wno-override-module\n"
            "-c\n"
            "-o\n"
            "main.o\n"
            "<tmp>/main.ll\n",
            FORT_FAKE_CC);
    TEST_ASSERT_EQ_STR(log.joined, expected);
    sandbox_close(&box);
})

TEST(without_tmpdir_the_temporary_goes_under_slash_tmp, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_UNUSED(unsetenv("TMPDIR"));
    const run_t run = RUN("--cc", FORT_FAKE_CC, "--std-dir", "/std", "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    cc_log_t log;
    TEST_ASSERT_TRUE(cc_log_load(box.log, &log));
    TEST_ASSERT_TRUE(strncmp(log.tmp_dir, "/tmp/fort-", strlen("/tmp/fort-")) == 0);
    TEST_ASSERT_EQ_INT32(access(log.tmp_dir, F_OK), -1);
    sandbox_close(&box);
})

TEST(an_unusable_tmpdir_is_exit_2_before_the_compiler_runs, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char missing[PATH_CAP];
    join(missing, sizeof missing, box.dir, "gone");
    TEST_UNUSED(setenv("TMPDIR", missing, 1));
    const run_t run = RUN("--cc", FORT_FAKE_CC, "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    char expected[CAPTURE_MAX];
    expect1(expected,
            sizeof expected,
            "fort: error: cannot create a temporary directory in '%s': "
            "No such file or directory\n",
            missing);
    TEST_ASSERT_EQ_STR(run.err, expected);
    TEST_ASSERT_EQ_INT32(access(box.log, F_OK), -1);
    sandbox_close(&box);
})

TEST(an_unwritable_module_path_is_exit_2, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    char module[PATH_CAP];
    join(module, sizeof module, box.dir, "gone/out.ll");
    const run_t run = RUN("-S", "-o", module, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    char expected[CAPTURE_MAX];
    expect1(expected,
            sizeof expected,
            "fort: error: cannot write '%s': No such file or directory\n",
            module);
    TEST_ASSERT_EQ_STR(run.err, expected);
    sandbox_close(&box);
})

TEST(an_empty_std_dir_variable_counts_as_unset, {
    env_reset();
    str_pool_t pool;
    str_pool_init(&pool);
    driver_options_t opts;
    driver_options_init(&opts);
    opts.entry = "main.ft";
    char expected[PATH_CAP];
    TEST_ASSERT_TRUE(std_beside_this_test(expected, sizeof expected));
    TEST_UNUSED(setenv("FORT_STD_DIR", "", 1));
    TEST_ASSERT_EQ_STR(driver_std_dir(&opts, "bin/fort", &pool).ptr, expected);
    TEST_UNUSED(unsetenv("FORT_STD_DIR"));
    driver_options_free(&opts);
    str_pool_free(&pool);
})

TEST(an_option_swallowing_the_entry_file_leaves_none, {
    const run_t run = RUN("-Xcc", "main.ft");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_STR(run.err,
                       "fort: error: no entry file\n"
                       "usage: fort [options] entry.ft\n");
})

TEST(a_usage_error_creates_no_temporary_at_all, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    const run_t run = RUN("--bogus", box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_USAGE);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    TEST_ASSERT_EQ_INT32(access(box.log, F_OK), -1);
    sandbox_close(&box);
})

TEST(exit_status_constants_match_d14_1, {
    TEST_ASSERT_EQ_INT32(FORT_EXIT_OK, 0);
    TEST_ASSERT_EQ_INT32(FORT_EXIT_COMPILE_ERROR, 1);
    TEST_ASSERT_EQ_INT32(FORT_EXIT_USAGE, 2);
})

// ---- the front end (toolchain.md 2, steps 1 and 2) -------------------------------

// The compile-time diagnostics of D14.2 go to stderr, not to the stream
// driver_main writes its `fort: error:` lines to, so a test that wants them
// captures them with diag_capture; the run is otherwise an ordinary one.
static char last_diags[CAPTURE_MAX];

static run_t run_and_capture(char** argv) {
    sb_t sink;
    sb_init(&sink);
    diag_capture(&sink);
    const run_t run = run_driver(argv);
    diag_capture(NULL);
    TEST_UNUSED(snprintf(last_diags, sizeof last_diags, "%s", sb_cstr(&sink)));
    sb_free(&sink);
    return run;
}

#define RUN_CAPTURED(...) run_and_capture((char*[]){"fort", __VA_ARGS__, NULL})

// Writes a source file, replacing what the sandbox put there.
static bool write_source(const char* path, const char* text) {
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    TEST_UNUSED(fputs(text, file));
    TEST_UNUSED(fclose(file));
    return true;
}

TEST(an_import_no_root_reaches_is_a_compile_error, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "import nothere;\nfn i32 main() { return 0; }\n"));
    const run_t run = RUN_CAPTURED("--cc", FORT_FAKE_CC, "-o", box.out, box.entry);
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_COMPILE_ERROR);
    TEST_ASSERT_NONNULL(strstr(last_diags, "module 'nothere' not found"));
    TEST_ASSERT_EQ_INT32(access(box.log, F_OK), -1);
    TEST_ASSERT_EQ_INT32(count_entries(box.tmp), 0);
    sandbox_close(&box);
})

TEST(a_module_beside_the_entry_file_is_reached_without_any_option, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "import util;\nfn i32 main() { return 0; }\n"));
    char util[PATH_CAP];
    join(util, sizeof util, box.dir, "util.ft");
    TEST_ASSERT_TRUE(write_source(util, "fn i32 add(i32 a, i32 b) { return a + b; }\n"));
    // The entry file's directory is always a search root (D9.2).
    const run_t run = RUN_CAPTURED("--cc", FORT_FAKE_CC, "-o", box.out, box.entry);
    TEST_ASSERT_EQ_STR(last_diags, "");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    TEST_ASSERT_EQ_INT32(access(box.log, F_OK), 0);
    sandbox_close(&box);
})

TEST(an_include_root_reaches_a_module_the_entry_directory_lacks, {
    sandbox_t box = sandbox_open();
    TEST_ASSERT_TRUE(box.ok);
    TEST_ASSERT_TRUE(write_source(box.entry, "import util;\nfn i32 main() { return 0; }\n"));
    char lib[PATH_CAP];
    join(lib, sizeof lib, box.dir, "lib");
    TEST_ASSERT_EQ_INT32(mkdir(lib, S_IRWXU), 0);
    char util[PATH_CAP];
    join(util, sizeof util, lib, "util.ft");
    TEST_ASSERT_TRUE(write_source(util, "fn i32 add(i32 a, i32 b) { return a + b; }\n"));
    // A `-I` root is searched after the entry file's directory (D9.2).
    const run_t run = RUN_CAPTURED("--cc", FORT_FAKE_CC, "-I", lib, "-o", box.out, box.entry);
    TEST_ASSERT_EQ_STR(last_diags, "");
    TEST_ASSERT_EQ_INT32(run.status, FORT_EXIT_OK);
    sandbox_close(&box);
})

int main(int argc, char** argv) {
    TEST_INIT("driver", argc, argv);
    TEST_RUN(usage_line_is_the_one_of_toolchain_section_1);
    TEST_RUN(no_arguments_prints_usage_to_stderr_and_exits_2);
    TEST_RUN(help_prints_the_usage_line_first_and_exits_0);
    TEST_RUN(help_lists_every_option_of_the_table);
    TEST_RUN(help_wins_before_a_later_bad_option);
    TEST_RUN(version_prints_fort_and_the_version);
    TEST_RUN(version_after_the_entry_file_still_wins);
    TEST_RUN(unknown_option_is_a_usage_error);
    TEST_RUN(unknown_short_option_is_a_usage_error);
    TEST_RUN(bare_l_is_not_an_option);
    TEST_RUN(option_missing_its_argument_is_a_usage_error);
    TEST_RUN(two_entry_files_are_a_usage_error);
    TEST_RUN(options_without_an_entry_file_are_a_usage_error);
    TEST_RUN(defaults_are_clang_the_x86_64_triple_and_checked_mode);
    TEST_RUN(the_defaults_are_the_ones_the_header_names);
    TEST_RUN(every_flag_sets_its_option);
    TEST_RUN(the_last_output_cc_and_target_win);
    TEST_RUN(roots_libraries_and_cc_arguments_keep_their_order);
    TEST_RUN(an_option_argument_may_start_with_a_dash);
    TEST_RUN(the_entry_file_may_come_first);
    TEST_RUN(help_and_version_stop_the_parse);
    TEST_RUN(the_entry_base_drops_the_directory_and_the_ft_suffix);
    TEST_RUN(the_default_output_is_a_out_then_base_o_then_base_ll);
    TEST_RUN(std_beside_a_binary_replaces_its_last_component);
    TEST_RUN(the_std_dir_option_beats_the_environment_which_beats_the_binary);
    TEST_RUN(the_checked_invocation_is_the_line_of_toolchain_2);
    TEST_RUN(release_compiles_the_module_with_o2);
    TEST_RUN(the_target_option_becomes_the_target_argument);
    TEST_RUN(libraries_then_cc_arguments_close_the_line);
    TEST_RUN(compile_only_puts_c_before_o_and_drops_the_link_arguments);
    TEST_RUN(the_module_is_never_introduced_by_x_ir);
    TEST_RUN(a_checked_build_spawns_the_invocation_and_removes_the_temporary);
    TEST_RUN(a_release_build_passes_o2_the_target_the_libraries_and_the_cc_arguments);
    TEST_RUN(compile_only_stops_at_the_object_and_names_it_after_the_entry);
    TEST_RUN(the_std_dir_environment_variable_locates_the_runtime_object);
    TEST_RUN(emitting_the_module_stops_before_the_compiler);
    TEST_RUN(the_default_module_is_named_after_the_entry_in_the_current_directory);
    TEST_RUN(a_failing_compiler_is_exit_2_and_still_removes_the_temporary);
    TEST_RUN(a_compiler_killed_by_a_signal_is_exit_2_and_says_so);
    TEST_RUN(an_empty_tmpdir_variable_counts_as_unset);
    TEST_RUN(an_entry_without_the_ft_suffix_keeps_its_whole_name);
    TEST_RUN(the_default_module_of_an_entry_without_a_suffix_keeps_its_whole_name);
    TEST_RUN(a_compiler_that_cannot_be_run_is_exit_2);
    TEST_RUN(an_unreadable_entry_file_is_exit_2_and_removes_the_temporary);
    TEST_RUN(the_object_of_c_is_named_after_the_entry_in_the_current_directory);
    TEST_RUN(without_tmpdir_the_temporary_goes_under_slash_tmp);
    TEST_RUN(an_unusable_tmpdir_is_exit_2_before_the_compiler_runs);
    TEST_RUN(an_unwritable_module_path_is_exit_2);
    TEST_RUN(an_empty_std_dir_variable_counts_as_unset);
    TEST_RUN(an_option_swallowing_the_entry_file_leaves_none);
    TEST_RUN(a_usage_error_creates_no_temporary_at_all);
    TEST_RUN(exit_status_constants_match_d14_1);
    TEST_RUN(an_import_no_root_reaches_is_a_compile_error);
    TEST_RUN(a_module_beside_the_entry_file_is_reached_without_any_option);
    TEST_RUN(an_include_root_reaches_a_module_the_entry_directory_lacks);
    TEST_EXIT();
}

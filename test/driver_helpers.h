// The environment the driver suites share: one run of driver_main with its
// captured stdout and stderr, the file-system sandbox a run that touches the
// disk needs (the entry file, the fake cc's log and its own $TMPDIR), the
// capture of the compile-time diagnostics, and the two formatters that fill a
// sandbox path into an expected text.
// D14.2
//
// The helpers are static inline and every path the sandbox holds is per
// suite, so a suite that uses only some of them still builds under -Werror.
#ifndef FORT_TEST_DRIVER_HELPERS_H
#define FORT_TEST_DRIVER_HELPERS_H

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "diag.h"
#include "driver.h"
#include "str.h"

#include "common.h"

enum { CAPTURE_MAX = 8192, PATH_CAP = 512 };

// ---- running the driver ---------------------------------------------------------

/// One driver run with its captured stdout and stderr.
typedef struct {
    int status;
    char out[CAPTURE_MAX];
    char err[CAPTURE_MAX];
} run_t;

/// Reads the whole of a temporary stream into buf as a NUL-terminated string.
static inline void slurp(FILE* stream, char* buf, size_t size) {
    TEST_UNUSED(fseek(stream, 0, SEEK_SET));
    const size_t got = fread(buf, 1, size - 1, stream);
    buf[got] = '\0';
}

/// Runs driver_main on the NULL-terminated argument list, argv[0] included.
static inline run_t run_driver(char** argv) {
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

// ---- the file-system sandbox of one run ------------------------------------------

/// Joins a directory and a name into `dst`.
static inline void join(char* dst, size_t size, const char* dir, const char* name) {
    TEST_UNUSED(snprintf(dst, size, "%s/%s", dir, name));
}

/// Removes a directory and everything below it: the sandbox of one run.
static inline void remove_tree(const char* path) {
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

/// The number of entries in a directory, `.` and `..` apart, or -1 when it
/// cannot be read.
static inline int count_entries(const char* path) {
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

/// A private directory for one driver run: the entry file, the fake cc's log
/// and the $TMPDIR the driver must leave empty (toolchain.md 2).
typedef struct {
    bool ok;
    char dir[PATH_CAP];
    char tmp[PATH_CAP];   // $TMPDIR of the run
    char log[PATH_CAP];   // where fake_cc.sh records its command line
    char entry[PATH_CAP]; // <dir>/main.ft
    char out[PATH_CAP];   // <dir>/prog, the -o argument
    char std[PATH_CAP];   // <dir>/std, the standard library directory
    char rt[PATH_CAP];    // <dir>/std/rt.ft, the runtime every closure holds
} sandbox_t;

/// The runtime file of the sandbox. Every closure holds `std.rt`, so a run
/// that reads a program reads this file too and lists it first in the
/// document. It is deliberately empty: these suites drive the driver, and a
/// `--check` needs the file to parse while a build with fake_cc.sh never
/// compiles the module, so an empty runtime keeps every expected document
/// short and adds no index record of its own.
/// D9.10, D20.2, D20.3
static const char SANDBOX_RUNTIME[] = "// The empty runtime of a driver test.\n";

/// Clears every variable these tests and the driver read (toolchain.md 1),
/// so that a test which returns early on a failed assertion cannot leave one
/// behind for the next: each test establishes its own environment first.
static inline void env_reset(void) {
    TEST_UNUSED(unsetenv("TMPDIR"));
    TEST_UNUSED(unsetenv("FORT_STD_DIR"));
    TEST_UNUSED(unsetenv("FORT_FAKE_CC_LOG"));
    TEST_UNUSED(unsetenv("FORT_FAKE_CC_STATUS"));
    TEST_UNUSED(unsetenv("FORT_FAKE_CC_SIGNAL"));
}

static inline sandbox_t sandbox_open(void) {
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
    join(box.std, sizeof box.std, box.dir, "std");
    join(box.rt, sizeof box.rt, box.std, "rt.ft");
    if (mkdir(box.tmp, S_IRWXU) != 0 || mkdir(box.std, S_IRWXU) != 0) {
        return box;
    }
    FILE* entry = fopen(box.entry, "wb");
    if (entry == NULL) {
        return box;
    }
    TEST_UNUSED(fputs("fn main() i32 { return 0; }\n", entry));
    TEST_UNUSED(fclose(entry));
    FILE* runtime = fopen(box.rt, "wb");
    if (runtime == NULL) {
        return box;
    }
    TEST_UNUSED(fputs(SANDBOX_RUNTIME, runtime));
    TEST_UNUSED(fclose(runtime));
    // The driver reads TMPDIR and FORT_STD_DIR (toolchain.md 1) and
    // fake_cc.sh reads the FORT_FAKE_CC variables; env_reset above cleared
    // everything else.
    TEST_UNUSED(setenv("TMPDIR", box.tmp, 1));
    TEST_UNUSED(setenv("FORT_STD_DIR", box.std, 1));
    TEST_UNUSED(setenv("FORT_FAKE_CC_LOG", box.log, 1));
    box.ok = true;
    return box;
}

/// Removes the sandbox. A test that fails an assertion returns before this,
/// which is why the next sandbox_open resets the environment rather than
/// trusting this one to have run.
static inline void sandbox_close(sandbox_t* box) {
    env_reset();
    if (box->dir[0] != '\0') {
        remove_tree(box->dir);
    }
}

// ---- the expected texts ----------------------------------------------------------

/// The expected command lines and messages hold paths the sandbox chose, so
/// they are written as a format taking one or two of them.
static inline void expect1(char* dst, size_t size, const char* format, const char* a) {
    TEST_UNUSED(snprintf(dst, size, format, a));
}

static inline void expect2(
    char* dst, size_t size, const char* format, const char* a, const char* b) {
    TEST_UNUSED(snprintf(dst, size, format, a, b));
}

static inline void expect3(
    char* dst, size_t size, const char* format, const char* a, const char* b, const char* c) {
    TEST_UNUSED(snprintf(dst, size, format, a, b, c));
}

// ---- the diagnostics of a run ----------------------------------------------------
// D14.2

/// The compile-time diagnostics go to stderr, not to the stream driver_main
/// writes its `fort: error:` lines to, so a test that wants them captures them
/// with diag_capture; the run is otherwise an ordinary one.
/// D14.2
static char last_diags[CAPTURE_MAX];

static inline run_t run_and_capture(char** argv) {
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

/// Writes a source file, replacing what the sandbox put there.
static inline bool write_source(const char* path, const char* text) {
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    TEST_UNUSED(fputs(text, file));
    TEST_UNUSED(fclose(file));
    return true;
}

#endif

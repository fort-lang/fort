// Provides file sandboxes and shared helpers for module tests.
//
// The helpers are static inline and the state is per suite, so a suite that
// uses only some of them still builds under -Werror.
#ifndef FORT_TEST_MODULES_HELPERS_H
#define FORT_TEST_MODULES_HELPERS_H

#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "diag.h"
#include "modules.h"
#include "scope.h"
#include "str.h"

#include "test.h"

enum { PATH_CAP = 512 };

// Stops the suite when a sandbox path did not fit its buffer, naming the path that overflowed. A
// truncated path names a file other than the one the test asked for, and the helpers below read it,
// write it and delete it. Working on the truncation would answer about the wrong file, or remove
// it. `written` is the snprintf result: using it is also what keeps gcc from warning that the call
// may truncate.
static inline void sandbox_path_fits(int written, size_t cap, const char* what) {
    if (written < 0 || (size_t)written >= cap) {
        TEST_UNUSED(fprintf(stderr, "modules: path too long for %zu bytes: %s\n", cap, what));
        exit(TEST_RESULT_ERR);
    }
}

// Writes `<dir>/<rel>` into `dst` and exits when it does not fit.
// The distinct name avoids the driver function that each suite links.
static inline void join_sandbox_path(char* dst, size_t cap, const char* dir, const char* rel) {
    const int written = snprintf(dst, cap, "%s/%s", dir, rel);
    if (written < 0 || (size_t)written >= cap) {
        TEST_UNUSED(
            fprintf(stderr, "modules: path too long for %zu bytes: %s/%s\n", cap, dir, rel));
        exit(TEST_RESULT_ERR);
    }
}

// Removes a directory and everything below it.
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
            join_sandbox_path(child, sizeof child, path, entry->d_name);
            remove_tree(child);
        }
        entry = readdir(dir);
    }
    TEST_UNUSED(closedir(dir));
    TEST_UNUSED(rmdir(path));
}

// The tree of the running test, its module set and its captured diagnostics.
static char sandbox[PATH_CAP];
static char scratch[PATH_CAP];
static module_set_t set;
static sb_t captured;
static bool open_sandbox = false;

// Releases the module set, captured diagnostics, and sandbox tree.
// Each setup and suite shutdown calls it, including after an early assertion failure.
static inline void done(void) {
    if (!open_sandbox) {
        return;
    }
    diag_capture(NULL);
    module_set_free(&set);
    sb_free(&captured);
    remove_tree(sandbox);
    sandbox[0] = '\0';
    open_sandbox = false;
}

static inline void begin(void) {
    done();
    char pattern[] = "/tmp/fort-modules-test-XXXXXX";
    if (mkdtemp(pattern) == NULL) {
        // Every test writes into the tree, so a suite without one would run
        // against the previous test's files: end it here instead.
        TEST_UNUSED(fputs("modules: cannot create the sandbox directory\n", stderr));
        exit(TEST_RESULT_ERR);
    }
    // Rejects a truncated copy of the path that `mkdtemp` writes.
    sandbox_path_fits(snprintf(sandbox, sizeof sandbox, "%s", pattern), sizeof sandbox, pattern);
    sb_init(&captured);
    diag_capture(&captured);
    diag_reset();
    module_set_init(&set);
    open_sandbox = true;
}

// `<sandbox>/<rel>`, valid until the next call.
static inline const char* in_sandbox(const char* rel) {
    join_sandbox_path(scratch, sizeof scratch, sandbox, rel);
    return scratch;
}

// Writes a module at `rel` under the sandbox, creating the directories its path names.
static inline void add(const char* rel, const char* text) {
    char path[PATH_CAP];
    join_sandbox_path(path, sizeof path, sandbox, rel);
    for (char* cursor = path + strlen(sandbox) + 1; *cursor != '\0'; cursor++) {
        if (*cursor == '/') {
            *cursor = '\0';
            TEST_UNUSED(mkdir(path, S_IRWXU));
            *cursor = '/';
        }
    }
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return;
    }
    TEST_UNUSED(fputs(text, file));
    TEST_UNUSED(fclose(file));
}

// Adds a search root of the sandbox, as `-I <dir>` does.
static inline void root(const char* rel) {
    module_set_add_root(&set, in_sandbox(rel));
}

// The runtime file a standard library directory must hold: every closure holds `std.rt`, which the
// loader reads from `<std-dir>/rt.ft`. The tests below care about the loader and not about the
// runtime, so the file is empty.
static const char SANDBOX_RUNTIME[] = "// The empty runtime of a loader test.\n";

// Names the standard library directory, as `--std-dir` does, and puts the runtime in it.
static inline void std_dir(const char* rel) {
    char path[PATH_CAP];
    join_sandbox_path(path, sizeof path, rel, "rt.ft");
    add(path, SANDBOX_RUNTIME);
    module_set_std_dir(&set, in_sandbox(rel));
}

// Names a standard library directory and puts no runtime in it, which is what a directory the
// compiler cannot read `std.rt` from is.
static inline void std_dir_without_runtime(const char* rel) {
    module_set_std_dir(&set, in_sandbox(rel));
}

// Loads the closure of the entry module `rel` of the sandbox.
static inline bool load(const char* rel) {
    return module_set_load(&set, in_sandbox(rel));
}

// The captured diagnostics, one line each.
// The next load or `done` invalidates the result.
static inline const char* diags(void) {
    return sb_cstr(&captured);
}

// Whether the diagnostics hold `text`.
static inline bool said(const char* text) {
    return strstr(diags(), text) != NULL;
}

// Returns the `i`-th module path in dependency order.
// Returns the diagnostics when that module does not exist.
static inline const char* ordered(uint64_t i) {
    if (i >= module_set_count(&set)) {
        return diags();
    }
    return module_set_at(&set, i)->path.ptr;
}

// The module path of the `i`-th module of the pass order, or the diagnostics when there is no such
// module.
static inline const char* passed(uint64_t i) {
    if (i >= module_set_pass_count(&set)) {
        return diags();
    }
    return module_set_pass_at(&set, i)->path.ptr;
}

// The binding of `name` in the namespace of the module `path`, or NULL.
static inline const binding_t* bound(const char* path, const char* name) {
    const module_t* m = module_set_find(&set, str_from_cstr(path));
    if (m == NULL) {
        return NULL;
    }
    return scope_find(&m->names, str_from_cstr(name));
}

// The sources the tests reuse.
static inline const char* src_main(void) {
    return "fn main() i32 {\n    return 0;\n}\n";
}

static inline const char* src_add(void) {
    return "fn add(i32 a, i32 b) i32 {\n    return a + b;\n}\n";
}

#endif

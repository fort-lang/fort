// The environment the module suites share: a directory tree under /tmp that
// one test writes its modules into, the module_set_t that loads them and the
// captured diagnostics, so that resolution is exercised against real files
// (module-system.md 2).
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
            TEST_UNUSED(snprintf(child, sizeof child, "%s/%s", path, entry->d_name));
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

// Releases the set, the capture and the tree of the previous test. Called by
// every begin and once at the end of the suite, so that a test which returns
// early on a failed assertion leaves nothing behind.
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
    TEST_UNUSED(snprintf(sandbox, sizeof sandbox, "%s", pattern));
    sb_init(&captured);
    diag_capture(&captured);
    diag_reset();
    module_set_init(&set);
    open_sandbox = true;
}

// `<sandbox>/<rel>`, valid until the next call.
static inline const char* in_sandbox(const char* rel) {
    TEST_UNUSED(snprintf(scratch, sizeof scratch, "%s/%s", sandbox, rel));
    return scratch;
}

// Writes a module at `rel` under the sandbox, creating the directories its
// path names.
static inline void add(const char* rel, const char* text) {
    char path[PATH_CAP];
    TEST_UNUSED(snprintf(path, sizeof path, "%s/%s", sandbox, rel));
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

// Adds a search root of the sandbox, as `-I <dir>` does (D9.2).
static inline void root(const char* rel) {
    module_set_add_root(&set, in_sandbox(rel));
}

// Names the standard library directory, as `--std-dir` does (D14.1).
static inline void std_dir(const char* rel) {
    module_set_std_dir(&set, in_sandbox(rel));
}

// Loads the closure of the entry module `rel` of the sandbox.
static inline bool load(const char* rel) {
    return module_set_load(&set, in_sandbox(rel));
}

// The captured diagnostics, one line each.
static inline const char* diags(void) {
    return sb_cstr(&captured);
}

// Whether the diagnostics hold `text`.
static inline bool said(const char* text) {
    return strstr(diags(), text) != NULL;
}

// The module path of the `i`-th module of the dependency order, or the
// diagnostics when there is no such module, so that a failing assertion shows
// why.
static inline const char* ordered(uint64_t i) {
    if (i >= module_set_count(&set)) {
        return diags();
    }
    return module_set_at(&set, i)->path.ptr;
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
    return "fn i32 main() {\n    return 0;\n}\n";
}

static inline const char* src_add(void) {
    return "fn i32 add(i32 a, i32 b) {\n    return a + b;\n}\n";
}

#endif

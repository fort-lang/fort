// The environment the index suites share: the module sandbox and the checker
// of check_helpers.h, an index_t built over the closure they leave, and one
// spelling of a record so that a test compares a whole record in one
// assertion (D20.3).
//
// The helpers are static inline and the state is per suite, so a suite that
// uses only some of them still builds under -Werror.
#ifndef FORT_TEST_INDEX_HELPERS_H
#define FORT_TEST_INDEX_HELPERS_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "check_helpers.h"
#include "index.h"
#include "modules_helpers.h"
#include "str.h"
#include "sym.h"

#include "test.h"

static index_t ix;
static bool index_live = false;

// Releases the index of the previous test. It borrows the names and the file
// names of the modules, so it never outlives the sandbox that holds them.
static inline void index_reset(void) {
    if (index_live) {
        index_free(&ix);
        index_live = false;
    }
}

// Indexes the closure the last check left (D20.3).
static inline void index_closure(void) {
    index_reset();
    index_init(&ix);
    index_live = true;
    index_build(&ix, &set);
}

// Checks the entry module `main.ft` of one source and indexes the closure.
static inline bool index_src(const char* text) {
    const bool ok = check_src(text);
    index_closure();
    return ok;
}

// A file name without the sandbox directory, so that a test names
// `main.ft` and not the temporary directory of the run.
static inline const char* rel_file(const char* file) {
    if (file == NULL) {
        return "<none>";
    }
    const size_t n = strlen(sandbox);
    if (strncmp(file, sandbox, n) == 0 && file[n] == '/') {
        return file + n + 1;
    }
    return file;
}

static inline void append_pos(sb_t* out, loc_t loc) {
    sb_append(out, rel_file(loc.file));
    sb_push(out, ':');
    sb_append_u64(out, loc.line);
    sb_push(out, ':');
    sb_append_u64(out, loc.col);
}

// One record as a line: the range of the occurrence, the kind, the name, the
// type in quotes, `decl` or `use`, and the declaration's position or `null`
// (D20.3). Valid until the next call.
static inline const char* entry_text(const index_entry_t* e) {
    static sb_t out;
    static bool ready = false;
    if (!ready) {
        sb_init(&out);
        ready = true;
    }
    sb_clear(&out);
    if (e == NULL) {
        sb_append(&out, "<none>");
        return sb_cstr(&out);
    }
    append_pos(&out, e->loc);
    sb_push(&out, '-');
    sb_append_u64(&out, e->loc.end_line);
    sb_push(&out, ':');
    sb_append_u64(&out, e->loc.end_col);
    sb_push(&out, ' ');
    sb_append(&out, sym_kind_name(e->kind));
    sb_push(&out, ' ');
    sb_append_str(&out, e->name);
    sb_append(&out, " '");
    sb_append_str(&out, e->type);
    sb_append(&out, "' ");
    sb_append(&out, e->is_decl ? "decl " : "use ");
    if (e->has_decl) {
        append_pos(&out, e->decl);
    } else {
        sb_append(&out, "null");
    }
    return sb_cstr(&out);
}

// The record whose occurrence begins at `line`:`col` of the file `rel`, or
// NULL; `rel` NULL takes the first file that has one.
static inline const index_entry_t* entry_in(const char* rel, uint32_t line, uint32_t col) {
    for (uint64_t i = 0; i < index_count(&ix); i++) {
        const index_entry_t* e = index_at(&ix, i);
        if (e->loc.line == line && e->loc.col == col &&
            (rel == NULL || strcmp(rel_file(e->loc.file), rel) == 0)) {
            return e;
        }
    }
    return NULL;
}

static inline const index_entry_t* entry_at(uint32_t line, uint32_t col) {
    return entry_in(NULL, line, col);
}

// The spelling of the record at `line`:`col`, or "<none>".
static inline const char* text_at(uint32_t line, uint32_t col) {
    return entry_text(entry_at(line, col));
}

// The same in one named file, which is what a test over two modules asks.
static inline const char* text_in(const char* rel, uint32_t line, uint32_t col) {
    return entry_text(entry_in(rel, line, col));
}

// Every record of the file `rel`, one per line, in the order the index holds
// them: what a test asserts when the order is the point. Valid until the next
// call.
static inline const char* file_text(const char* rel) {
    static sb_t out;
    static bool ready = false;
    if (!ready) {
        sb_init(&out);
        ready = true;
    }
    sb_clear(&out);
    for (uint64_t i = 0; i < index_count(&ix); i++) {
        const index_entry_t* e = index_at(&ix, i);
        if (strcmp(rel_file(e->loc.file), rel) != 0) {
            continue;
        }
        if (out.len > 0) {
            sb_push(&out, '\n');
        }
        sb_append_str(&out, e->name);
        sb_push(&out, '@');
        sb_append_u64(&out, e->loc.line);
        sb_push(&out, ':');
        sb_append_u64(&out, e->loc.col);
    }
    return sb_cstr(&out);
}

// The records of the file `rel`.
static inline uint64_t file_count(const char* rel) {
    uint64_t n = 0;
    for (uint64_t i = 0; i < index_count(&ix); i++) {
        if (strcmp(rel_file(index_at(&ix, i)->loc.file), rel) == 0) {
            n++;
        }
    }
    return n;
}

#endif

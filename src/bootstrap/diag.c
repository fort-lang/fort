// Diagnostics and the message builder; see diag.h.
#include "diag.h"

#include <stdio.h>

// Errors reported since the last diag_reset.
static uint64_t error_count = 0;

// Where the lines go: the capture buffer, or stderr when NULL.
static sb_t* capture_sink = NULL;

// The nesting depth of diag_mute and the error count the outermost mute saw.
static uint64_t mute_depth = 0;
static uint64_t mute_saved_count = 0;

// A position with no extent yet: the end is the start (D20.4).
loc_t loc_make(const char* file, uint32_t line, uint32_t col) {
    return loc_range(file, line, col, line, col);
}

loc_t loc_range(
    const char* file, uint32_t line, uint32_t col, uint32_t end_line, uint32_t end_col) {
    loc_t loc;
    loc.file = file;
    loc.line = line;
    loc.col = col;
    loc.end_line = end_line;
    loc.end_col = end_col;
    return loc;
}

// Whether the position `line_a`:`col_a` is at or before `line_b`:`col_b`.
static bool pos_at_or_before(uint32_t line_a, uint32_t col_a, uint32_t line_b, uint32_t col_b) {
    if (line_a != line_b) {
        return line_a < line_b;
    }
    return col_a <= col_b;
}

bool loc_starts_at_or_before(loc_t a, loc_t b) {
    return pos_at_or_before(a.line, a.col, b.line, b.col);
}

bool loc_ends_at_or_before(loc_t a, loc_t b) {
    return pos_at_or_before(a.end_line, a.end_col, b.end_line, b.end_col);
}

bool loc_is_ordered(loc_t loc) {
    return pos_at_or_before(loc.line, loc.col, loc.end_line, loc.end_col);
}

// The start of `a` and the later of the two ends, so extending never moves
// the start and never shrinks the range (D20.4).
loc_t loc_extend(loc_t a, loc_t b) {
    if (loc_ends_at_or_before(a, b)) {
        return loc_range(a.file, a.line, a.col, b.end_line, b.end_col);
    }
    return a;
}

// Writes one `<file>:<line>:<col>: <kind>: <msg>` line (toolchain.md 4).
static void diag_write(loc_t loc, const char* kind, const char* msg) {
    if (mute_depth > 0) {
        // A muted diagnostic is counted by the caller of diag_write and never
        // built, so a probing parse costs nothing and diag_count still says
        // whether the file it read parsed.
        return;
    }
    sb_t line;
    sb_init(&line);
    sb_append(&line, loc.file);
    sb_push(&line, ':');
    sb_append_u64(&line, loc.line);
    sb_push(&line, ':');
    sb_append_u64(&line, loc.col);
    sb_append(&line, ": ");
    sb_append(&line, kind);
    sb_append(&line, ": ");
    sb_append(&line, msg);
    sb_push(&line, '\n');
    if (capture_sink != NULL) {
        sb_append_str(capture_sink, sb_view(&line));
    } else {
        (void)fwrite(line.data, 1, (size_t)line.len, stderr);
    }
    sb_free(&line);
}

void diag_error(loc_t loc, const char* msg) {
    diag_write(loc, "error", msg);
    error_count++;
}

void diag_note(loc_t loc, const char* msg) {
    diag_write(loc, "note", msg);
}

uint64_t diag_count(void) {
    return error_count;
}

void diag_reset(void) {
    error_count = 0;
}

void diag_capture(sb_t* sink) {
    capture_sink = sink;
}

void diag_mute(void) {
    if (mute_depth == 0) {
        mute_saved_count = error_count;
    }
    mute_depth++;
}

uint64_t diag_unmute(void) {
    if (mute_depth == 0) {
        fatal_internal("diag_unmute: no diag_mute is open");
    }
    mute_depth--;
    if (mute_depth > 0) {
        return 0;
    }
    const uint64_t suppressed = error_count - mute_saved_count;
    error_count = mute_saved_count;
    return suppressed;
}

// ---- message builder ---------------------------------------------------------------

void msg_begin(sb_t* m) {
    sb_clear(m);
}

void msg_str(sb_t* m, const char* text) {
    sb_append(m, text);
}

void msg_view(sb_t* m, str_t s) {
    sb_append_str(m, s);
}

void msg_int(sb_t* m, int64_t v) {
    sb_append_i64(m, v);
}

void msg_uint(sb_t* m, uint64_t v) {
    sb_append_u64(m, v);
}

void msg_quote(sb_t* m, str_t name) {
    sb_push(m, '\'');
    sb_append_str(m, name);
    sb_push(m, '\'');
}

const char* msg_end(sb_t* m) {
    return sb_cstr(m);
}

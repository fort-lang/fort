/* Diagnostics and the message builder; see diag.h. */
#include "diag.h"

#include <stdio.h>

/* Errors reported since the last diag_reset. */
static uint64_t error_count = 0;

/* Where the lines go: the capture buffer, or stderr when NULL. */
static sb_t* capture_sink = NULL;

loc_t loc_make(const char* file, uint32_t line, uint32_t col) {
    loc_t loc;
    loc.file = file;
    loc.line = line;
    loc.col = col;
    return loc;
}

/* Writes one `<file>:<line>:<col>: <kind>: <msg>` line (toolchain.md 4). */
static void diag_write(loc_t loc, const char* kind, const char* msg) {
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

/* ---- message builder ------------------------------------------------------------ */

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

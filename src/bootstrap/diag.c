// Diagnostics and the message builder; see diag.h.
#include "diag.h"

#include <stddef.h>
#include <stdio.h>

// One sink holds everything a run of diagnostics needs: the records and the
// pool their messages are copied into, the error count, where the text lines
// go and whether they are written at all, and the state of diag_mute. It is a
// global because the compiler is a single-threaded batch job, and zeroed
// storage is a valid empty sink, which is what a `mut` global with a `{}`
// initializer gives the fort port (D7.10). Text output is on until it is
// turned off, so the flag records its absence.
typedef struct {
    diag_record_t* records;    // the diagnostics reported so far, or NULL
    uint64_t len;              // records in use
    uint64_t cap;              // records allocated
    str_pool_t pool;           // owns the copy of every recorded message
    uint64_t errors;           // errors reported since the last diag_reset
    uint64_t file_errors;      // errors reported since the last diag_begin_file
    bool text_off;             // whether the text line is suppressed
    sb_t* capture;             // where the lines go, or stderr when NULL
    uint64_t mute_depth;       // open diag_mute calls
    uint64_t mute_saved_count; // the error count the outermost mute saw
    uint64_t mute_saved_file;  // the file error count the outermost mute saw
} diag_sink_t;

static diag_sink_t sink;

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

// Whether a diagnostic reported now is dropped: a muted one is counted by
// diag_error and never written or recorded, so a probing parse costs nothing
// and diag_count still says whether the file it read parsed.
static bool diag_muted(void) {
    return sink.mute_depth > 0;
}

// Writes one `<file>:<line>:<col>: <kind>: <msg>` line (toolchain.md 4), the
// text form of D14.2, unless the text is muted or turned off.
static void diag_write(loc_t loc, const char* kind, const char* msg) {
    if (diag_muted() || sink.text_off) {
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
    if (sink.capture != NULL) {
        sb_append_str(sink.capture, sb_view(&line));
    } else {
        (void)fwrite(line.data, 1, (size_t)line.len, stderr);
    }
    sb_free(&line);
}

// Room for one more record; growth is allocate, copy, free.
static void records_reserve(void) {
    if (sink.len < sink.cap) {
        return;
    }
    const uint64_t cap = mem_grown_cap(sink.cap, mem_add(sink.len, 1));
    diag_record_t* records =
        (diag_record_t*)mem_alloc(mem_mul(cap, (uint64_t)sizeof(diag_record_t)));
    for (uint64_t i = 0; i < sink.len; i++) {
        records[i] = sink.records[i];
    }
    mem_free(sink.records);
    sink.records = records;
    sink.cap = cap;
}

// Keeps the diagnostic, with its whole range (D20.4) and a copy of its
// message, for the structured form; a muted diagnostic is not reported and so
// is not recorded.
//
// The file name is copied too: the caller's own name dies with the module set
// that read the file, while a record is read after the front end returned
// (D20.2), so a record owns every byte it hands out.
static void record_append(loc_t loc, diag_severity_t severity, const char* msg) {
    if (diag_muted()) {
        return;
    }
    records_reserve();
    diag_record_t rec;
    rec.loc = loc;
    if (loc.file != NULL) {
        rec.loc.file = str_pool_intern(&sink.pool, str_from_cstr(loc.file)).ptr;
    }
    rec.severity = severity;
    rec.msg = str_pool_intern(&sink.pool, str_from_cstr(msg));
    sink.records[sink.len] = rec;
    sink.len++;
}

// A file's budget is spent by its lexer and by its parser alike (D14.2), so
// the count they cap on is this one.
void diag_begin_file(void) {
    sink.file_errors = 0;
}

uint64_t diag_file_count(void) {
    return sink.file_errors;
}

void diag_error(loc_t loc, const char* msg) {
    diag_write(loc, "error", msg);
    record_append(loc, DIAG_ERROR, msg);
    sink.errors++;
    sink.file_errors++;
}

// A note belongs to the error before it and is not counted (D14.2).
void diag_note(loc_t loc, const char* msg) {
    diag_write(loc, "note", msg);
    record_append(loc, DIAG_NOTE, msg);
}

uint64_t diag_count(void) {
    return sink.errors;
}

void diag_reset(void) {
    sink.errors = 0;
    sink.file_errors = 0;
    mem_free(sink.records);
    sink.records = NULL;
    sink.len = 0;
    sink.cap = 0;
    str_pool_free(&sink.pool);
}

void diag_capture(sb_t* buffer) {
    sink.capture = buffer;
}

void diag_set_text(bool on) {
    sink.text_off = !on;
}

void diag_mute(void) {
    if (sink.mute_depth == 0) {
        sink.mute_saved_count = sink.errors;
        sink.mute_saved_file = sink.file_errors;
    }
    sink.mute_depth++;
}

uint64_t diag_unmute(void) {
    if (sink.mute_depth == 0) {
        fatal_internal("diag_unmute: no diag_mute is open");
    }
    sink.mute_depth--;
    if (sink.mute_depth > 0) {
        return 0;
    }
    const uint64_t suppressed = sink.errors - sink.mute_saved_count;
    sink.errors = sink.mute_saved_count;
    // The probe's own file spent no budget of the file that asked for it.
    sink.file_errors = sink.mute_saved_file;
    return suppressed;
}

uint64_t diag_record_count(void) {
    return sink.len;
}

// By value: appending a diagnostic moves the array, so a pointer into it
// would dangle at the next diag_error.
diag_record_t diag_record_at(uint64_t i) {
    if (i >= sink.len) {
        fatal_internal("diag_record_at: index out of range");
    }
    return sink.records[i];
}

// ---- the structured form -----------------------------------------------------------

// The range of a diagnostic: the start D14.2 prints and the exclusive end an
// editor underlines, both 1-based byte columns (D20.4).
static void write_range(json_t* j, loc_t loc) {
    json_key(j, "file");
    json_cstr(j, loc.file);
    json_key(j, "line");
    json_uint(j, loc.line);
    json_key(j, "col");
    json_uint(j, loc.col);
    json_key(j, "end_line");
    json_uint(j, loc.end_line);
    json_key(j, "end_col");
    json_uint(j, loc.end_col);
}

static const char* severity_name(diag_severity_t severity) {
    switch (severity) {
    case DIAG_ERROR:
        return "error";
    case DIAG_NOTE:
        return "note";
    }
    fatal_internal("diag: unknown severity");
}

// The record at `i` as one diagnostic, with the notes that follow it nested
// under it: a note belongs to the error it follows (D14.2).
static void write_diagnostic(json_t* j, uint64_t i) {
    const diag_record_t* rec = &sink.records[i];
    json_object_begin(j);
    write_range(j, rec->loc);
    json_key(j, "severity");
    json_cstr(j, severity_name(rec->severity));
    json_key(j, "message");
    json_str(j, rec->msg);
    json_key(j, "notes");
    json_array_begin(j);
    if (rec->severity == DIAG_ERROR) {
        uint64_t k = i + 1;
        while (k < sink.len && sink.records[k].severity == DIAG_NOTE) {
            json_object_begin(j);
            write_range(j, sink.records[k].loc);
            json_key(j, "message");
            json_str(j, sink.records[k].msg);
            json_object_end(j);
            k++;
        }
    }
    json_array_end(j);
    json_object_end(j);
}

void diag_write_json(json_t* j) {
    json_array_begin(j);
    // A note is written under the error it follows; one that follows no error
    // stands alone rather than being dropped.
    bool after_error = false;
    for (uint64_t i = 0; i < sink.len; i++) {
        const diag_severity_t severity = sink.records[i].severity;
        if (severity == DIAG_NOTE && after_error) {
            continue;
        }
        write_diagnostic(j, i);
        after_error = severity == DIAG_ERROR;
    }
    json_array_end(j);
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

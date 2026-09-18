// Compile-time diagnostics and the message builder.
//
// Diagnostics use `<file>:<line>:<col>: <severity>: <message>` on stderr.
// Errors increase the count. Notes do not.
// diag_capture redirects these lines to a buffer.
//
// Each diagnostic is also stored as a record with its range and message.
// diag_write_json writes these records after compilation.
// diag_set_text disables the text form when a mode needs only records.
//
// The msg_* functions build a message in a caller-owned sb_t.
// Call msg_begin, append the parts, and pass msg_end to diag_error or diag_note.
#ifndef FORT_DIAG_H
#define FORT_DIAG_H

#include <stdbool.h>
#include <stdint.h>

#include "json.h"
#include "str.h"

// A range in a source file: 1-based lines and byte columns, a tab counting as
// one column, the start inclusive and the end exclusive. An
// error without a position in the file uses the empty range at 1:1. Only the
// start is printed; the end is what an editor underlines.
typedef struct {
    const char* file;
    uint32_t line;
    uint32_t col;
    uint32_t end_line;
    uint32_t end_col;
} loc_t;

// The empty range at `line`:`col`: a position with no extent yet.
loc_t loc_make(const char* file, uint32_t line, uint32_t col);

// The range from `line`:`col` to `end_line`:`end_col`, the end exclusive.
loc_t loc_range(const char* file, uint32_t line, uint32_t col, uint32_t end_line, uint32_t end_col);

// Moves the end of `a` to the later end.
// It does not move the start or shrink the range.
// To widen leftwards, call loc_range with the leftmost start.
loc_t loc_extend(loc_t a, loc_t b);

// Whether `a` starts at or before `b` starts, and whether `a` ends at or
// before `b` ends; both compare line then column.
bool loc_starts_at_or_before(loc_t a, loc_t b);
bool loc_ends_at_or_before(loc_t a, loc_t b);

// Whether `loc` ends at or after it starts, which every range does.
bool loc_is_ordered(loc_t loc);

// What a diagnostic says about itself: an error, or a note that belongs to the
// error before it.
typedef enum { DIAG_ERROR, DIAG_NOTE } diag_severity_t;

// One reported diagnostic, returned by value.
// The sink owns copies of `msg` and the file name in `loc`.
// These copies stay valid until diag_reset and include NUL terminators.
// Thus, `msg.ptr` is also a C string.
typedef struct {
    loc_t loc;
    diag_severity_t severity;
    str_t msg;
} diag_record_t;

// One file can report at most twenty lexical and syntax errors in total.
enum { DIAG_MAX_PER_FILE = 20 };

// Clears the file error count before lex_file starts.
// parse_module continues with the same count.
void diag_begin_file(void);

// The errors reported since diag_begin_file, which is what the budget above
// is spent from; 0 when no file was begun.
uint64_t diag_file_count(void);

// Writes `<file>:<line>:<col>: error: <msg>` and counts one error.
void diag_error(loc_t loc, const char* msg);

// Writes `<file>:<line>:<col>: note: <msg>`; not counted.
void diag_note(loc_t loc, const char* msg);

// The number of errors reported since the start or the last diag_reset.
uint64_t diag_count(void);

// Sets the error count to 0 and drops every record: the records and the
// messages read before it are gone.
void diag_reset(void);

// Sends every following diagnostic line to `buffer` instead of stderr; NULL
// restores stderr. The buffer is borrowed until then.
void diag_capture(sb_t* buffer);

// Suppresses diagnostic lines and records until the matching diag_unmute.
// Mutes nest. The outermost diag_unmute restores the saved total and file counts.
// It returns the number of suppressed errors. Inner diag_unmute calls return 0.
// Calling diag_unmute with no open mute is an internal error.
void diag_mute(void);
uint64_t diag_unmute(void);

// Enables or disables diagnostic text. Text is on initially.
// Records remain enabled in both modes. diag_reset does not change this mode.
void diag_set_text(bool on);

// The diagnostics recorded since the start or the last diag_reset, in the
// order they were reported. `i` past the end is an internal error.
uint64_t diag_record_count(void);
diag_record_t diag_record_at(uint64_t i);

// Writes the records as a JSON array of diagnostics, each note nested under
// the error it follows, through `j`.
void diag_write_json(json_t* j);

// ---- message builder ---------------------------------------------------------------

// Empties a message buffer before the caller reuses it.
void msg_begin(sb_t* m);

void msg_str(sb_t* m, const char* text);

void msg_view(sb_t* m, str_t s);

// Appends a number in decimal.
void msg_int(sb_t* m, int64_t v);
void msg_uint(sb_t* m, uint64_t v);

// Appends a name between single quotes: 'name'.
void msg_quote(sb_t* m, str_t name);

// The finished message as a C string, valid until the next msg_* call on
// `m`.
const char* msg_end(sb_t* m);

#endif

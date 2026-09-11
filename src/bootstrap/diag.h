// Compile-time diagnostics (toolchain.md 4, D14.2) and the message builder.
//
// Diagnostics are written to stderr, one per line, as
// `<file>:<line>:<col>: error: <message>` and
// `<file>:<line>:<col>: note: <message>`; errors are counted and notes are
// not. For tests, diag_capture redirects the lines into a buffer.
//
// Every diagnostic is also kept as a record, with its whole range and a copy
// of its message, so that a structured form can be written after the
// compilation as well as the text form during it (diag_write_json); a mode
// that wants only the structured form turns the text off with diag_set_text.
//
// Messages are assembled with the msg_* functions into a caller-owned sb_t
// (str.h) so that the compiler never formats with printf: begin, append
// pieces, end, and pass the result to diag_error or diag_note.
//
// The file mirrors what the self-hosted compiler will do with std.strbuf and
// std.io: no unions, no function pointers, no macros beyond constants.
#ifndef FORT_DIAG_H
#define FORT_DIAG_H

#include <stdbool.h>
#include <stdint.h>

#include "json.h"
#include "str.h"

// A range in a source file: 1-based lines and byte columns, a tab counting as
// one column, the start inclusive and the end exclusive (D20.4, toolchain.md
// 4). An error without a position in the file uses the empty range at 1:1
// (D14.2). Only the start is printed; the end is what an editor underlines.
typedef struct {
    const char* file;
    uint32_t line;
    uint32_t col;
    uint32_t end_line;
    uint32_t end_col;
} loc_t;

// The empty range at `line`:`col`: a position with no extent yet (D20.4).
loc_t loc_make(const char* file, uint32_t line, uint32_t col);

// The range from `line`:`col` to `end_line`:`end_col`, the end exclusive
// (D20.4).
loc_t loc_range(const char* file, uint32_t line, uint32_t col, uint32_t end_line, uint32_t end_col);

// `a` with its end moved to the later of the two ends: it never moves the
// start and never shrinks the range, so extending a range over a token it
// already covers changes nothing (D20.4). To widen leftwards, build the range
// from the leftmost start with loc_range.
loc_t loc_extend(loc_t a, loc_t b);

// Whether `a` starts at or before `b` starts, and whether `a` ends at or
// before `b` ends; both compare line then column.
bool loc_starts_at_or_before(loc_t a, loc_t b);
bool loc_ends_at_or_before(loc_t a, loc_t b);

// Whether `loc` ends at or after it starts, which every range does (D20.4).
bool loc_is_ordered(loc_t loc);

// What a diagnostic says about itself: an error, or a note that belongs to
// the error before it (D14.2).
typedef enum { DIAG_ERROR, DIAG_NOTE } diag_severity_t;

// One reported diagnostic, handed out by value: the sink's own array moves
// when it grows, while `msg` and the file name of `loc` are copies owned by
// the sink's pool, stable until diag_reset, and NUL-terminated like every
// pooled string, so `msg.ptr` is also a C string. The file name is copied
// because a record is read after the compilation that reported it has
// released the names it read (D20.2).
typedef struct {
    loc_t loc;
    diag_severity_t severity;
    str_t msg;
} diag_record_t;

// The diagnostics one file may report (D14.2): at most twenty, counted
// across its lexical and its syntax errors together, so a file that is
// mistyped from end to end costs a reader twenty lines and no more.
enum { DIAG_MAX_PER_FILE = 20 };

// Starts the budget of the file about to be read: lex_file opens it and
// parse_module goes on with what the lexer left of it, so the two share one
// counter (D14.2).
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

// Drops the line of every diagnostic reported until diag_unmute, which
// restores the error count diag_mute saw and returns how many errors were
// suppressed: a parse that only answers a question about a file, such as
// whether it declares a name, reports nothing (module-system.md 3). Mutes
// nest, and only the outermost one restores the count.
void diag_mute(void);
uint64_t diag_unmute(void);

// Whether each diagnostic writes its text line as it arrives; on until turned
// off, so a mode that wants only the structured form (diag_write_json) writes
// no text. Records are kept either way. A mode, not state: diag_reset leaves
// it alone.
void diag_set_text(bool on);

// The diagnostics recorded since the start or the last diag_reset, in the
// order they were reported. `i` past the end is an internal error.
uint64_t diag_record_count(void);
diag_record_t diag_record_at(uint64_t i);

// Writes the records as a JSON array of diagnostics, each note nested under
// the error it follows (D14.2), through `j`.
void diag_write_json(json_t* j);

// ---- message builder ---------------------------------------------------------------

// Empties the message buffer.
void msg_begin(sb_t* m);

// Appends literal text.
void msg_str(sb_t* m, const char* text);

// Appends the bytes of a view.
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

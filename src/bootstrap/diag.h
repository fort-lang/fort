// Compile-time diagnostics (toolchain.md 4, D14.2) and the message builder.
//
// Diagnostics are written to stderr, one per line, as
// `<file>:<line>:<col>: error: <message>` and
// `<file>:<line>:<col>: note: <message>`; errors are counted and notes are
// not. For tests, diag_capture redirects the lines into a buffer.
//
// Messages are assembled with the msg_* functions into a caller-owned sb_t
// (str.h) so that the compiler never formats with printf: begin, append
// pieces, end, and pass the result to diag_error or diag_note.
//
// The file mirrors what the self-hosted compiler will do with std::strbuf and
// std::io: no unions, no function pointers, no macros beyond constants.
#ifndef FORT_DIAG_H
#define FORT_DIAG_H

#include <stdint.h>

#include "str.h"

// A position in a source file: 1-based line and column (toolchain.md 4). An
// error without a position in the file uses 1:1 (D14.2).
typedef struct {
    const char* file;
    uint32_t line;
    uint32_t col;
} loc_t;

loc_t loc_make(const char* file, uint32_t line, uint32_t col);

// Writes `<file>:<line>:<col>: error: <msg>` and counts one error.
void diag_error(loc_t loc, const char* msg);

// Writes `<file>:<line>:<col>: note: <msg>`; not counted.
void diag_note(loc_t loc, const char* msg);

// The number of errors reported since the start or the last diag_reset.
uint64_t diag_count(void);

// Sets the error count to 0.
void diag_reset(void);

// Sends every following diagnostic line to `sink` instead of stderr; NULL
// restores stderr. The sink is borrowed until then.
void diag_capture(sb_t* sink);

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

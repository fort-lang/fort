// The JSON writer of the bootstrap compiler: the structured output of
// `fort --check --json` is built with these calls over an sb_t (str.h) and
// written in one piece, so the compiler never formats with printf.
//
// A writer emits one document on one line, valid UTF-8: no indentation, no
// spaces between tokens. Commas come from a per-depth "first" flag, so a
// caller only opens a container, writes keys and values, and closes it.
// Nesting deeper than JSON_MAX_DEPTH is an internal error, as is closing a
// container that was never opened.
//
// The file mirrors what the self-hosted compiler will do (the fort port is a
// separate deliverable): no unions, no function pointers, no macros beyond
// constants, every struct laid out in the open.
#ifndef FORT_JSON_H
#define FORT_JSON_H

#include <stdbool.h>
#include <stdint.h>

#include "str.h"

// The deepest nesting of objects and arrays a writer accepts; the documents
// of the check mode nest four deep.
enum { JSON_MAX_DEPTH = 8 };

// A writer over a borrowed buffer. `depth` is the number of containers open;
// `first[d]` says whether the container at depth `d` is still empty, which is
// what decides a separating comma. Zero-initialized storage is not a valid
// writer: json_init sets the buffer.
typedef struct {
    sb_t* out;                  // the buffer the document is built in
    uint64_t depth;             // containers currently open
    bool first[JSON_MAX_DEPTH]; // whether the container at that depth is empty
} json_t;

// Starts a writer over `out`, which is borrowed until the last call and is
// not emptied: a caller may write a document after other bytes.
void json_init(json_t* j, sb_t* out);

// Opens and closes an object or an array. Opening beyond JSON_MAX_DEPTH or
// closing at depth 0 is an internal error.
void json_object_begin(json_t* j);
void json_object_end(json_t* j);
void json_array_begin(json_t* j);
void json_array_end(json_t* j);

// Writes a member name and its colon, escaped as json_str escapes a string;
// the member's value is the next value written. Outside an object it is an
// internal error.
void json_key(json_t* j, const char* name);

// Writes a quoted string: '"' and '\' are escaped, a byte below 0x20 is
// written as \n, \t, \r or \u00XX, and a well-formed multi-byte UTF-8
// sequence is passed through verbatim. The document a writer produces is
// valid UTF-8: a byte that is not part of a well-formed sequence is written
// as U+FFFD, since fort source is UTF-8 by convention and never validated
// (D2.1, D3.7), so a message can quote any bytes at all.
void json_str(json_t* j, str_t s);
void json_cstr(json_t* j, const char* s);

// Writes a number in decimal, `-` first for a negative one.
void json_uint(json_t* j, uint64_t v);
void json_int(json_t* j, int64_t v);

// Writes `true`, `false` or `null`.
void json_bool(json_t* j, bool v);
void json_null(json_t* j);

#endif

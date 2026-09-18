// Appends compact, valid UTF-8 JSON to a string buffer.
#ifndef FORT_JSON_H
#define FORT_JSON_H

#include <stdbool.h>
#include <stdint.h>

#include "str.h"

// The deepest nesting of objects and arrays a writer accepts. Check documents nest five levels.
enum { JSON_MAX_DEPTH = 8 };

// A writer over a borrowed buffer. `depth` is the number of open containers. `first[d]` states
// whether the container at depth `d` is empty. This state controls the separator comma.
// Zero-initialized storage is not a valid writer: json_init sets the buffer.
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

// Writes a quoted string. It escapes `"` and `\`. It writes bytes below 0x20 as \n, \t, \r, or
// \u00XX. Valid multi-byte UTF-8 passes through unchanged. The writer always produces valid UTF-8.
// It writes an invalid byte as U+FFFD. This permits a message to quote arbitrary source bytes.
void json_str(json_t* j, str_t s);
void json_cstr(json_t* j, const char* s);

// Writes a number in decimal, `-` first for a negative one.
void json_uint(json_t* j, uint64_t v);
void json_int(json_t* j, int64_t v);

// Writes `true`, `false` or `null`.
void json_bool(json_t* j, bool v);
void json_null(json_t* j);

#endif

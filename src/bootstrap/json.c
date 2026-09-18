// Implements the compact JSON writer.
#include "json.h"

#include <stddef.h>

// The hex digits of a \u00XX escape, uppercase as in the compiler's other
// hex spellings (consts.c, lexer.c).
static const char HEX_DIGITS[] = "0123456789ABCDEF";
enum { HEX_SHIFT = 4, HEX_MASK = 0xFU };

// The last control byte: a byte at or below it is escaped.
enum { CONTROL_MAX = 0x1F };

// Defines the last ASCII byte and valid UTF-8 byte ranges (RFC 3629). The ranges cover two-byte,
// three-byte, and four-byte sequences. The second-byte limits exclude overlong forms, UTF-16
// surrogates, and values above U+10FFFF.
enum {
    ASCII_MAX = 0x7F,
    UTF8_CONT_MIN = 0x80,
    UTF8_CONT_MAX = 0xBF,
    UTF8_LEAD2_MIN = 0xC2,
    UTF8_LEAD2_MAX = 0xDF,
    UTF8_LEAD3_MIN = 0xE0,
    UTF8_LEAD3_MAX = 0xEF,
    UTF8_LEAD4_MIN = 0xF0,
    UTF8_LEAD4_MAX = 0xF4,
    UTF8_LEAD3_OVERLONG = 0xE0,
    UTF8_LEAD3_SURROGATE = 0xED,
    UTF8_LEAD4_OVERLONG = 0xF0,
    UTF8_LEAD4_HIGHEST = 0xF4,
    UTF8_E0_MIN = 0xA0,
    UTF8_ED_MAX = 0x9F,
    UTF8_F0_MIN = 0x90,
    UTF8_F4_MAX = 0x8F,
};

// Defines the UTF-8 bytes of U+FFFD. The writer uses this replacement for each invalid byte, so
// every document is valid UTF-8.
static const char REPLACEMENT[] = "\xEF\xBF\xBD";

void json_init(json_t* j, sb_t* out) {
    if (out == NULL) {
        fatal_internal("json_init: no buffer");
    }
    j->out = out;
    j->depth = 0;
    for (uint64_t i = 0; i < (uint64_t)JSON_MAX_DEPTH; i++) {
        j->first[i] = true;
    }
}

// Writes the comma that separates two members or elements of the container
// currently open, and marks that container non-empty. At depth 0 the document
// is one value, so nothing separates it.
static void json_sep(json_t* j) {
    if (j->depth == 0) {
        return;
    }
    const uint64_t level = j->depth - 1;
    if (j->first[level]) {
        j->first[level] = false;
    } else {
        sb_push(j->out, ',');
    }
}

// Opens a container after its separator: the new level starts empty.
static void json_begin(json_t* j, char open) {
    if (j->depth >= (uint64_t)JSON_MAX_DEPTH) {
        fatal_internal("json: nesting too deep");
    }
    json_sep(j);
    sb_push(j->out, open);
    j->first[j->depth] = true;
    j->depth++;
}

static void json_end(json_t* j, char close) {
    if (j->depth == 0) {
        fatal_internal("json: no container is open");
    }
    j->depth--;
    sb_push(j->out, close);
}

void json_object_begin(json_t* j) {
    json_begin(j, '{');
}

void json_object_end(json_t* j) {
    json_end(j, '}');
}

void json_array_begin(json_t* j) {
    json_begin(j, '[');
}

void json_array_end(json_t* j) {
    json_end(j, ']');
}

// Returns the valid UTF-8 sequence length at `i`, or 0 for invalid bytes. It rejects truncated
// sequences, stray continuation bytes, overlong forms, UTF-16 surrogates, and values above U+10FFFF
// (RFC 3629).
static uint64_t utf8_sequence_len(str_t s, uint64_t i) {
    const uint8_t lead = (uint8_t)s.ptr[i];
    uint64_t len = 0;
    uint8_t second_min = UTF8_CONT_MIN;
    uint8_t second_max = UTF8_CONT_MAX;
    if (lead >= UTF8_LEAD2_MIN && lead <= UTF8_LEAD2_MAX) {
        len = 2;
    } else if (lead >= UTF8_LEAD3_MIN && lead <= UTF8_LEAD3_MAX) {
        len = 3;
        if (lead == UTF8_LEAD3_OVERLONG) {
            second_min = UTF8_E0_MIN;
        }
        if (lead == UTF8_LEAD3_SURROGATE) {
            second_max = UTF8_ED_MAX;
        }
    } else if (lead >= UTF8_LEAD4_MIN && lead <= UTF8_LEAD4_MAX) {
        len = 4;
        if (lead == UTF8_LEAD4_OVERLONG) {
            second_min = UTF8_F0_MIN;
        }
        if (lead == UTF8_LEAD4_HIGHEST) {
            second_max = UTF8_F4_MAX;
        }
    } else {
        return 0;
    }
    if (s.len - i < len) {
        return 0;
    }
    const uint8_t second = (uint8_t)s.ptr[i + 1];
    if (second < second_min || second > second_max) {
        return 0;
    }
    for (uint64_t k = 2; k < len; k++) {
        const uint8_t cont = (uint8_t)s.ptr[i + k];
        if (cont < UTF8_CONT_MIN || cont > UTF8_CONT_MAX) {
            return 0;
        }
    }
    return len;
}

// Writes the quoted form of `s`. It escapes quotes and backslashes. It writes control bytes as \n,
// \t, \r, a fort escape, or JSON \u00XX. A valid multi-byte UTF-8 sequence passes through
// unchanged. An invalid byte becomes U+FFFD. Thus, the document remains valid when fort source is
// not validated.
static void json_quoted(json_t* j, str_t s) {
    sb_push(j->out, '"');
    uint64_t i = 0;
    while (i < s.len) {
        const uint8_t byte = (uint8_t)s.ptr[i];
        uint64_t step = 1;
        if (byte == (uint8_t)'"' || byte == (uint8_t)'\\') {
            sb_push(j->out, '\\');
            sb_push(j->out, (char)byte);
        } else if (byte == (uint8_t)'\n') {
            sb_append(j->out, "\\n");
        } else if (byte == (uint8_t)'\t') {
            sb_append(j->out, "\\t");
        } else if (byte == (uint8_t)'\r') {
            sb_append(j->out, "\\r");
        } else if (byte <= CONTROL_MAX) {
            sb_append(j->out, "\\u00");
            sb_push(j->out, HEX_DIGITS[byte >> HEX_SHIFT]);
            sb_push(j->out, HEX_DIGITS[byte & HEX_MASK]);
        } else if (byte <= ASCII_MAX) {
            sb_push(j->out, (char)byte);
        } else {
            step = utf8_sequence_len(s, i);
            if (step == 0) {
                sb_append(j->out, REPLACEMENT);
                step = 1;
            } else {
                sb_append_str(j->out, str_from_range(s.ptr + i, step));
            }
        }
        i += step;
    }
    sb_push(j->out, '"');
}

// A member name is a string followed by a colon. The value that follows is
// the member's own, so the level is marked empty again and the value's
// separator writes no comma.
void json_key(json_t* j, const char* name) {
    if (j->depth == 0) {
        fatal_internal("json_key: no object is open");
    }
    json_sep(j);
    json_quoted(j, str_from_cstr(name));
    sb_push(j->out, ':');
    j->first[j->depth - 1] = true;
}

void json_str(json_t* j, str_t s) {
    json_sep(j);
    json_quoted(j, s);
}

void json_cstr(json_t* j, const char* s) {
    json_str(j, str_from_cstr(s));
}

void json_uint(json_t* j, uint64_t v) {
    json_sep(j);
    sb_append_u64(j->out, v);
}

void json_int(json_t* j, int64_t v) {
    json_sep(j);
    sb_append_i64(j->out, v);
}

void json_bool(json_t* j, bool v) {
    json_sep(j);
    if (v) {
        sb_append(j->out, "true");
    } else {
        sb_append(j->out, "false");
    }
}

void json_null(json_t* j) {
    json_sep(j);
    sb_append(j->out, "null");
}

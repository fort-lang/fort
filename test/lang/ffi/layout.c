// Compares fort struct layout with the target C ABI.
//
// fort computes the layout of a struct and the C compiler computes it again. The two must agree
// ("Struct layout stays C-compatible, so pointer-based interop works"). Nothing in a fort-only
// program can see a disagreement, because a wrong layout that is used consistently agrees with
// itself. These helpers are the boundary that can see it: every struct below is the C spelling of
// one the fort side declares. Each helper reads or writes it through C's offsets while fort reads
// or writes it through its own.
//
// The signatures take pointers and integers only, which is what an extern
// signature may use.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The values layout_fill writes, one per field, so that a field read at the
// wrong offset cannot come back right by accident.
enum {
    FILL_TAG = 11,
    FILL_N = 222,
    FILL_K = 3333,
    FILL_BIG = 44444,
};

// The base layout_table_digest mixes one field per digit in.
enum { DIGIT = 10 };

// struct interop { u8 tag; i32 n; u16 k; i64 big; }
struct interop {
    uint8_t tag;
    int32_t n;
    uint16_t k;
    int64_t big;
};

// struct flagged { bool on; i32 n; bool off; }: fort's `bool` is C's `_Bool`,
// one byte in memory.
struct flagged {
    bool on;
    int32_t n;
    bool off;
};

// struct named { u8 tag; string name; }: a `string` is a two-word header and
// is never split into two fields.
struct named {
    uint8_t tag;
    struct {
        const char* ptr;
        uint64_t len;
    } name;
};

// struct cell { i32 n; u8 tag; } and struct table { cell[3] rows; u8 tag; }:
// the array strides by the element's padded size.
struct cell {
    int32_t n;
    uint8_t tag;
};

struct table {
    struct cell rows[3];
    uint8_t tag;
};

uint64_t layout_sizeof_interop(void) {
    return sizeof(struct interop);
}

// The offset C gives each field of `interop`, by declaration index.
uint64_t layout_offsetof_interop(int32_t field) {
    switch (field) {
    case 0:
        return offsetof(struct interop, tag);
    case 1:
        return offsetof(struct interop, n);
    case 2:
        return offsetof(struct interop, k);
    case 3:
        return offsetof(struct interop, big);
    default:
        break;
    }
    return UINT64_MAX;
}

// Writes one distinct value into each field, for fort to read back.
void layout_fill(struct interop* r) {
    r->tag = (uint8_t)FILL_TAG;
    r->n = FILL_N;
    r->k = (uint16_t)FILL_K;
    r->big = FILL_BIG;
}

// Whether every field holds the value fort was asked to write into it.
bool layout_check(const struct interop* r) {
    return r->tag == (uint8_t)FILL_TAG && r->n == FILL_N && r->k == (uint16_t)FILL_K &&
           r->big == FILL_BIG;
}

uint64_t layout_sizeof_flagged(void) {
    return sizeof(struct flagged);
}

// The two `bool` fields as one number, so a `bool` laid out as anything but a
// byte moves `n` and shows up here.
int64_t layout_read_flagged(const struct flagged* f) {
    return (int64_t)f->n + (f->on ? 1 : 0) - (f->off ? 1 : 0);
}

uint64_t layout_sizeof_named(void) {
    return sizeof(struct named);
}

// The length of the `string` field, read through C's offset of it.
uint64_t layout_named_len(const struct named* s) {
    return s->name.len;
}

// Its first byte, which proves the pointer half of the header is where C expects it too. fort
// `char` is C's `unsigned char` at the boundary, an `i8 zeroext`, and plain `char` is signed on
// x86-64. The return type is the unsigned one the fort declaration promises.
unsigned char layout_named_first(const struct named* s) {
    return s->name.len == 0 ? (unsigned char)'?' : (unsigned char)s->name.ptr[0];
}

uint64_t layout_sizeof_table(void) {
    return sizeof(struct table);
}

// Mixes each `table` field into a separate decimal digit.
// A wrong offset, stride, or `tag` changes the result.
int64_t layout_table_digest(const struct table* t) {
    int64_t digest = 0;
    for (size_t i = 0; i < sizeof t->rows / sizeof t->rows[0]; i++) {
        digest = digest * DIGIT + t->rows[i].n;
        digest = digest * DIGIT + t->rows[i].tag;
    }
    return digest * DIGIT + t->tag;
}

// The sum of the rows' `n` fields: a wrong element stride reads the padding.
int64_t layout_sum_rows(const struct table* t) {
    int64_t sum = 0;
    for (size_t i = 0; i < sizeof t->rows / sizeof t->rows[0]; i++) {
        sum += t->rows[i].n;
    }
    return sum;
}

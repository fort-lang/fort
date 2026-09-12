// C11 helpers for test/lang/run/ffi/011_rt_enum_member.ft. That test holds
// std.rt's `struct enum_member` against the layout of the table a print of an
// enum reads.
//
// The compiler emits that table as `[N x %fort.enum_member]`, and
// `%fort.enum_member = type { i32, ptr }` (toolchain.md 6 items 2 and 21;
// test/gen_enum_test.c pins that text). The struct below is that type. The C
// runtime read those tables through this declaration for as long as the
// runtime was C.
//
// No fort program can name `@.enum.<path>`, so this file is the only boundary
// that can see std.rt's two fields disagree with it. C writes the table
// through C's offsets and fort reads it through fort's offsets, and then the
// two swap jobs. One language alone cannot see a swap, a widening or a
// reorder that leaves every later offset where it was. This file can.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// struct enum_member { i32 value; char* name; } in std.rt.
struct rt_enum_member {
    int32_t value;
    const char* name;
};

// One distinct value for each row, so that a field read at the wrong offset
// cannot come back right by accident. The three are a negative value, zero,
// and a value too large for a byte.
enum {
    FILL_FIRST = -7,
    FILL_SECOND = 0,
    FILL_THIRD = 1000000,
    ENTRY_COUNT = 3,
};

static const char* const NAMES[ENTRY_COUNT] = {"minus_seven", "zero", "million"};
static const int32_t VALUES[ENTRY_COUNT] = {FILL_FIRST, FILL_SECOND, FILL_THIRD};

uint64_t rt_enum_sizeof(void) {
    return sizeof(struct rt_enum_member);
}

uint64_t rt_enum_offset_value(void) {
    return offsetof(struct rt_enum_member, value);
}

uint64_t rt_enum_offset_name(void) {
    return offsetof(struct rt_enum_member, name);
}

// Writes ENTRY_COUNT rows through C's offsets. fort then reads them through
// its own.
void rt_enum_fill(struct rt_enum_member* m) {
    for (int32_t i = 0; i < ENTRY_COUNT; i++) {
        m[i].value = VALUES[i];
        m[i].name = NAMES[i];
    }
}

// Answers whether the rows fort wrote hold what fort was asked to write. It
// reads them through C's offsets.
bool rt_enum_check(const struct rt_enum_member* m) {
    for (int32_t i = 0; i < ENTRY_COUNT; i++) {
        if (m[i].value != VALUES[i] || strcmp(m[i].name, NAMES[i]) != 0) {
            return false;
        }
    }
    return true;
}

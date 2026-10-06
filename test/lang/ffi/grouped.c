// Compares a fort struct whose fields are grouped types with its C spelling.
//
// Parentheses group a complete type and change no layout and no calling convention (D3.6,
// D3.10), so C spells the same struct without them. Each helper reads or writes the struct
// through C's offsets while fort uses its own, and C calls the fort functions of the table
// through C function pointers.
//
// The signatures take pointers, function pointers and integers only, which is what an extern
// signature may use.
#include <stddef.h>
#include <stdint.h>

typedef int32_t (*unary_fn_t)(int32_t);

// struct dispatch { u8 tag; (fn (i32) i32)[2] table; ((i32 mut*)) slot; ((u8)) last; }
struct dispatch {
    uint8_t tag;
    unary_fn_t table[2];
    int32_t* slot;
    uint8_t last;
};

// The base grouped_bytes puts the tag in front of the last byte with.
enum { BYTE_BASE = 1000 };

// The field numbers grouped_offsetof_dispatch takes, in declaration order.
enum { FIELD_TAG, FIELD_TABLE, FIELD_SLOT, FIELD_LAST };

uint64_t grouped_sizeof_dispatch(void) {
    return sizeof(struct dispatch);
}

uint64_t grouped_offsetof_dispatch(int32_t field) {
    switch (field) {
    case FIELD_TAG:
        return offsetof(struct dispatch, tag);
    case FIELD_TABLE:
        return offsetof(struct dispatch, table);
    case FIELD_SLOT:
        return offsetof(struct dispatch, slot);
    default:
        return offsetof(struct dispatch, last);
    }
}

// Calls entry `index` of the table through C's offsets and stores the result through the slot.
int32_t grouped_call(const struct dispatch* d, int32_t index, int32_t n) {
    const int32_t result = d->table[index](n);
    *d->slot = result;
    return result;
}

// Swaps the two entries through C's offsets, so fort then calls them in the other order.
void grouped_swap(struct dispatch* d) {
    const unary_fn_t first = d->table[0];
    d->table[0] = d->table[1];
    d->table[1] = first;
}

// Reads the two byte fields that stand on each side of the table.
int32_t grouped_bytes(const struct dispatch* d) {
    return ((int32_t)d->tag * BYTE_BASE) + (int32_t)d->last;
}

// Calls each of `count` entries of a table that fort lends as a pointer to its first element.
int32_t grouped_sum(const unary_fn_t* table, int32_t count, int32_t n) {
    int32_t sum = 0;
    for (int32_t i = 0; i < count; i++) {
        sum += table[i](n);
    }
    return sum;
}

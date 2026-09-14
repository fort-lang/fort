// C11 helpers linked into test/lang/run/ffi/012_multidim_layout.ft, which
// holds fort's layout of a nested array against the C ABI's on the target.
//
// A wrong element stride is invisible to a fort-only program and to the LLVM
// verifier: under opaque pointers the type of a field is observable only
// through the offsets it moves, and a stride that is wrong on both sides of an
// access agrees with itself. These helpers are the boundary that sees it.
// Every array below is the C spelling of one the fort side declares, and each
// helper reads or writes it through C's own indexing while fort reads or
// writes it through its GEPs.
//
// The signatures take pointers and integers only, which is what an extern
// signature may use.
// D9.8
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The dimensions the fort side writes: i32[2][3] is two rows of three.
// D3.6
enum { ROWS = 2, COLS = 3, PLANES = 2 };

// The base a digest mixes one element per digit in.
enum { DIGIT = 10 };

// struct cell { i32 n; u8 tag; }, which pads to 8 bytes, and
// struct block { i32[2][3] grid; u8 tag; }.
struct cell {
    int32_t n;
    uint8_t tag;
};

struct block {
    int32_t grid[ROWS][COLS];
    uint8_t tag;
};

// The fat pointer: a span is { ptr, len } and an array of spans strides by
// this struct's size.
// D19.2
struct span {
    void* ptr;
    uint64_t len;
};

uint64_t multidim_sizeof_grid(void) {
    return sizeof(int32_t[ROWS][COLS]);
}

uint64_t multidim_sizeof_row(void) {
    return sizeof(int32_t[COLS]);
}

uint64_t multidim_sizeof_cube(void) {
    return sizeof(int32_t[PLANES][ROWS][COLS]);
}

uint64_t multidim_sizeof_block(void) {
    return sizeof(struct block);
}

uint64_t multidim_offsetof_block_tag(void) {
    return offsetof(struct block, tag);
}

uint64_t multidim_sizeof_cell_grid(void) {
    return sizeof(struct cell[ROWS][COLS]);
}

uint64_t multidim_sizeof_span(void) {
    return sizeof(struct span);
}

// Writes 10 * i + j into every element, through C's own indexing, so that a
// row read at the wrong stride comes back with another row's values.
void multidim_fill(int32_t* grid) {
    int32_t(*rows)[COLS] = (int32_t(*)[COLS])grid;
    for (int32_t i = 0; i < ROWS; i++) {
        for (int32_t j = 0; j < COLS; j++) {
            rows[i][j] = DIGIT * i + j;
        }
    }
}

// Every element mixed into one digit each, in row-major order: a wrong stride,
// a transposed pair of dimensions or a lost element all change the number.
int64_t multidim_digest(const int32_t* grid) {
    const int32_t(*rows)[COLS] = (const int32_t(*)[COLS])grid;
    int64_t digest = 0;
    for (int32_t i = 0; i < ROWS; i++) {
        for (int32_t j = 0; j < COLS; j++) {
            digest = digest * DIGIT + rows[i][j];
        }
    }
    return digest;
}

// The same over the block's field, which also says the field starts where
// fort's `&b.grid` says it does.
int64_t multidim_block_digest(const struct block* b) {
    return multidim_digest(&b->grid[0][0]) * DIGIT + b->tag;
}

// One element of the grid by its two indices, read with C's stride.
int32_t multidim_at(const int32_t* grid, int32_t i, int32_t j) {
    const int32_t(*rows)[COLS] = (const int32_t(*)[COLS])grid;
    return rows[i][j];
}

// The `n` field of one cell of a cell[2][3], which strides by the padded size
// of the struct in both dimensions.
// D3.4, D3.8
int32_t multidim_cell_at(const struct cell* cells, int32_t i, int32_t j) {
    const struct cell(*rows)[COLS] = (const struct cell(*)[COLS])cells;
    return rows[i][j].n;
}

// The sum of the elements of the `k`-th row of a span of rows: the span's
// element is an int32_t[3], so this strides by 12 bytes from the pointer fort
// handed over.
int64_t multidim_span_row_sum(const int32_t* rows, uint64_t len, uint64_t k) {
    if (k >= len) {
        return -1;
    }
    const int32_t(*at)[COLS] = (const int32_t(*)[COLS])rows;
    int64_t sum = 0;
    for (int32_t j = 0; j < COLS; j++) {
        sum += at[k][j];
    }
    return sum;
}

// The length field of the `k`-th span of an array of spans, read at C's own
// stride over the fat pointer.
uint64_t multidim_span_len_at(const struct span* spans, uint64_t k) {
    return spans[k].len;
}

// Its first element, which proves the pointer half is where C expects it.
int32_t multidim_span_first_at(const struct span* spans, uint64_t k) {
    const int32_t* first = (const int32_t*)spans[k].ptr;
    return first[0];
}

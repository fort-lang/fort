// The LLVM IR emitter of the bootstrap compiler (toolchain.md 6, D19): it
// walks the checked tree of every module of the closure and appends the text
// of one module to a buffer, in one forward pass (D19.1).
//
// What it reads. The checker (check.h) left a type on every expression, a
// symbol on every name and a folded value on every constant node, so the
// emitter asks no questions of its own: a node marked CHECK_ANN_CONST is
// emitted as a literal, every other node is lowered instruction by
// instruction. The tree, the symbols and the type table must still be alive,
// so a caller emits before check_free.
//
// The model (D19.3, D19.4). Only scalars are SSA values; a struct, fixed
// array, span or `string` always occupies a place, so an expression is
// lowered in one of three ways: gen_expr_value for a scalar, gen_expr_place
// for an lvalue and gen_expr_into for a value written into a destination.
// Every local, parameter slot and compiler temporary is an entry-block
// alloca, and no `phi` is ever built.
//
// Determinism (D19.5). Every counter is per function and reset at each
// definition, every list is appended to in emission order and nothing is
// iterated out of a hash table, so the text is a function of the program
// alone.
//
// Sections (toolchain.md 6 item 1). The module is assembled from one buffer
// per section at the end: the triple, the named types, the module-level data,
// the function definitions, the private data, the declarations and the
// attribute groups. A function keeps two buffers of its own, because failure
// blocks are emitted after every normal block of the function (D19.6).
//
// The file mirrors what the self-hosted compiler will do: no unions, no
// macros beyond constants, plain switches over the node kinds, and text
// appended through sb_t instead of printf formats.
#ifndef FORT_GEN_H
#define FORT_GEN_H

#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "check.h"
#include "containers.h"
#include "modules.h"
#include "str.h"
#include "sym.h"
#include "types.h"

// The runtime entry points of toolchain.md 5.1, in the order that section
// lists them, which is the order their declarations are emitted in (D19.5).
typedef enum {
    RT_NEW,
    RT_DEL,
    RT_FAIL_BOUNDS,
    RT_FAIL_SPAN,
    RT_FAIL_OVERFLOW,
    RT_FAIL_SHIFT,
    RT_FAIL_DIV_ZERO,
    RT_FAIL_DIV_OVERFLOW,
    RT_FAIL_ALLOC_COUNT,
    RT_FAIL_OVERWRITE,
    RT_PANIC,
    RT_ASSERT_FAIL,
    RT_PRINT_I64,
    RT_PRINT_U64,
    RT_PRINT_F32,
    RT_PRINT_F64,
    RT_PRINT_BOOL,
    RT_PRINT_CHAR,
    RT_PRINT_PTR,
    RT_PRINT_STR,
    RT_PRINT_ENUM,
    RT_FLUSH,
    RT_FLUSH_ALL,
    RT_COUNT,
} gen_rt_t;

// The intrinsics of toolchain.md 6 item 8, in the fixed order that item
// gives: memcpy, memset, the overflow family by operation then by ascending
// width, then llvm.trap. The float conversions of item 12 have no slot: the
// bootstrap compiler rejects floats before code generation.
typedef enum {
    IN_MEMCPY,
    IN_MEMSET,
    IN_OVERFLOW_FIRST,
    IN_TRAP = IN_OVERFLOW_FIRST + 24,
    IN_COUNT,
} gen_intrinsic_t;

// The overflow intrinsics, in the order `sadd ssub smul uadd usub umul` and,
// within each, the widths `i8 i16 i32 i64` (item 15).
typedef enum {
    GEN_OVF_SADD,
    GEN_OVF_SSUB,
    GEN_OVF_SMUL,
    GEN_OVF_UADD,
    GEN_OVF_USUB,
    GEN_OVF_UMUL,
    GEN_OVF_COUNT,
} gen_overflow_t;

// The widths an overflow intrinsic exists at, `i8 i16 i32 i64` (item 15).
enum { GEN_OVF_WIDTHS = 4 };

// The attribute groups of toolchain.md 6, at their fixed indices; only the
// used ones are emitted, so gaps in the numbering are normal (D19.5).
typedef enum {
    ATTR_FN,        // #0, every fort definition (item 7)
    ATTR_FN_NORET,  // #1, a `noreturn` definition (item 20)
    ATTR_FAIL,      // #2, the failure entry points (item 14)
    ATTR_NOBUILTIN, // #3, every extern call site (item 8)
    ATTR_OVERFLOW,  // #4, the overflow intrinsics (item 15)
    ATTR_MEMCPY,    // #5
    ATTR_MEMSET,    // #6
    ATTR_TRAP,      // #7, llvm.trap (item 20)
    ATTR_COUNT,
} gen_attr_t;

// An IR operand: the type text and the value text, both stable for the life
// of the emitter. `ty` may carry a parameter attribute at a call site
// (`i8 zeroext`), which is why it is text and not a type.
typedef struct {
    str_t ty;
    str_t val;
} gen_val_t;

// A place: the address of storage and the fort type it holds (D19.3). Every
// aggregate and every local lives in one.
typedef struct {
    gen_val_t addr;
    const type_t* type;
} gen_place_t;

// The argument text of one call, built left to right so that a call of any
// arity needs no fixed bound.
typedef struct {
    sb_t text;
    uint64_t count;
} gen_args_t;

void gen_args_init(gen_args_t* a);
void gen_args_free(gen_args_t* a);

// Appends `<ty> <val>`, with the parameter attribute of item 7 between them
// for the _ext form (`i8 zeroext 10`); `attr` may be NULL.
void gen_args_add(gen_args_t* a, gen_val_t v);
void gen_args_add_ext(gen_args_t* a, gen_val_t v, const char* attr);

// The build mode the module is emitted in (D11.1, D10.6).
typedef struct {
    bool release;         // --release: wrapping arithmetic, no overwrite check
    bool no_bounds_check; // --no-bounds-check: no index or span branch
} gen_options_t;

// One local or parameter place: the symbol it belongs to and the name of its
// storage (D19.5). An aggregate parameter's place is the caller-made copy the
// incoming pointer designates, so its name is `%<ident>.in` (item 10).
typedef struct {
    const sym_t* sym;
    str_t name;
} gen_slot_t;

typedef struct gen gen_t;
struct gen {
    gen_options_t opts;
    const check_t* ck;
    const module_set_t* set;

    // ---- the sections of item 1, assembled in order at the end ----
    sb_t named;   // the named types
    sb_t globals; // the module-level data (D7.10)
    sb_t funcs;   // the function definitions
    sb_t out;     // the whole module, written by gen_finish

    // ---- the function being emitted ----
    sb_t allocas; // its entry-block allocas, in declaration order (D19.4)
    sb_t body;    // its normal blocks
    sb_t fail;    // its failure blocks, emitted after them (D19.6)
    uint64_t temps;
    uint64_t labels;
    uint64_t tmps;
    ptrvec_t slots;  // gen_slot_t*, owned; the locals and parameters in order
    bool terminated; // the block being written already ended in a terminator

    // ---- what the module refers to, in first-use order (D19.5) ----
    // The three private-data lists hold records the emitter owns and
    // gen_free releases; the extern list borrows the symbols the checker owns.
    ptrvec_t files;   // @.file.N, one per file
    ptrvec_t strs;    // @.str.N, never deduplicated by content
    ptrvec_t enums;   // @.enum.<path.name>, one per enum some print reaches
    ptrvec_t externs; // const sym_t*: the extern C functions, in first-use order
    bool rt[RT_COUNT];
    bool intrinsics[IN_COUNT];
    bool attrs[ATTR_COUNT];

    str_pool_t pool;        // owns every operand and type text the emitter builds
    sb_t scratch;           // the buffer those texts are built in
    const module_t* module; // the module being emitted
    bool failed;            // a construct the emitter cannot lower yet was reported
};

void gen_init(gen_t* g, gen_options_t opts);

// Releases every buffer and every record; the emitter is empty afterwards.
void gen_free(gen_t* g);

// Emits the whole program into `g`: every module of the closure in dependency
// order, then `fort_entry` for the entry module (D9.10, D11.6). `ck` must be
// the checker that annotated `set` and must not have been freed. Returns
// whether the module is complete; a construct the emitter cannot lower yet is
// reported as a diagnostic and returns false.
bool gen_program(gen_t* g, const check_t* ck, const module_set_t* set);

// The emitted module, valid until the next call on `g`.
str_t gen_text(const gen_t* g);

// ---- shared with gen_data.c, gen_expr.c and gen_stmt.c -----------------------------

// A copy of the text of `scratch`, owned by the emitter's pool and stable
// until gen_free; the buffer is empty afterwards.
str_t gen_take(gen_t* g);

// Reports that the emitter cannot lower `what` yet and fails the module, so
// that an unfinished construct is a diagnostic and never wrong code. The
// tickets after T-015 remove these one by one.
void gen_todo(gen_t* g, loc_t loc, const char* what);

// ---- types (item 2) ---------------------------------------------------------------

// Whether `t` lives in a place rather than in a register: a struct, fixed
// array, span or `string` (D19.3).
bool gen_is_aggregate(const type_t* t);

// The value type of `t`, what a temporary holds: `i1` for `bool` (D19.2).
str_t gen_value_type(gen_t* g, const type_t* t);

// The memory type of `t`, what an alloca, a global or a field holds: `i8` for
// `bool`, `%fort.span` for a span or `string`, `[N x T]` for a fixed array
// (D19.2).
str_t gen_mem_type(gen_t* g, const type_t* t);

// The IR type of a function's result: `void` for an aggregate result, which
// is returned through the `sret` pointer of item 7.
str_t gen_result_type(gen_t* g, const type_t* t);

// The parameter attribute of item 7, or NULL: `zeroext` for `bool`, `char`,
// `u8` and `u16`, `signext` for `i8` and `i16`.
const char* gen_ext_attr(const type_t* t);

// ---- names (item 4, D9.7) ---------------------------------------------------------

// The ELF symbol of a declaration: the module path joined with dots plus the
// declaration name (`main.add`), or the unmangled C name of an `extern`
// function.
str_t gen_symbol(gen_t* g, const sym_t* s);

// The operand naming a declaration: `@"main.add"` for a fort symbol, `@name`
// for a C one, since a dotted name is quoted and quoting is spelling only
// (D9.7).
str_t gen_symbol_ref(gen_t* g, const sym_t* s);

// ---- emission primitives ----------------------------------------------------------

void gen_text_append(gen_t* g, const char* s);
void gen_text_append_str(gen_t* g, str_t s);
void gen_text_append_u64(gen_t* g, uint64_t v);
void gen_text_append_i64(gen_t* g, int64_t v);

// Begins an instruction that produces a value: `  %tN = `, and names it.
gen_val_t gen_temp(gen_t* g, str_t ty);

// Begins an instruction that produces none: two spaces of indent.
void gen_ins(gen_t* g);

// Ends the instruction being written.
void gen_ins_end(gen_t* g);

// ---- values -----------------------------------------------------------------------

// A value with the given type and literal text (`null`, `true`, `0`).
gen_val_t gen_literal(gen_t* g, str_t ty, const char* text);

// An integer literal printed with the signedness of its fort type: `i8 -1`
// for an `i8` and `i8 255` for a `u8` (D19.5).
gen_val_t gen_const_signed(gen_t* g, str_t ty, int64_t v);
gen_val_t gen_const_unsigned(gen_t* g, str_t ty, uint64_t v);

// The smallest value of a signed type of `bits` bits, printed as a signed
// decimal: the operand the division check of item 15 compares against. It is
// built from the bit pattern, since negating the magnitude of `i64` MIN
// overflows the type that would hold it.
gen_val_t gen_const_min(gen_t* g, str_t ty, uint32_t bits);

// The value of a folded constant of type `t` (CHECK_ANN_CONST): an integer,
// `bool`, `char`, `null` or an enum member. A string constant is not a scalar
// and has no value form.
gen_val_t gen_const_value(gen_t* g, const type_t* t, cval_t v);

// `  %tN = <op> <ty> <a>, <b>`, the shape every arithmetic and bitwise
// instruction has.
gen_val_t gen_binary(gen_t* g, const char* op, gen_val_t a, gen_val_t b);

// `  %tN = icmp <pred> <ty> <a>, <b>`, whose result is `i1`.
gen_val_t gen_icmp(gen_t* g, const char* pred, gen_val_t a, gen_val_t b);

// `  %tN = <op> <ty> <a> to <to>`: trunc, zext, sext, ptrtoint, inttoptr
// (item 12).
gen_val_t gen_cast_op(gen_t* g, const char* op, gen_val_t a, str_t to);

// The bit width of an integer, `char`, `bool` or enum type: what a `trunc`
// or an extension compares (item 9).
uint32_t gen_int_bits(const type_t* t);

// Narrows or widens `v` from `from` bits to the `to`-bit type `ty`, by
// `sign`, and emits nothing when the widths already agree (item 9).
gen_val_t gen_resize(gen_t* g, gen_val_t v, uint32_t from, str_t ty, uint32_t to, bool sign);

// `  %tN = load <ty>, ptr <addr>, align <n>` and its store.
gen_val_t gen_load(gen_t* g, str_t ty, gen_val_t addr, uint64_t align);
void gen_store(gen_t* g, gen_val_t v, gen_val_t addr, uint64_t align);

// The scalar value held in a place: a `bool` place is loaded as `i8` and
// truncated (D19.2).
gen_val_t gen_load_place(gen_t* g, gen_place_t p);

// Stores a scalar into a place: a `bool` is zero-extended to `i8` (D19.2).
void gen_store_place(gen_t* g, gen_place_t p, gen_val_t v);

// ---- the three getelementptr shapes (item 3) ---------------------------------------

// `getelementptr inbounds <arrty>, ptr <base>, i64 0, i64 <index>`: an
// element of a fixed array.
gen_val_t gen_gep_array(gen_t* g, const type_t* array, gen_val_t base, gen_val_t index);

// `getelementptr inbounds <elemty>, ptr <base>, i64 <index>`: an element
// reached through a pointer or a span's `.ptr`.
gen_val_t gen_gep_element(gen_t* g, const type_t* elem, gen_val_t base, gen_val_t index);

// `getelementptr inbounds <ty>, ptr <base>, i32 0, i32 <k>`: a field of a
// struct or a header field of a span.
gen_val_t gen_gep_field(gen_t* g, str_t ty, gen_val_t base, uint64_t k);

// ---- aggregates (item 3) ----------------------------------------------------------

// `llvm.memcpy` of `size` bytes from `src` to `dst`, the copy of every
// aggregate (D19.3).
void gen_memcpy(
    gen_t* g, gen_val_t dst, uint64_t dst_align, gen_val_t src, uint64_t src_align, uint64_t size);

// `llvm.memset` of `size` zero bytes: `{}`, `del` and `move` zero a place.
void gen_memset_zero(gen_t* g, gen_val_t dst, uint64_t align, uint64_t size);

// ---- blocks and calls -------------------------------------------------------------

// A fresh block label, `%L<N>` in creation order (D19.5). The number is
// returned; gen_block_begin opens it.
uint64_t gen_label(gen_t* g);

// Opens the block `L<n>`: a blank line, the label, and a fresh terminator
// state.
void gen_block_begin(gen_t* g, uint64_t n);

// `  br label %L<n>` and `  br i1 <cond>, label %L<t>, label %L<f>`.
void gen_br(gen_t* g, uint64_t n);
void gen_br_cond(gen_t* g, gen_val_t cond, uint64_t t, uint64_t f);

// A call to a runtime entry point, which is declared on first use (item 8).
// The result form names the value; the void form emits none.
void gen_call_rt(gen_t* g, gen_rt_t rt, const gen_args_t* args);
gen_val_t gen_call_rt_value(gen_t* g, gen_rt_t rt, str_t ret, const gen_args_t* args);

// The `ptr @.file.N, i32 <line>, i32 <col>` every runtime failure takes: the
// position of D11.4, which is the operator's token (toolchain.md 4).
void gen_args_add_loc(gen_t* g, gen_args_t* args, loc_t loc);

// The check of D19.6: the continuation label is allocated before the failure
// label, and the failure block holds one call to `rt` and `unreachable` and
// is emitted after every normal block. `fail_when` is true for every check,
// whose `i1` is true on failure and whose branch names the failure block
// first, and false for `assert` alone, whose operand is already the success
// condition (D12.2). The caller adds the check's own values to `args`; the
// location of D11.4 is appended here.
void gen_check(gen_t* g, gen_val_t cond, bool fail_when, gen_rt_t rt, gen_args_t* args, loc_t loc);

// Marks an intrinsic or an attribute group used, so that only referenced
// declarations are emitted (item 8, D19.5).
void gen_use_intrinsic(gen_t* g, gen_intrinsic_t which);
void gen_use_attr(gen_t* g, gen_attr_t which);

// ---- private data (item 5) --------------------------------------------------------

// `@.file.N` for the file of `loc`, assigned on first use, one per file.
str_t gen_file_ref(gen_t* g, loc_t loc);

// `@.str.N` holding `s` with the trailing NUL of D3.7, assigned on first use
// and never deduplicated by content (D19.5).
str_t gen_str_ref(gen_t* g, str_t s);

// `@.enum.<path.name>` for the table of an enum some `print` reaches (item
// 21), and the number of members in it.
str_t gen_enum_ref(gen_t* g, const sym_t* e);
uint64_t gen_enum_count(const sym_t* e);

// Records an `extern` function so that its declaration is emitted in
// first-use order (item 8).
void gen_use_extern(gen_t* g, const sym_t* s);

// Appends the private data, the declarations and the attribute groups, then
// assembles the module into `g->out` (item 1).
void gen_finish(gen_t* g);

// ---- slots (item 10) --------------------------------------------------------------

// The place of a local or parameter, whose storage gen_function made.
gen_place_t gen_slot_place(gen_t* g, const sym_t* s);

// ---- statements and expressions ---------------------------------------------------

void gen_stmt(gen_t* g, ast_node_t* n);
void gen_block(gen_t* g, ast_node_t* n);

// A scalar expression's value.
gen_val_t gen_expr_value(gen_t* g, ast_node_t* n);

// An lvalue's place.
gen_place_t gen_expr_place(gen_t* g, ast_node_t* n);

// An expression written into `dst`, which is how every aggregate is produced
// (D19.3). A scalar expression stores its value.
void gen_expr_into(gen_t* g, ast_node_t* n, gen_place_t dst);

// An expression evaluated for its effects alone: a call statement.
void gen_expr_discard(gen_t* g, ast_node_t* n);

// A place the compiler invents, `%tmp<K>` from its own counter (D19.5), for
// an aggregate argument copy or a short-circuit slot.
gen_place_t gen_temp_place(gen_t* g, const type_t* t);

// `a op b` on two scalars, with the checks of item 15: the overflow
// intrinsics for `+ - *`, unary `-`, `++` and `--` in checked mode, plain
// `add`, `sub` and `mul` in release mode and for `+% -% *%`, the shift-count
// check and the division checks, which hold in both modes (D6.13). `at` is
// the left operand's type and the result's, `bt` the right operand's, which
// differs from it only for a shift count (D6.2); `loc` is the operator's
// token, where D11.4 reports the failure.
gen_val_t gen_arith(
    gen_t* g, loc_t loc, int32_t op, const type_t* at, gen_val_t a, const type_t* bt, gen_val_t b);

// The call of a builtin (D12.2, item 19): the print family, `assert` and
// `panic`. Returns false when `n` is not a builtin call.
bool gen_builtin_call(gen_t* g, ast_node_t* n);

#endif

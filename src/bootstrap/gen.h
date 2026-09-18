// Emits one deterministic LLVM IR module from a checked module closure.
// The syntax trees, symbols, and types must remain alive during emission.
#ifndef FORT_GEN_H
#define FORT_GEN_H

#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "check.h"
#include "containers.h"
#include "modules.h"
#include "runtime_sig.h"
#include "str.h"
#include "sym.h"
#include "types.h"

// Intrinsics have a fixed order: memcpy, memset, overflow operations by width,
// and llvm.trap. This compiler rejects floats before code generation.
typedef enum {
    IN_MEMCPY,
    IN_MEMSET,
    IN_OVERFLOW_FIRST,
    IN_TRAP = IN_OVERFLOW_FIRST + 24,
    IN_COUNT,
} gen_intrinsic_t;

// The overflow intrinsics, in the order `sadd ssub smul uadd usub umul` and
// within each, the widths `i8 i16 i32 i64`.
typedef enum {
    GEN_OVF_SADD,
    GEN_OVF_SSUB,
    GEN_OVF_SMUL,
    GEN_OVF_UADD,
    GEN_OVF_USUB,
    GEN_OVF_UMUL,
    GEN_OVF_COUNT,
} gen_overflow_t;

// The widths an overflow intrinsic exists at, `i8 i16 i32 i64`.
enum { GEN_OVF_WIDTHS = 4 };

// The attribute groups, at their fixed indices; only the
// used ones are emitted, so gaps in the numbering are normal.
typedef enum {
    ATTR_FN,        // #0, every fort definition
    ATTR_FN_NORET,  // #1, a `noreturn` definition
    ATTR_UNUSED_2,  // #2, never emitted: it held the C runtime's declarations
    ATTR_NOBUILTIN, // #3, every extern call site
    ATTR_OVERFLOW,  // #4, the overflow intrinsics
    ATTR_MEMCPY,    // #5
    ATTR_MEMSET,    // #6
    ATTR_TRAP,      // #7, llvm.trap
    ATTR_RT_NORET,  // #8, a cold `noreturn` runtime entry point
    ATTR_COUNT,
} gen_attr_t;

// An IR operand: the type text and the value text, both stable for the life
// of the emitter. `ty` may carry a parameter attribute at a call site
// (`i8 zeroext`), which is why it is text and not a type.
typedef struct {
    str_t ty;
    str_t val;
} gen_val_t;

// A place: the address of storage and the fort type it holds. Every aggregate
// and every local lives in one.
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

// Appends `<ty> <val>`, with the parameter attribute between them
// for the _ext form (`i8 zeroext 10`); `attr` may be NULL.
void gen_args_add(gen_args_t* a, gen_val_t v);
void gen_args_add_ext(gen_args_t* a, gen_val_t v, const char* attr);

// The build mode and selected target. A NULL target selects Linux.
typedef struct {
    bool release;         // --release: wrapping arithmetic, no overwrite check
    bool no_bounds_check; // --no-bounds-check: no index or span branch
    const char* target;   // x86_64-linux-gnu or a versioned Darwin target
} gen_options_t;

// One local or parameter place: the symbol it belongs to and the name of its
// storage. An aggregate parameter's place is the caller-made copy the incoming
// pointer designates, so its name is `%<ident>.in`.
typedef struct {
    const sym_t* sym;
    str_t name;
} gen_slot_t;

// The kind of a block scope: which exits leave it, and therefore run the deferred statements it
// holds. `break` leaves the innermost loop or case body. `continue` leaves the innermost loop body.
// `return` leaves through the function body. A block end leaves only that block.
typedef enum {
    GEN_SCOPE_BLOCK, // an ordinary block, left by falling off its end
    GEN_SCOPE_LOOP,  // a loop body, left by `break` and by `continue` too
    GEN_SCOPE_CASE,  // a case body, left by `break`
    GEN_SCOPE_FN,    // the function body, left by `return`
} gen_scope_kind_t;

typedef struct gen gen_t;
struct gen {
    gen_options_t opts;
    const check_t* ck;
    const module_set_t* set;

    // ---- the sections, assembled in order at the end ----
    sb_t named;   // the named types
    sb_t globals; // the module-level data
    sb_t funcs;   // the function definitions
    sb_t out;     // the whole module, written by gen_finish

    // ---- the function being emitted ----
    sb_t allocas; // its entry-block allocas, in declaration order
    sb_t body;    // its normal blocks
    sb_t fail;    // its failure blocks, emitted after them
    uint64_t temps;
    uint64_t labels;
    uint64_t tmps;
    // `break` targets the innermost loop or `switch`. `continue` targets the innermost loop, across
    // an intervening `switch`. Therefore, a loop sets both saved targets. A `switch` sets only the
    // break target. Each flag states whether its target is set. The emitter saves and restores each
    // flag with its label.
    uint64_t break_label;
    uint64_t continue_label;
    bool has_break;
    bool has_continue;
    ptrvec_t slots;  // gen_slot_t*, owned. The locals and parameters in order
    bool terminated; // the block being written already ended in a terminator
    // Tracks open block scopes, with the innermost last, and their deferred statements. Each exit
    // has a static defer set. The emitter reads that set from this stack and copies its code into
    // the exit. The tree shape determines how many scopes an exit leaves. This stack records that
    // shape, so the emitter needs no separate counter.
    ptrvec_t defers;      // ast_node_t*, borrowed. The AST_DEFER nodes of every
                          // open scope, in textual order
    intvec_t scope_kinds; // GEN_SCOPE_*, one per open scope
    intvec_t scope_first; // the index into `defers` where each scope's own
                          // deferred statements begin.
    // The scope an exit inside deferred code may not unwind past: the scopes below it are the ones
    // the exit being expanded is already leaving. The checker refuses `return` there. It also
    // refuses `break` or `continue` without a target inside the deferred code. Reaching the floor
    // is therefore an internal error, not recursive defer expansion.
    uint64_t defer_floor;

    // ---- what the module refers to, in first-use order ----
    // The three private-data lists hold records the emitter owns and gen_free
    // releases. The extern list borrows the symbols the checker owns.
    ptrvec_t files;   // @.file.N, one per file
    ptrvec_t strs;    // @.str.N, never deduplicated by content
    ptrvec_t enums;   // @.enum.<path.name>, one per enum some print reaches
    ptrvec_t externs; // const sym_t*: the extern C functions, in first-use order
    bool intrinsics[IN_COUNT];
    bool attrs[ATTR_COUNT];

    // Stores the `char` element type that `string` does not carry. gen_element_type returns it, so
    // the emitter has one spelling for the IR type.
    type_t char_type;

    str_pool_t pool;        // owns every operand and type text the emitter builds
    sb_t scratch;           // the buffer those texts are built in
    const module_t* module; // the module being emitted
    bool entry_defined;     // `fort_entry` was emitted, so nothing declares it
    bool failed;            // a construct the emitter cannot lower yet was reported
};

void gen_init(gen_t* g, gen_options_t opts);

// Releases every buffer and every record. The emitter is empty afterwards.
void gen_free(gen_t* g);

// Emits the whole program into `g`: every module of the closure in dependency
// order, then `fort_entry` for the entry module. `ck` must be the checker that
// annotated `set` and must not have been freed. Returns whether the module is
// complete. A construct the emitter cannot lower yet is reported as a diagnostic
// and returns false.
bool gen_program(gen_t* g, const check_t* ck, const module_set_t* set);

// The emitted module, valid until the next call on `g`.
str_t gen_text(const gen_t* g);

// ---- shared with gen_data.c, gen_expr.c and gen_stmt.c -----------------------------

// A copy of the text of `scratch`, owned by the emitter's pool and stable
// until gen_free. The buffer is empty afterwards.
str_t gen_take(gen_t* g);

// Reports that the emitter cannot lower `what` and marks the module as failed.
void gen_todo(gen_t* g, loc_t loc, const char* what);

// ---- types ---------------------------------------------------------------

// Whether `t` lives in a place rather than in a register: a struct, fixed
// array, span or `string`.
bool gen_is_aggregate(const type_t* t);

// The value type of `t`, what a temporary holds: `i1` for `bool`.
str_t gen_value_type(gen_t* g, const type_t* t);

// Returns the memory type of `t` for an alloca, global, or field. `bool` uses `i8`. Spans and
// strings use `%fort.span`. A fixed array uses `[N x T]`.
str_t gen_mem_type(gen_t* g, const type_t* t);

// The IR type of a function's result: `void` for an aggregate result, which
// is returned through the `sret` pointer.
str_t gen_result_type(gen_t* g, const type_t* t);

// The parameter attribute, or NULL: `zeroext` for `bool`, `char`,
// `u8` and `u16`, `signext` for `i8` and `i16`.
const char* gen_ext_attr(const type_t* t);

// Returns a field's position among AST_FIELD_DECL children. The named type uses this order.
// Constant initializers use the same field index. The one map from a field symbol to its index: a
// second one would let the initializer's order drift from the named type's.
bool gen_field_index(const sym_t* field, uint64_t* out);

// ---- names ---------------------------------------------------------------

// Returns the declaration's ELF symbol. A fort symbol joins its dotted module path and name, such
// as `main.add`. An `extern` function uses its unmangled C name.
str_t gen_symbol(gen_t* g, const sym_t* s);

// Returns the operand that names a declaration. A fort symbol uses `@"main.add"`. A C symbol uses
// `@name`. Quotes affect spelling only.
str_t gen_symbol_ref(gen_t* g, const sym_t* s);

// Appends `<prefix><name>` after an IR name's `@` or `%`. `always` quotes every fort symbol.
// Otherwise, the function quotes only names that LLVM cannot write unquoted. Module paths never
// need this, but entry base names can. Inside quotes, `"`, `\`, and nonprinting bytes use `\XX` hex
// pairs. LLVM decodes each pair to its byte. Therefore, quoting does not change the ELF symbol.
void gen_append_name(sb_t* out, const char* prefix, str_t name, bool always);

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

// An integer literal printed with the signedness of its fort type: `i8 -1` for
// an `i8` and `i8 255` for a `u8`.
gen_val_t gen_const_signed(gen_t* g, str_t ty, int64_t v);
gen_val_t gen_const_unsigned(gen_t* g, str_t ty, uint64_t v);

// The smallest value of a signed type of `bits` bits, printed as a signed
// decimal: the operand that the division check compares against. It is
// built from the bit pattern, since negating the magnitude of `i64` MIN
// overflows the type that would hold it.
gen_val_t gen_const_min(gen_t* g, str_t ty, uint32_t bits);

// The value of a folded constant of type `t` (CHECK_ANN_CONST): an integer,
// `bool`, `char`, `null` or an enum member. A string constant is not a scalar
// and has no value form.
gen_val_t gen_const_value(gen_t* g, const type_t* t, cval_t v);

// Writes a constant in memory form for a global initializer or field. A `bool` uses `i8 0` or `i8
// 1`, not value-form `i1 true`.
gen_val_t gen_const_mem_value(gen_t* g, const type_t* t, cval_t v);

// `  %tN = <op> <ty> <a>, <b>`, the shape every arithmetic and bitwise
// instruction has.
gen_val_t gen_binary(gen_t* g, const char* op, gen_val_t a, gen_val_t b);

// `  %tN = icmp <pred> <ty> <a>, <b>`, whose result is `i1`.
gen_val_t gen_icmp(gen_t* g, const char* pred, gen_val_t a, gen_val_t b);

// `  %tN = <op> <ty> <a> to <to>`: trunc, zext, sext, ptrtoint, inttoptr
gen_val_t gen_cast_op(gen_t* g, const char* op, gen_val_t a, str_t to);

// The bit width of an integer, `char`, `bool` or enum type: what a `trunc`
// or an extension compares.
uint32_t gen_int_bits(const type_t* t);

// Returns whether values of `t` use signed comparisons, extension, and printing. This includes four
// signed integers and every enum. `char` is unsigned. `bool` is 0 or 1. An enum uses signed `i32`.
// Uses one predicate for both operations. Separate predicates could make a folded member constant
// disagree with its widening cast.
bool gen_is_signed(const type_t* t);

// Narrows or widens `v` from `from` bits to the `to`-bit type `ty`, by
// `sign`, and emits nothing when the widths already agree.
gen_val_t gen_resize(gen_t* g, gen_val_t v, uint32_t from, str_t ty, uint32_t to, bool sign);

// `  %tN = load <ty>, ptr <addr>, align <n>` and its store.
gen_val_t gen_load(gen_t* g, str_t ty, gen_val_t addr, uint64_t align);
void gen_store(gen_t* g, gen_val_t v, gen_val_t addr, uint64_t align);

// The scalar value held in a place: a `bool` place is loaded as `i8` and
// truncated.
gen_val_t gen_load_place(gen_t* g, gen_place_t p);

// Stores a scalar into a place: a `bool` is zero-extended to `i8`.
void gen_store_place(gen_t* g, gen_place_t p, gen_val_t v);

// ---- the three getelementptr shapes ---------------------------------------

// `getelementptr inbounds <arrty>, ptr <base>, i64 0, i64 <index>`: an
// element of a fixed array.
gen_val_t gen_gep_array(gen_t* g, const type_t* array, gen_val_t base, gen_val_t index);

// `getelementptr inbounds <elemty>, ptr <base>, i64 <index>`: an element
// reached through a pointer or a span's `.ptr`.
gen_val_t gen_gep_element(gen_t* g, const type_t* elem, gen_val_t base, gen_val_t index);

// `getelementptr inbounds <ty>, ptr <base>, i32 0, i32 <k>`: a field of a
// struct or a header field of a span.
gen_val_t gen_gep_field(gen_t* g, str_t ty, gen_val_t base, uint64_t k);

// Returns a span or `string` header field. Field 0 is the pointer, and field 1 is the length. All
// readers and writers use this layout helper.
gen_val_t gen_span_ptr(gen_t* g, gen_val_t base);
gen_val_t gen_span_len(gen_t* g, gen_val_t base);
void gen_span_init(gen_t* g, gen_val_t base, gen_val_t ptr, gen_val_t len);

// Returns an `i64` length for a fixed array, span, or `string`. A fixed array uses a literal. A
// span or string uses its header field.
gen_val_t gen_length_of(gen_t* g, const type_t* t, gen_val_t base);

// Returns the element type of a fixed array, span, or `string`. A string uses the stored `char`
// type. gen_mem_type still maps each element to IR, so one function owns that mapping.
const type_t* gen_element_type(gen_t* g, const type_t* t);

// The address of element `index` of such a place, by the array shape or the
// element shape.
gen_val_t gen_element_addr(gen_t* g, const type_t* t, gen_val_t base, gen_val_t index);

// ---- aggregates ----------------------------------------------------------

// `llvm.memcpy` of `size` bytes from `src` to `dst`, the copy of every
// aggregate.
void gen_memcpy(
    gen_t* g, gen_val_t dst, uint64_t dst_align, gen_val_t src, uint64_t src_align, uint64_t size);

// `llvm.memset` of `size` zero bytes: `{}`, `del` and `move` zero a place.
void gen_memset_zero(gen_t* g, gen_val_t dst, uint64_t align, uint64_t size);

// Writes the zero value of an owning place. A pointer or `void*` stores null. A span, string, or
// owning aggregate uses `llvm.memset`. This is the value left by `move`, an implicit return move,
// and `del`. It is not an assignment, so it has no overwrite check.
void gen_zero_owner(gen_t* g, gen_place_t p);

// ---- blocks and calls -------------------------------------------------------------

// A fresh block label, `%L<N>` in creation order. The number is returned;
// gen_block_begin opens it.
uint64_t gen_label(gen_t* g);

// Opens the block `L<n>`: a blank line, the label, and a fresh terminator
// state.
void gen_block_begin(gen_t* g, uint64_t n);

// `  br label %L<n>` and `  br i1 <cond>, label %L<t>, label %L<f>`.
void gen_br(gen_t* g, uint64_t n);
void gen_br_cond(gen_t* g, gen_val_t cond, uint64_t t, uint64_t f);

// Writes the LLVM header for a fort `switch`. It then writes one label line per case and a closing
// `  ]`. The three calls write one terminator, which is the only one the emitter spells over
// several lines.
void gen_switch_begin(gen_t* g, gen_val_t operand, uint64_t default_label);
void gen_switch_case(gen_t* g, gen_val_t value, uint64_t label);
void gen_switch_end(gen_t* g);

// A call to a runtime entry point, which is declared on first use.
// The result form names the value. The void form emits none.
void gen_call_rt(gen_t* g, rt_entry_t rt, const gen_args_t* args);
gen_val_t gen_call_rt_value(gen_t* g, rt_entry_t rt, str_t ret, const gen_args_t* args);

// The `ptr @.file.N, i32 <line>, i32 <col>` every runtime failure takes: the
// position of the failure, which is the operator's token.
void gen_args_add_loc(gen_t* g, gen_args_t* args, loc_t loc);

// Writes a runtime check. It allocates the continuation label before the failure label. The failure
// block calls `rt`, writes `unreachable`, and follows all normal blocks. `fail_when` is true for
// checks whose `i1` means failure. Their branches name the failure block first. It is false only
// for `assert`, whose operand means success. The caller adds the check's own values to `args`. The
// location is appended here.
void gen_check(
    gen_t* g, gen_val_t cond, bool fail_when, rt_entry_t rt, gen_args_t* args, loc_t loc);

// Writes a failure block that no check branch reaches. An enum `switch` without `default` reaches
// it through the switch's default edge. The caller allocated `label`. The block calls `rt` and
// writes `unreachable`. This function adds the location to `args` and writes the failure buffer.
// The caller emits it before later labels.
void gen_fail_block(gen_t* g, uint64_t label, rt_entry_t rt, gen_args_t* args, loc_t loc);

// Marks an intrinsic or an attribute group used, so that only referenced
// declarations are emitted.
void gen_use_intrinsic(gen_t* g, gen_intrinsic_t which);
void gen_use_attr(gen_t* g, gen_attr_t which);

// ---- module-level data ---------------------------------------------------

// Appends `decl` to the globals section. An immutable declaration uses `dso_local constant` in
// read-only memory. A `mut` declaration uses `dso_local global`. A declaration that failed to check
// is skipped.
void gen_global(gen_t* g, const ast_node_t* decl);

// ---- private data --------------------------------------------------------

// `@.file.N` for the file of `loc`, assigned on first use, one per file.
str_t gen_file_ref(gen_t* g, loc_t loc);

// `@.str.N` holding `s` with its trailing NUL, assigned on first use and never
// deduplicated by content.
str_t gen_str_ref(gen_t* g, str_t s);

// `@.enum.<path.name>` for the table of an enum that `print` reaches.
// gen_enum_count returns the number of members.
str_t gen_enum_ref(gen_t* g, const sym_t* e);
uint64_t gen_enum_count(const sym_t* e);

// Records an `extern` function in first-use order, once per C name. Two modules can declare it as
// separate symbols that share one ELF symbol. The emitter does not declare runtime functions.
// `std.rt` is in the closure, so the calling module also holds each entry-point definition.
void gen_use_extern(gen_t* g, const sym_t* s);

// Appends the private data, the declarations and the attribute groups, then
// assembles the module into `g->out`.
void gen_finish(gen_t* g);

// ---- slots --------------------------------------------------------------

// The place of a local or parameter, whose storage gen_function made.
gen_place_t gen_slot_place(gen_t* g, const sym_t* s);

// ---- statements and expressions ---------------------------------------------------

void gen_stmt(gen_t* g, ast_node_t* n);

// A block as an ordinary block scope, which only falling off its end exits.
void gen_block(gen_t* g, ast_node_t* n);

// Emits a block as a scope of `kind`. The kind controls which exits run its deferred statements.
// Functions use GEN_SCOPE_FN, loops use GEN_SCOPE_LOOP, and cases use GEN_SCOPE_CASE.
void gen_block_scoped(gen_t* g, ast_node_t* n, gen_scope_kind_t kind);

gen_val_t gen_expr_value(gen_t* g, ast_node_t* n);

gen_place_t gen_expr_place(gen_t* g, ast_node_t* n);

// An expression written into `dst`, which is how every aggregate is produced.
// A scalar expression stores its value.
void gen_expr_into(gen_t* g, ast_node_t* n, gen_place_t dst);

// An expression evaluated for its effects alone: a call statement.
void gen_expr_discard(gen_t* g, ast_node_t* n);

// Whether `n` is a call of the `move` builtin, the one universe function with
// a value, which every expression path lowers itself.
bool gen_is_move(const ast_node_t* n);

// A place the compiler invents, `%tmp<K>` from its own counter, for an
// aggregate argument copy or a short-circuit slot.
gen_place_t gen_temp_place(gen_t* g, const type_t* t);

// The same, for a place with no fort type: the `i64` counter of a range `for`,
// whose memory type and alignment are given directly.
gen_place_t gen_temp_place_raw(gen_t* g, str_t mem_type, uint64_t align);

// Emits `a op b` for two scalars. Checked mode uses overflow intrinsics for arithmetic, unary
// minus, increments, and decrements. Release mode and wrapping operators use plain arithmetic. Both
// modes check shift counts and division. `at` is the left operand and result type. `bt` is the
// right operand type, which differs only for shifts. `loc` is the operator token for failures.
gen_val_t gen_arith(
    gen_t* g, loc_t loc, int32_t op, const type_t* at, gen_val_t a, const type_t* bt, gen_val_t b);

// The call of a builtin: the print family, `assert` and `panic`.
// Returns false when `n` is not a builtin call.
bool gen_builtin_call(gen_t* g, ast_node_t* n);

#endif

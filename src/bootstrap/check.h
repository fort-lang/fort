// The checker of the bootstrap compiler (core-language.md 3, 5, 6): it
// resolves every name to a symbol (sym.h), gives every expression a type
// (types.h), folds every constant expression (consts.h) and reports the
// program errors.
// D3 to D8, D12, D14.2
//
// Order of work. Every module of the closure is checked, in the dependency
// order the loader left: an imported module is complete before its importer
// looks at it. Within a module the pass is two phases. First it collects one
// symbol per top-level declaration, so that declarations are
// order-independent. Then it resolves each symbol lazily: the symbol of a
// declaration reached from another one is resolved on the spot, a declaration
// reached while it is being resolved closes a cycle and is reported once
// (struct sizes and constant values are resolved lazily with cycle detection),
// and the function bodies are checked last, when every declaration of the
// module has a type.
// D9.10, D14.2, D7.10
//
// Failure is local. A declaration whose type or initializer fails to check
// gets the error type and `error` on its symbol; the type table poisons
// through the error type, so every later diagnostic that involves the
// declaration is silent and an importer is still checked in full.
// D14.2
//
// Annotations. The checker writes `type` on every expression and type node,
// `sym` on every node whose own name token denotes something, `ann` bits from
// the list below and `aux`, which indexes the folded value of a constant node
// (check_node_value) and holds the byte offset of a field declaration. The
// tree, the arena and the symbols stay alive until the driver frees the
// checker, so the index walk can run after the compilation.
// D3.8
//
// The file mirrors what the self-hosted compiler will do: no unions, no
// function pointers, no macros beyond constants, one fat context passed down
// instead of globals.
#ifndef FORT_CHECK_H
#define FORT_CHECK_H

#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "consts.h"
#include "containers.h"
#include "diag.h"
#include "modules.h"
#include "scope.h"
#include "str.h"
#include "sym.h"
#include "types.h"

/// The bits of `ast_node_t.ann` the checker sets.
enum {
    /// The node is a constant expression and `aux` indexes its value:
    /// check_node_value reads it.
    /// D4.6
    CHECK_ANN_CONST = 1U,
    /// An untyped constant that has not met its context yet: its `type` is the
    /// default type until a context finalizes it.
    /// D4.1, D4.5
    CHECK_ANN_UNTYPED = 2U,
    /// AST_CALL: the callee never returns, so the call statement terminates.
    /// D8.4
    CHECK_ANN_NORETURN = 4U,
    /// A top-level declaration on the resolution path, and one whose symbol is
    /// complete: the two states of the lazy resolution.
    /// D7.10
    CHECK_ANN_RESOLVING = 8U,
    CHECK_ANN_RESOLVED = 16U,
    /// AST_SWITCH: the switch has a `default` clause or lists every member of
    /// its enum, which is what a terminating switch needs.
    /// D7.7, D8.4
    CHECK_ANN_EXHAUSTIVE = 32U,
    /// AST_RETURN: `return x` of an `own` local or parameter is an implicit
    /// `move`, so the emitter empties the operand after reading it.
    /// D17.5, D17.7
    CHECK_ANN_MOVE = 64U,
    /// One past the highest bit above. A new bit is declared as the new
    /// highest and this sentinel doubles with it, so that CHECK_ANN_ALL, which
    /// is the only mask check_module clears, never leaves one out; nothing
    /// else clears `ann`, and a bit left behind would survive into the next
    /// check of the same tree. check_own_test.c holds the assertion that ties
    /// the sentinel to the highest declared bit.
    CHECK_ANN_END = 128U,
    CHECK_ANN_ALL = CHECK_ANN_END - 1U,
};

/// Where a written type stands, which decides the markers its outermost
/// position may carry. The type table builds the type and leaves these
/// refusals to the checker, which alone knows the position (types.h).
typedef enum {
    TYPE_POS_BINDING, // D5.3: a variable, parameter or field of a literal: any marker
    TYPE_POS_FIELD,   // D5.5: a struct field: no outermost `mut`
    TYPE_POS_RETURN,  // D5.5: a return type: no outermost `mut`
    TYPE_POS_CAST,    // D3.14: a cast target: a result has no binding
    TYPE_POS_ALLOC,   // D5.8: inside `new`: every level is allocated writable
} type_pos_t;

/// Whether `move` and `del` may empty an lvalue, and why not when they may not.
/// Emptying is not an assignment, so a binding's own `mut` does not decide it:
/// `del` on an immutable `own` binding is legal, while `move` out of a slot
/// reached through an immutable level is not.
/// D17.6, D17.9
typedef enum {
    EMPTY_OK,        // a binding's own storage, or a mutable indirection
    EMPTY_READONLY,  // D7.10: a module-level constant, or a member of one
    EMPTY_IMMUTABLE, // an indirection whose level is immutable
} empty_kind_t;

/// A written type after resolution: the type and the mutability of level 0, which
/// lives outside the type. The error type marks a failure.
/// D5.2
typedef struct {
    const type_t* type;
    bool mut0;
} check_type_t;

/// A checked expression. `type` is never NULL and is the error type after a
/// failure; `value` is CV_NONE unless the expression is a constant expression;
/// `untyped` marks a constant that still takes its type from context; `lvalue`
/// and `mut` are level-0 answers; `sym` is the declaration an identifier or a
/// field access denotes, NULL otherwise, so that a diagnostic can name it.
/// D4.1, D4.6, D5.7, D6.7
typedef struct {
    const type_t* type;
    cval_t value;
    bool untyped;
    bool lvalue;
    bool mut;
    /// Whether `move` and `del` may empty this lvalue, and why not when they
    /// may not. Meaningless unless `lvalue`.
    /// D17.6, D17.9
    empty_kind_t empty;
    /// Usable as a module-level initializer: every constant expression, and
    /// also `null`, a function name, `&` of a module-level declaration and a
    /// literal whose members are all of those.
    /// D7.10
    bool init_const;
    const sym_t* sym;
} expr_t;

typedef struct {
    type_table_t types; // owned: every type the annotations point to
    ptrvec_t syms;      // sym_t*, owned; alive until check_free
    ptrvec_t values;    // cval_t*, owned: the folded values `aux` indexes
    sb_t msg;           // the message builder of diag.h
    /// The names an import of the module being checked failed to bind: the
    /// loader reported each one, so a use of one is silent, as a use of a
    /// declaration that failed to check is.
    /// D14.2
    strmap_t bad_imports;
    /// The first `extern fn` declaration of each C symbol, over the whole closure:
    /// `extern_first` maps the C name to a position in `externs`, which holds that
    /// declaration's symbol. A later declaration of the same symbol is held against
    /// it. The symbols are owned by `syms`.
    /// D9.8
    strmap_t extern_first;
    ptrvec_t externs;
    const sym_t* builtins[UNIVERSE_COUNT]; // D12.2: the universe functions
    bool mute;                             // D20.2: annotate without reporting, for an editor mode
    bool require_main; // D8.6: the entry module defines main; off for a check-only run
    uint64_t errors;   // checker diagnostics, muted ones included

    // ---- the module and the function being checked ----
    const module_t* module;
    const sym_t* module_sym;
    const sym_t* fn_sym; // the function whose body is being checked
    /// The callee of the call being resolved: an `extern fn` name is a callee
    /// and nowhere a value, so the value path tells the two apart.
    /// D3.10
    const ast_node_t* callee;
    /// Whether the operand of a `&` is being checked, which asks for an address and
    /// not for a value: `node N = node{&N};` is a legal self-pointing sentinel, since
    /// `&` of a module-level declaration is admitted from any module, this one
    /// included, and the value of `N` does not depend on the value of `N`. The lazy
    /// resolution would otherwise read the reference as a cycle, its guard being
    /// unable to tell "I need your value" from "I need your address".
    /// D4.6, D7.10
    bool addr_only;
    scope_t* scope;      // the innermost block scope, NULL at module level
    const type_t* ret;   // the return type of the function being checked
    bool ret_void;       // D7.11: its return type is `void`
    bool ret_noreturn;   // D8.5: it is `fn noreturn`
    bool in_function;    // a body is being checked
    uint64_t loops;      // D7.5: enclosing loops: `continue` needs one
    uint64_t switches;   // D7.6: enclosing switches: `break` takes either
    uint64_t defers;     // D7.8: enclosing deferred statements
    uint64_t block_errs; // D14.2: AST_ERROR nodes seen in the body
} check_t;

void check_init(check_t* ck);

/// Releases the symbols, the values and the type table. Every annotation the
/// checker wrote points into them, so the tree must not be read afterwards.
void check_free(check_t* ck);

/// Checks the closure: every module the loader put in the dependency order first,
/// then every other module that parsed, deepest first, so that a file whose import
/// failed is still checked and an editor sees its own errors. A module that did
/// not parse is not checked. Returns whether none reported an error.
/// D9.10, D14.2, D20.1
bool check_program(check_t* ck, const module_set_t* set);

/// Checks one module. Every module it imports must have been checked already,
/// which check_program's order guarantees.
bool check_module(check_t* ck, const module_t* m);

/// The folded value of a node the checker marked CHECK_ANN_CONST, and CV_NONE
/// for every other node.
cval_t check_node_value(const check_t* ck, const ast_node_t* n);

/// The symbols in the order they were made, for the index walk.
uint64_t check_sym_count(const check_t* ck);
const sym_t* check_sym_at(const check_t* ck, uint64_t i);

// ---- shared with check_stmt.c -----------------------------------------------------

/// The one funnel every checker diagnostic goes through: it counts the error
/// and reports it unless the checker is muted.
/// D14.2
void check_error(check_t* ck, loc_t loc, const char* msg);

/// Begins a message in `ck->msg`; check_msg_end finishes it and reports it at
/// `loc`. Between them the caller appends with the msg_* functions of diag.h and
/// with check_msg_type, which writes a type in the canonical spelling.
/// D5.3
void check_msg_begin(check_t* ck);
void check_msg_type(check_t* ck, const type_t* t);
void check_msg_end(check_t* ck, loc_t loc);

/// `t` lent: the same type without the `own` of the reference it holds, which
/// is what `==`, a range loop and a borrowed use of an owning value see.
/// D6.2, D17.4, D17.10
const type_t* check_lend(check_t* ck, const type_t* t);

/// Whether a value of `t` owns an allocation: an `own` reference or an owning
/// aggregate, which are the two things `move` transfers and `del` refuses. A
/// struct still without a layout answers false, the declaration that needs it
/// having reported its own error.
/// D3.8, D17.1, D17.7
bool check_owning(const type_t* t);

/// Reports "owning temporary would leak" at `loc` when `e` is an owning rvalue;
/// `what` says what the expression would do with it. Returns whether it reported.
/// D17.8
bool check_owning_temporary(check_t* ck, loc_t loc, const expr_t* e, const char* what);

/// Reports "there is no pointer arithmetic" when `t` is a pointer, and returns
/// whether it did.
/// D10.4
bool check_pointer_arithmetic(check_t* ck, loc_t loc, int32_t op, const type_t* t);

/// Whether `t` already failed: no diagnostic mentions a poisoned type, which is
/// how one error stays one error.
/// D14.2
bool check_poisoned(const type_t* t);

/// The range of the closing brace of a construct whose range ends at it, where
/// "missing return" and a non-exhaustive `switch` are reported.
/// D14.2
loc_t check_close_brace(loc_t loc);

/// A new symbol, owned by the checker and recorded for the index walk.
sym_t* check_sym_new(
    check_t* ck, sym_kind_t kind, str_t name, const ast_node_t* node, const sym_t* owner);

/// The type a written type node denotes at `pos`, with the marker refusals the
/// type table delegates; the error type after a diagnostic.
/// D3.14, D5.5
check_type_t check_type(check_t* ck, ast_node_t* node, type_pos_t pos);

/// Reports "type is too large" at `loc` when the size of `t` passes the ceiling,
/// and returns whether the type may be used.
/// D3.4
bool check_size_fits(check_t* ck, loc_t loc, const type_t* t);

/// Lays out the struct behind `t` when it needs it, so that sizeof and a
/// declaration of that type are exact; false when the layout failed.
/// D3.8
bool check_layout(check_t* ck, const type_t* t);

/// Checks one expression and annotates its node.
void check_expr(check_t* ck, ast_node_t* node, expr_t* out);

/// Checks an expression that must produce a value of `target`: it finalizes an
/// untyped constant against it and reports a type that does not convert. `what`
/// names the context in the diagnostic. An owning target is an own place, so an
/// owning lvalue reaches it only through `move`.
/// D4.1, D5.4, D17.5
void check_expr_as(
    check_t* ck, ast_node_t* node, const type_t* target, const char* what, expr_t* out);

/// Checks the operand of `ret`, an AST_RETURN with a value, against `target`. It
/// is the one own place an owning lvalue reaches without `move`, and only when it
/// names a local or a parameter outright, which is an implicit move the checker
/// records on `ret` for the emitter.
/// D17.5, D17.7
void check_return_value(check_t* ck, ast_node_t* ret, const type_t* target, expr_t* out);

/// Checks the initializer of a declaration of type `target`, which may be a bare
/// `{ ... }`.
/// D6.5
void check_initializer(
    check_t* ck, ast_node_t* node, const type_t* target, const char* what, expr_t* out);

/// `a op b` on two checked operands: it finalizes an untyped operand against the
/// other, applies the operand rules of the operator, folds two constants and
/// reports every violation at `loc`. The compound assignments have the operand
/// rules of their operator, so they use it too.
/// D4.1, D4.6, D6.2, D7.2
void check_operands(check_t* ck,
                    loc_t loc,
                    int32_t op,
                    ast_node_t* lhs,
                    expr_t* a,
                    ast_node_t* rhs,
                    expr_t* b,
                    expr_t* out);

/// Checks an expression used as a value with no context, so an untyped constant
/// takes its default type.
/// D4.5
void check_expr_default(check_t* ck, ast_node_t* node, expr_t* out);

/// Checks a `bool` condition.
/// D3.3, D7.4
void check_condition(check_t* ck, ast_node_t* node, const char* what);

/// Declares a local, a parameter or a range variable in the innermost scope with
/// the shadowing rules, and gives its node a symbol.
/// D7.9
sym_t* check_declare_local(
    check_t* ck, ast_node_t* node, sym_kind_t kind, const type_t* type, bool mut0);

/// Checks the body of a function declaration, whose symbol is resolved.
void check_function_body(check_t* ck, ast_node_t* fn, const sym_t* sym);

/// Checks one statement and, for a block, the scope it opens.
void check_stmt(check_t* ck, ast_node_t* node);
void check_block(check_t* ck, ast_node_t* node);

/// Whether the statement always leaves the enclosing block.
/// D8.4
bool check_terminates(const ast_node_t* node);

#endif

// The checker of the bootstrap compiler (core-language.md 3, 5, 6; D3 to D8,
// D12): it resolves every name to a symbol (sym.h), gives every expression a
// type (types.h), folds every constant expression (consts.h) and reports the
// program errors of D14.2.
//
// Order of work. Every module of the closure is checked, in the dependency
// order the loader left (D9.10, D14.2 as amended): an imported module is
// complete before its importer looks at it. Within a module the pass is two
// phases. First it collects one symbol per top-level declaration, so that
// declarations are order-independent (D7.10). Then it resolves each symbol
// lazily: the symbol of a declaration reached from another one is resolved on
// the spot, a declaration reached while it is being resolved closes a cycle
// and is reported once (D7.10: struct sizes and constant values are resolved
// lazily with cycle detection), and the function bodies are checked last,
// when every declaration of the module has a type.
//
// Failure is local. A declaration whose type or initializer fails to check
// gets the error type and `error` on its symbol; the type table poisons
// through the error type, so every later diagnostic that involves the
// declaration is silent and an importer is still checked in full (D14.2).
//
// Annotations. The checker writes `type` on every expression and type node,
// `sym` on every node whose own name token denotes something, `ann` bits from
// the list below and `aux`, which indexes the folded value of a constant node
// (check_node_value) and holds the byte offset of a field declaration (D3.8).
// The tree, the arena and the symbols stay alive until the driver frees the
// checker, so the index walk can run after the compilation.
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

// The bits of `ast_node_t.ann` the checker sets.
enum {
    // The node is a constant expression (D4.6) and `aux` indexes its value:
    // check_node_value reads it.
    CHECK_ANN_CONST = 1U,
    // An untyped constant that has not met its context yet (D4.1): its
    // `type` is the default type of D4.5 until a context finalizes it.
    CHECK_ANN_UNTYPED = 2U,
    // AST_CALL: the callee never returns, so the call statement terminates
    // (D8.4).
    CHECK_ANN_NORETURN = 4U,
    // A top-level declaration on the resolution path, and one whose symbol is
    // complete (D7.10): the two states of the lazy resolution.
    CHECK_ANN_RESOLVING = 8U,
    CHECK_ANN_RESOLVED = 16U,
    // AST_SWITCH: the switch has a `default` clause or lists every member of
    // its enum, which is what a terminating switch needs (D7.7, D8.4).
    CHECK_ANN_EXHAUSTIVE = 32U,
};

// Where a written type stands, which decides the markers its outermost
// position may carry. The type table builds the type and leaves these
// refusals to the checker, which alone knows the position (types.h).
typedef enum {
    TYPE_POS_BINDING, // a variable, parameter or field of a literal: any marker (D5.3)
    TYPE_POS_FIELD,   // a struct field: no outermost `mut` (D5.5)
    TYPE_POS_RETURN,  // a return type: no outermost `mut` (D5.5)
    TYPE_POS_CAST,    // a cast target: a result has no binding (D3.14)
    TYPE_POS_ALLOC,   // inside `new`: every level is allocated writable (D5.8)
} type_pos_t;

// A written type after resolution: the type and the mutability of level 0,
// which lives outside the type (D5.2). The error type marks a failure.
typedef struct {
    const type_t* type;
    bool mut0;
} check_type_t;

// A checked expression. `type` is never NULL and is the error type after a
// failure; `value` is CV_NONE unless the expression is a constant expression
// (D4.6); `untyped` marks a constant that still takes its type from context
// (D4.1); `lvalue` and `mut` are D6.7 and D5.7; `sym` is the declaration an
// identifier or a field access denotes, NULL otherwise, so that a diagnostic
// can name it.
typedef struct {
    const type_t* type;
    cval_t value;
    bool untyped;
    bool lvalue;
    bool mut;
    // Usable as a module-level initializer (D7.10): every constant
    // expression, and also `null`, a function name, `&` of a module-level
    // declaration and a literal whose members are all of those.
    bool init_const;
    const sym_t* sym;
} expr_t;

typedef struct {
    type_table_t types;                    // owned: every type the annotations point to
    ptrvec_t syms;                         // sym_t*, owned; alive until check_free
    ptrvec_t values;                       // cval_t*, owned: the folded values `aux` indexes
    sb_t msg;                              // the message builder of diag.h
    const sym_t* builtins[UNIVERSE_COUNT]; // the universe functions of D12.2
    bool mute;                             // annotate without reporting, for an editor mode (D20.2)
    bool require_main; // the entry module defines main (D8.6); off for a check-only run
    uint64_t errors;   // checker diagnostics, muted ones included

    // ---- the module and the function being checked ----
    const module_t* module;
    const sym_t* module_sym;
    const sym_t* fn_sym; // the function whose body is being checked
    scope_t* scope;      // the innermost block scope, NULL at module level
    const type_t* ret;   // the return type of the function being checked
    bool ret_void;       // its return type is `void` (D7.11)
    bool ret_noreturn;   // it is `fn noreturn` (D8.5)
    bool in_function;    // a body is being checked
    uint64_t loops;      // enclosing loops: `continue` needs one (D7.5)
    uint64_t switches;   // enclosing switches: `break` takes either (D7.6)
    uint64_t defers;     // enclosing deferred statements (D7.8)
    uint64_t block_errs; // AST_ERROR nodes seen in the body (D14.2)
} check_t;

void check_init(check_t* ck);

// Releases the symbols, the values and the type table. Every annotation the
// checker wrote points into them, so the tree must not be read afterwards.
void check_free(check_t* ck);

// Checks every module of the closure in dependency order (D9.10, D14.2) and
// returns whether none reported an error.
bool check_program(check_t* ck, const module_set_t* set);

// Checks one module. Every module it imports must have been checked already,
// which check_program's order guarantees.
bool check_module(check_t* ck, const module_t* m);

// The folded value of a node the checker marked CHECK_ANN_CONST, and CV_NONE
// for every other node.
cval_t check_node_value(const check_t* ck, const ast_node_t* n);

// The symbols in the order they were made, for the index walk.
uint64_t check_sym_count(const check_t* ck);
const sym_t* check_sym_at(const check_t* ck, uint64_t i);

// ---- shared with check_stmt.c -----------------------------------------------------

// The one funnel every checker diagnostic goes through (D14.2): it counts the
// error and reports it unless the checker is muted.
void check_error(check_t* ck, loc_t loc, const char* msg);

// Begins a message in `ck->msg`; check_msg_end finishes it and reports it at
// `loc`. Between them the caller appends with the msg_* functions of diag.h
// and with check_msg_type, which writes a type as D5.3 spells it.
void check_msg_begin(check_t* ck);
void check_msg_type(check_t* ck, const type_t* t);
void check_msg_end(check_t* ck, loc_t loc);

// `t` lent: the same type without the `own` of the reference it holds, which
// is what `==`, a range loop and a borrowed use of an owning value see
// (D6.2, D17.4, D17.10).
const type_t* check_lend(check_t* ck, const type_t* t);

// Reports "there is no pointer arithmetic" when `t` is a pointer, and
// returns whether it did: `p + 1`, `p++` and `p[i]` are errors (D10.4).
bool check_pointer_arithmetic(check_t* ck, loc_t loc, int32_t op, const type_t* t);

// Whether `t` already failed: no diagnostic mentions a poisoned type, which
// is how one error stays one error (D14.2).
bool check_poisoned(const type_t* t);

// The range of the closing brace of a construct whose range ends at it, where
// "missing return" and a non-exhaustive `switch` are reported (D14.2).
loc_t check_close_brace(loc_t loc);

// A new symbol, owned by the checker and recorded for the index walk.
sym_t* check_sym_new(
    check_t* ck, sym_kind_t kind, str_t name, const ast_node_t* node, const sym_t* owner);

// The type a written type node denotes at `pos`, with the marker refusals the
// type table delegates (D5.5, D3.14); the error type after a diagnostic.
check_type_t check_type(check_t* ck, ast_node_t* node, type_pos_t pos);

// Reports "type is too large" at `loc` when the size of `t` passes the
// ceiling of D3.4, and returns whether the type may be used.
bool check_size_fits(check_t* ck, loc_t loc, const type_t* t);

// Lays out the struct behind `t` when it needs it, so that sizeof and a
// declaration of that type are exact (D3.8); false when the layout failed.
bool check_layout(check_t* ck, const type_t* t);

// Checks one expression and annotates its node.
void check_expr(check_t* ck, ast_node_t* node, expr_t* out);

// Checks an expression that must produce a value of `target`: it finalizes an
// untyped constant against it (D4.1) and reports a type that does not convert
// (D5.4). `what` names the context in the diagnostic.
void check_expr_as(
    check_t* ck, ast_node_t* node, const type_t* target, const char* what, expr_t* out);

// Checks the initializer of a declaration of type `target`, which may be the
// bare `{ ... }` of D6.5.
void check_initializer(
    check_t* ck, ast_node_t* node, const type_t* target, const char* what, expr_t* out);

// `a op b` on two checked operands (D6.2): it finalizes an untyped operand
// against the other (D4.1), applies the operand rules of the operator, folds
// two constants (D4.6) and reports every violation at `loc`. The compound
// assignments of D7.2 have the operand rules of their operator, so they use
// it too.
void check_operands(check_t* ck,
                    loc_t loc,
                    int32_t op,
                    ast_node_t* lhs,
                    expr_t* a,
                    ast_node_t* rhs,
                    expr_t* b,
                    expr_t* out);

// Checks an expression used as a value with no context, so an untyped
// constant takes its default type (D4.5).
void check_expr_default(check_t* ck, ast_node_t* node, expr_t* out);

// Checks a `bool` condition (D3.3, D7.4).
void check_condition(check_t* ck, ast_node_t* node, const char* what);

// Declares a local, a parameter or a range variable in the innermost scope
// with the shadowing rules of D7.9, and gives its node a symbol.
sym_t* check_declare_local(
    check_t* ck, ast_node_t* node, sym_kind_t kind, const type_t* type, bool mut0);

// Checks the body of a function declaration, whose symbol is resolved.
void check_function_body(check_t* ck, ast_node_t* fn, const sym_t* sym);

// Checks one statement and, for a block, the scope it opens.
void check_stmt(check_t* ck, ast_node_t* node);
void check_block(check_t* ck, ast_node_t* node);

// Whether the statement always leaves the enclosing block (D8.4).
bool check_terminates(const ast_node_t* node);

#endif

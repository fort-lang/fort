// Checks names, types, constants, and semantic rules.
// It annotates syntax trees and reports diagnostics.
//
// The checker collects declarations, resolves dependencies, and checks bodies.
// A failed declaration gets the error type, which suppresses dependent diagnostics.
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
    // The node is a constant expression and `aux` indexes its value:
    // check_node_value reads it.
    CHECK_ANN_CONST = 1U,
    // An untyped constant that has not met its context yet: its `type` is the
    // default type until a context finalizes it.
    CHECK_ANN_UNTYPED = 2U,
    // AST_CALL: the callee never returns, so the call statement terminates.
    CHECK_ANN_NORETURN = 4U,
    // A top-level declaration on the resolution path, and one whose symbol is
    // complete: the two states of the lazy resolution.
    CHECK_ANN_RESOLVING = 8U,
    CHECK_ANN_RESOLVED = 16U,
    // AST_SWITCH: the switch has a `default` clause or lists every member of
    // its enum, which is what a terminating switch needs.
    CHECK_ANN_EXHAUSTIVE = 32U,
    // AST_RETURN: `return x` of an `own` local or parameter is an implicit
    // `move`, so the emitter empties the operand after reading it.
    CHECK_ANN_MOVE = 64U,
    // One past the highest annotation bit. A new highest bit doubles this
    // sentinel. CHECK_ANN_ALL then includes each bit that check_module clears.
    // No other code clears `ann`. A missed bit would survive the next check.
    // check_own_test.c compares this sentinel with the highest declared bit.
    CHECK_ANN_END = 128U,
    CHECK_ANN_ALL = CHECK_ANN_END - 1U,
};

// Where a written type stands, which decides the markers its outermost
// position may carry. The type table builds the type and leaves these
// refusals to the checker, which alone knows the position (types.h).
typedef enum {
    TYPE_POS_BINDING, // a variable, parameter or field of a literal: any marker
    TYPE_POS_FIELD,   // a struct field: no outermost `mut`
    TYPE_POS_RETURN,  // a return type: no outermost `mut`
    TYPE_POS_CAST,    // a cast target: a result has no binding
    TYPE_POS_ALLOC,   // inside `new`: the outermost position is allocated
} type_pos_t;

// Whether `move` and `del` may empty an lvalue, and why not when they may not.
// Emptying is not an assignment, so a binding's own `mut` does not decide it.
// `del` on an immutable `own` binding is legal. `move` through an immutable
// level is not legal.
typedef enum {
    EMPTY_OK,        // a binding's own storage, or a mutable indirection
    EMPTY_READONLY,  // a module-level constant, or a member of one
    EMPTY_IMMUTABLE, // an indirection whose level is immutable
} empty_kind_t;

// A written type after resolution: the type and the mutability of level 0, which
// lives outside the type. The error type marks a failure.
typedef struct {
    const type_t* type;
    bool mut0;
} check_type_t;

// A checked expression. `type` is never NULL. It is the error type after a
// failure. `value` is CV_NONE unless the expression is constant. `untyped`
// marks a constant that still takes its type from context. `lvalue` and `mut`
// are level-0 answers. `sym` identifies the declaration for an identifier or
// field access. It is NULL otherwise.
typedef struct {
    const type_t* type;
    cval_t value;
    bool untyped;
    bool lvalue;
    bool mut;
    // Whether `move` and `del` may empty this lvalue, and why not when they
    // may not. Meaningless unless `lvalue`.
    empty_kind_t empty;
    // Usable as a module-level initializer. This includes each constant
    // expression, `null`, a function name, and an address of a module
    // declaration. It also includes literals made from these values.
    bool init_const;
    const sym_t* sym;
} expr_t;

typedef struct {
    type_table_t types; // owned: every type the annotations point to
    ptrvec_t syms;      // sym_t*, owned; alive until check_free
    ptrvec_t values;    // cval_t*, owned: the folded values `aux` indexes
    sb_t msg;           // the message builder of diag.h
    // Names that an import failed to bind. The loader reported each name, so
    // later uses are silent. Uses of declarations that failed are also silent.
    strmap_t bad_imports;
    // The first `extern fn` declaration for each C symbol in the closure.
    // `extern_first` maps the C name to a position in `externs`. That vector
    // holds the declaration symbol. Later declarations are compared with it.
    // `syms` owns the symbols.
    strmap_t extern_first;
    ptrvec_t externs;
    const sym_t* builtins[UNIVERSE_COUNT]; // the universe functions
    bool mute;                             // annotate without reporting, for an editor mode
    bool require_main; // the entry module defines main; off for a check-only run
    uint64_t errors;   // checker diagnostics, muted ones included

    // ---- the module and the function being checked ----
    const module_t* module;
    const sym_t* module_sym;
    const sym_t* fn_sym; // the function whose body is being checked
    // The callee of the call being resolved. An `extern fn` name is a callee,
    // never a value. The value path uses this field to distinguish the cases.
    const ast_node_t* callee;
    // Whether the checker needs an operand address instead of its value.
    // `node N = node{&N};` is a legal self-reference. An address of a module
    // declaration is available from all modules, including its own module.
    // The value of `N` does not depend on itself. Without this flag, lazy
    // resolution would report a cycle because its guard cannot distinguish
    // value access from address access.
    bool addr_only;
    scope_t* scope;      // the innermost block scope, NULL at module level
    const type_t* ret;   // the return type of the function being checked
    bool ret_void;       // its return type is `void`
    bool ret_noreturn;   // the function does not return
    bool in_function;    // a body is being checked
    uint64_t loops;      // enclosing loops: `continue` needs one
    uint64_t switches;   // enclosing switches: `break` takes either
    uint64_t defers;     // enclosing deferred statements
    uint64_t block_errs; // AST_ERROR nodes seen in the body
} check_t;

void check_init(check_t* ck);

// Releases the symbols, the values and the type table. Every annotation the
// checker wrote points into them, so the tree must not be read afterwards.
void check_free(check_t* ck);

// Checks the closure. Dependency-ordered modules come first. Other parsed
// modules follow, deepest first. Thus, a file with a failed import still gets
// its own diagnostics. A module that did not parse is not checked. Returns true
// when no module reported an error.
bool check_program(check_t* ck, const module_set_t* set);

// Checks one module. Every module it imports must have been checked already.
// The order from check_program guarantees this condition.
bool check_module(check_t* ck, const module_t* m);

// The folded value of a node the checker marked CHECK_ANN_CONST, and CV_NONE
// for every other node.
cval_t check_node_value(const check_t* ck, const ast_node_t* n);

// The symbols in the order they were made, for the index walk.
uint64_t check_sym_count(const check_t* ck);
const sym_t* check_sym_at(const check_t* ck, uint64_t i);

// ---- shared with check_stmt.c -----------------------------------------------------

// The one funnel every checker diagnostic goes through: it counts the error
// and reports it unless the checker is muted.
void check_error(check_t* ck, loc_t loc, const char* msg);

// Begins a message in `ck->msg`; check_msg_end finishes it and reports it at
// `loc`. Between them the caller appends with the msg_* functions of diag.h and
// with check_msg_type, which writes a type in the canonical spelling.
void check_msg_begin(check_t* ck);
void check_msg_type(check_t* ck, const type_t* t);
void check_msg_end(check_t* ck, loc_t loc);

// Returns `t` without the `own` mark of its stored reference. Equality, range
// loops, and borrowed uses of owning values see this type.
const type_t* check_lend(check_t* ck, const type_t* t);

// Whether a value of `t` owns an allocation: an `own` reference or an owning
// aggregate, which are the two things `move` transfers and `del` refuses. A
// struct still without a layout answers false, the declaration that needs it
// having reported its own error.
bool check_owning(const type_t* t);

// Reports "owning temporary would leak" at `loc` when `e` is an owning rvalue;
// `what` says what the expression would do with it. Returns whether it reported.
bool check_owning_temporary(check_t* ck, loc_t loc, const expr_t* e, const char* what);

// Reports "there is no pointer arithmetic" when `t` is a pointer, and returns
// whether it did.
bool check_pointer_arithmetic(check_t* ck, loc_t loc, int32_t op, const type_t* t);

// Whether `t` already failed: no diagnostic mentions a poisoned type, which is
// how one error stays one error.
bool check_poisoned(const type_t* t);

// The range of the closing brace of a construct whose range ends at it, where
// "missing return" and a non-exhaustive `switch` are reported.
loc_t check_close_brace(loc_t loc);

// A new symbol, owned by the checker and recorded for the index walk.
sym_t* check_sym_new(
    check_t* ck, sym_kind_t kind, str_t name, const ast_node_t* node, const sym_t* owner);

// The type a written type node denotes at `pos`, with the marker refusals the
// type table delegates; the error type after a diagnostic.
check_type_t check_type(check_t* ck, ast_node_t* node, type_pos_t pos);

// Reports "type is too large" at `loc` when the size of `t` passes the ceiling
// and returns whether the type may be used.
bool check_size_fits(check_t* ck, loc_t loc, const type_t* t);

// Lays out the struct behind `t` when needed. This makes its size and
// declarations exact. Returns false when layout fails.
bool check_layout(check_t* ck, const type_t* t);

// Checks one expression and annotates its node.
void check_expr(check_t* ck, ast_node_t* node, expr_t* out);

// Checks an operator operand that gets no context and drops poison. The thirteen
// positions include `*e`, `e.f`, `e->f`, `e.len`, `e[i]`, and `e[a..b]`. They
// also include `e()`, `&e`, `del(e)`, `move(e)`, an assignment or `++` target,
// and a range `for` collection. An untyped constant with
// no default type is poisoned and unreported. It takes its default type here.
// This reports the error or restores the operand when its value fits. Every
// other operand reaches the caller unchanged.
void check_operand(check_t* ck, ast_node_t* node, expr_t* out);

// Checks an expression that must produce a value of `target`: it finalizes an
// untyped constant against it and reports a type that does not convert. `what`
// names the context in the diagnostic. An owning target is an own place, so an
// owning lvalue reaches it only through `move`.
void check_expr_as(
    check_t* ck, ast_node_t* node, const type_t* target, const char* what, expr_t* out);

// Checks the operand of `ret`, an AST_RETURN with a value, against `target`. It
// is the one own place that an owning lvalue reaches without `move`. The lvalue
// must directly name a local or parameter. The checker records this implicit
// move on `ret` for the emitter.
void check_return_value(check_t* ck, ast_node_t* ret, const type_t* target, expr_t* out);

// Checks the initializer of a declaration of type `target`, which may be a bare
// `{... }`.
void check_initializer(
    check_t* ck, ast_node_t* node, const type_t* target, const char* what, expr_t* out);

// Checks `a op b` on two checked operands. It finalizes an untyped operand
// against the other operand. It applies the operator rules, folds two
// constants, and reports each violation at `loc`. Compound assignments use the
// same rules.
void check_operands(check_t* ck,
                    loc_t loc,
                    int32_t op,
                    ast_node_t* lhs,
                    expr_t* a,
                    ast_node_t* rhs,
                    expr_t* b,
                    expr_t* out);

// Checks an expression used as a value with no context, so an untyped constant
// takes its default type.
void check_expr_default(check_t* ck, ast_node_t* node, expr_t* out);

// Checks a `bool` condition.
void check_condition(check_t* ck, ast_node_t* node, const char* what);

// Declares a local, a parameter or a range variable in the innermost scope with
// the shadowing rules, and gives its node a symbol.
sym_t* check_declare_local(
    check_t* ck, ast_node_t* node, sym_kind_t kind, const type_t* type, bool mut0);

// Checks the body of a function declaration, whose symbol is resolved.
void check_function_body(check_t* ck, ast_node_t* fn, const sym_t* sym);

// Checks one statement and, for a block, the scope it opens.
void check_stmt(check_t* ck, ast_node_t* node);
void check_block(check_t* ck, ast_node_t* node);

// Whether the statement always leaves the enclosing block.
bool check_terminates(const ast_node_t* node);

#endif

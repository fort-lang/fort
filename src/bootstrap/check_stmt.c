// The statements of the checker (core-language.md 6, D7, D8.4); see check.h.
//
// One function per statement form, the scope chain of D7.9 opened and closed
// as blocks are entered and left, and the structural rule of D8.4 in
// check_terminates, which reads the annotations the pass left rather than
// walking the tree a second time.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ast.h"
#include "check.h"
#include "consts.h"
#include "diag.h"
#include "lexer.h"
#include "prim.h"
#include "scope.h"
#include "str.h"
#include "sym.h"
#include "types.h"

// ---- locals (D7.1, D7.9) -----------------------------------------------------------

sym_t* check_declare_local(
    check_t* ck, ast_node_t* node, sym_kind_t kind, const type_t* type, bool mut0) {
    sym_t* s = check_sym_new(ck, kind, node->name, node, ck->fn_sym);
    s->type = type;
    s->mut0 = mut0;
    s->error = check_poisoned(type);
    node->sym = s;
    node->type = type;
    if (ck->scope == NULL) {
        return s;
    }
    const binding_t* here = scope_find(ck->scope, node->name);
    const binding_t* outer = scope_find_in_blocks(ck->scope, node->name);
    if (here != NULL) {
        check_msg_begin(ck);
        msg_quote(&ck->msg, node->name);
        msg_str(&ck->msg, " is already declared in this block");
        check_msg_end(ck, node->name_loc);
    } else if (outer != NULL) {
        // A local or parameter may not reuse the name of an enclosing local
        // or parameter; it may shadow a module-level or universe name (D7.9),
        // and the diagnostic stands at the inner declaration (D14.2).
        check_msg_begin(ck);
        msg_quote(&ck->msg, node->name);
        msg_str(&ck->msg,
                outer->kind == BIND_PARAM ? " shadows a parameter" : " shadows an enclosing local");
        check_msg_end(ck, node->name_loc);
    }
    if (here == NULL) {
        (void)scope_declare(ck->scope,
                            node->name,
                            kind == SYM_PARAM ? BIND_PARAM : BIND_LOCAL,
                            node->name_loc,
                            node);
    }
    return s;
}

// `Type name = init;` (D7.1): the initializer is checked before the name is
// declared, so a local is visible only after its own declaration (D7.9).
static void check_local(check_t* ck, ast_node_t* n) {
    const check_type_t t = check_type(ck, n->a, TYPE_POS_BINDING);
    const type_t* type = t.type;
    if (!check_poisoned(type) && (!check_layout(ck, type) || !check_size_fits(ck, n->loc, type))) {
        type = type_error(&ck->types);
    }
    if (!check_poisoned(type) && type->kind == TYPE_VOID) {
        check_error(ck, n->loc, "'void' is only a return type or the base of 'void*'");
        type = type_error(&ck->types);
    }
    if (n->b != NULL) {
        expr_t e;
        check_initializer(ck, n->b, type, "the initializer", &e);
    }
    (void)check_declare_local(ck, n, SYM_LOCAL, type, t.mut0);
}

// ---- assignment (D7.2, D5.7) -------------------------------------------------------

// The operator of a compound assignment: `lv op= e` has the operand rules and
// the overflow behavior of `lv op e` (D7.2).
static int32_t compound_operator(int32_t op) {
    switch (op) {
    case TOK_PLUS_ASSIGN:
        return TOK_PLUS;
    case TOK_MINUS_ASSIGN:
        return TOK_MINUS;
    case TOK_STAR_ASSIGN:
        return TOK_STAR;
    case TOK_SLASH_ASSIGN:
        return TOK_SLASH;
    case TOK_PERCENT_ASSIGN:
        return TOK_PERCENT;
    case TOK_PLUS_WRAP_ASSIGN:
        return TOK_PLUS_WRAP;
    case TOK_MINUS_WRAP_ASSIGN:
        return TOK_MINUS_WRAP;
    case TOK_STAR_WRAP_ASSIGN:
        return TOK_STAR_WRAP;
    case TOK_AMP_ASSIGN:
        return TOK_AMP;
    case TOK_PIPE_ASSIGN:
        return TOK_PIPE;
    case TOK_CARET_ASSIGN:
        return TOK_CARET;
    case TOK_SHL_ASSIGN:
        return TOK_SHL;
    case TOK_SHR_ASSIGN:
        return TOK_SHR;
    default:
        break;
    }
    return TOK_ASSIGN;
}

// Assignment, compound assignment, `++` and `--` all require a mutable lvalue
// (D5.7).
static bool check_target(check_t* ck, ast_node_t* n, expr_t* lv, const char* verb) {
    if (check_poisoned(lv->type)) {
        return false;
    }
    if (!lv->lvalue) {
        check_msg_begin(ck);
        msg_str(&ck->msg, "cannot ");
        msg_str(&ck->msg, verb);
        msg_str(&ck->msg, " a value that is not an lvalue");
        check_msg_end(ck, n->loc);
        return false;
    }
    if (!lv->mut) {
        check_msg_begin(ck);
        msg_str(&ck->msg, "cannot ");
        msg_str(&ck->msg, verb);
        msg_str(&ck->msg, " immutable ");
        if (lv->sym != NULL) {
            // A field's own storage follows the value that contains it, so
            // the diagnostic says which one is immutable (D5.5, D5.7).
            if (lv->sym->kind == SYM_FIELD) {
                msg_str(&ck->msg, "field ");
            }
            msg_quote(&ck->msg, lv->sym->name);
        } else {
            check_msg_type(ck, lv->type);
        }
        check_msg_end(ck, n->loc);
        return false;
    }
    return true;
}

static void check_assign(check_t* ck, ast_node_t* n) {
    expr_t lv;
    check_expr(ck, n->a, &lv);
    const bool ok = check_target(ck, n->a, &lv, "assign to");
    if (n->op == TOK_ASSIGN) {
        expr_t e;
        check_expr_as(ck, n->b, ok ? lv.type : type_error(&ck->types), "the assignment", &e);
        return;
    }
    expr_t rhs;
    if (!ok) {
        // The target already failed: the value is checked for its own errors
        // and the operator says nothing more (D14.2).
        check_expr_default(ck, n->b, &rhs);
        return;
    }
    check_expr(ck, n->b, &rhs);
    expr_t result;
    check_operands(ck, n->loc, compound_operator(n->op), n->a, &lv, n->b, &rhs, &result);
}

static void check_incdec(check_t* ck, ast_node_t* n) {
    expr_t lv;
    check_expr(ck, n->a, &lv);
    if (!check_target(ck, n->a, &lv, "modify")) {
        return;
    }
    // `++` and `--` add or subtract 1 with the checks of `+` and `-` and are
    // allowed on integer types only (D7.2).
    if (lv.type->kind != TYPE_PRIM || !prim_is_integer(lv.type->prim)) {
        if (check_pointer_arithmetic(ck, n->loc, n->op, lv.type)) {
            return;
        }
        check_msg_begin(ck);
        msg_str(&ck->msg, "'");
        msg_str(&ck->msg, tok_kind_name((tok_kind_t)n->op));
        msg_str(&ck->msg, "' takes an integer operand, not ");
        check_msg_type(ck, lv.type);
        check_msg_end(ck, n->loc);
    }
}

// An expression statement is a call whose result is discarded, unless the
// result owns memory nothing could then free (D7.3, D17.8).
static void check_call_stmt(check_t* ck, ast_node_t* n) {
    expr_t e;
    check_expr(ck, n->a, &e);
    if (check_poisoned(e.type)) {
        return;
    }
    // An owning result is an `own` reference or an owning aggregate (D17.7),
    // and nothing could free either once it is dropped (D17.8).
    if ((type_is_reference(e.type) && e.type->own) || type_is_owning_aggregate(e.type)) {
        check_msg_begin(ck);
        msg_str(&ck->msg, "owning result discarded: bind it or del it");
        check_msg_end(ck, n->loc);
    }
}

// ---- control flow (D7.4, D7.5) -----------------------------------------------------

static void check_if(check_t* ck, ast_node_t* n) {
    check_condition(ck, n->a, "a condition");
    check_block(ck, n->b);
    if (n->c != NULL) {
        // The else branch is a block or the `if` of an `else if` (D7.4).
        check_stmt(ck, n->c);
    }
}

static void check_while(check_t* ck, ast_node_t* n, bool tail) {
    ck->loops++;
    // In `do`, variables declared in the body are not visible in the
    // condition (D7.5), which the separate scopes give for free.
    if (tail) {
        check_block(ck, n->a);
        check_condition(ck, n->b, "a condition");
    } else {
        check_condition(ck, n->a, "a condition");
        check_block(ck, n->b);
    }
    ck->loops--;
}

// `for (init; cond; step) { }` (D7.5): a variable declared in `init` is scoped
// to the loop, so the header and the body share one scope.
static void check_for(check_t* ck, ast_node_t* n) {
    scope_t header;
    scope_init(&header, SCOPE_BLOCK, ck->scope);
    ck->scope = &header;
    if (n->a != NULL) {
        check_stmt(ck, n->a);
    }
    if (n->b != NULL) {
        check_condition(ck, n->b, "a condition");
    }
    ck->loops++;
    if (n->c != NULL) {
        check_stmt(ck, n->c);
    }
    check_block(ck, n->d);
    ck->loops--;
    ck->scope = header.parent;
    scope_free(&header);
}

// `for (T x : coll) { }` (D7.5, D17.10): the collection is a fixed array, span
// or string, the variable a fresh copy of each element, and the loop lends an
// owning collection.
static void check_range_for(check_t* ck, ast_node_t* n) {
    scope_t header;
    scope_init(&header, SCOPE_BLOCK, ck->scope);
    ck->scope = &header;
    expr_t coll;
    check_expr(ck, n->b, &coll);
    const check_type_t decl = check_type(ck, n->a, TYPE_POS_BINDING);
    const type_t* elem = NULL;
    if (!check_poisoned(coll.type)) {
        if (coll.type->kind == TYPE_ARRAY || coll.type->kind == TYPE_SPAN) {
            elem = coll.type->elem;
        } else if (coll.type->kind == TYPE_STRING) {
            elem = type_prim(&ck->types, PRIM_CHAR);
        } else {
            check_msg_begin(ck);
            msg_str(&ck->msg, "cannot iterate ");
            check_msg_type(ck, coll.type);
            check_msg_end(ck, n->b->loc);
        }
    }
    const type_t* want = elem;
    if (elem != NULL && type_is_reference(elem) && elem->own) {
        // The loop lends its collection: the element type loses its outermost
        // `own` and an `own` range variable is an error (D17.10).
        want = check_lend(ck, elem);
    }
    if (!check_poisoned(decl.type) && type_is_reference(decl.type) && decl.type->own) {
        check_error(ck, n->a->loc, "a range variable cannot be own");
    } else if (elem != NULL && !check_poisoned(decl.type) && !type_equal(decl.type, want)) {
        check_msg_begin(ck);
        msg_str(&ck->msg, "the element type is ");
        check_msg_type(ck, want);
        msg_str(&ck->msg, ", not ");
        check_msg_type(ck, decl.type);
        check_msg_end(ck, n->a->loc);
    } else if (elem != NULL && type_is_owning_aggregate(elem)) {
        // A copy of an owning aggregate would need a `move`, so an index loop
        // is the way (D17.10).
        check_error(ck, n->b->loc, "the elements are owning: iterate by index");
    }
    (void)check_declare_local(ck, n, SYM_LOCAL, decl.type, decl.mut0);
    ck->loops++;
    check_block(ck, n->c);
    ck->loops--;
    ck->scope = header.parent;
    scope_free(&header);
}

// ---- switch (D7.6, D7.7) -----------------------------------------------------------

// Whether any label of the switch has the value `v`, which is how both the
// duplicate rule and the exhaustiveness rule read the clauses.
static const ast_node_t* label_with_value(check_t* ck,
                                          const ast_node_t* sw,
                                          cval_t v,
                                          const ast_node_t* before) {
    for (uint64_t i = 0; i < ast_len(sw); i++) {
        const ast_node_t* clause = ast_child(sw, i);
        if (clause->kind != AST_CASE) {
            continue;
        }
        for (uint64_t k = 0; k < ast_len(clause); k++) {
            const ast_node_t* label = ast_child(clause, k);
            if (label == before) {
                return NULL;
            }
            if (cv_eq(check_node_value(ck, label), v)) {
                return label;
            }
        }
    }
    return NULL;
}

// Every member of the enum is listed, or the switch has a `default` (D7.7).
static bool check_exhaustive(check_t* ck, ast_node_t* n, const type_t* t) {
    const sym_t* s = (const sym_t*)t->decl;
    if (s == NULL || s->node == NULL) {
        return true;
    }
    bool complete = true;
    check_msg_begin(ck);
    msg_str(&ck->msg, "switch over ");
    msg_view(&ck->msg, t->name);
    msg_str(&ck->msg, " does not handle ");
    for (uint64_t i = 0; i < ast_len(s->node); i++) {
        const ast_node_t* m = ast_child(s->node, i);
        if (m->kind != AST_ENUM_MEMBER) {
            continue;
        }
        if (label_with_value(ck, n, check_node_value(ck, m), NULL) != NULL) {
            continue;
        }
        if (!complete) {
            msg_str(&ck->msg, ", ");
        }
        msg_view(&ck->msg, m->name);
        complete = false;
    }
    if (!complete) {
        // A non-exhaustive enum switch is reported at the closing brace of the
        // switch (D14.2).
        check_msg_end(ck, check_close_brace(n->loc));
    }
    return complete;
}

static void check_switch(check_t* ck, ast_node_t* n) {
    expr_t op;
    // An untyped constant operand takes its default type (D4.5).
    check_expr_default(ck, n->a, &op);
    const type_t* t = op.type;
    if (!check_poisoned(t) && t->kind != TYPE_ENUM &&
        (t->kind != TYPE_PRIM || t->prim == PRIM_BOOL || prim_is_float(t->prim))) {
        // The operand has an integer, `char` or enum type (D7.6).
        check_msg_begin(ck);
        msg_str(&ck->msg, "cannot switch on ");
        check_msg_type(ck, t);
        check_msg_end(ck, n->a->loc);
        t = type_error(&ck->types);
    }
    const ast_node_t* first_default = NULL;
    ck->switches++;
    for (uint64_t i = 0; i < ast_len(n); i++) {
        ast_node_t* clause = ast_child(n, i);
        // An error node stands where a clause was expected (D14.2).
        if (clause->kind != AST_CASE) {
            continue;
        }
        if ((clause->flags & AST_FLAG_DEFAULT) != 0) {
            if (first_default != NULL) {
                // At most one `default`, in any position (D7.6).
                check_error(ck, clause->loc, "a switch has at most one 'default'");
            }
            first_default = clause;
        }
        for (uint64_t k = 0; k < ast_len(clause); k++) {
            ast_node_t* label = ast_child(clause, k);
            expr_t e;
            // Each label is a constant expression convertible to the
            // operand's type (D7.6).
            check_expr_as(ck, label, t, "a case label", &e);
            if (check_poisoned(e.type)) {
                continue;
            }
            if (e.value.kind == CV_NONE) {
                check_error(ck, label->loc, "a case label must be a constant expression");
                continue;
            }
            if (label_with_value(ck, n, e.value, label) != NULL) {
                // No duplicates after evaluation (D7.6).
                check_msg_begin(ck);
                msg_str(&ck->msg, "duplicate case value ");
                cv_to_str(e.value, &ck->msg);
                check_msg_end(ck, label->loc);
            }
        }
        // Each case body is an implicit block scope (D7.6).
        check_block(ck, clause->a);
    }
    ck->switches--;
    bool exhaustive = first_default != NULL;
    if (!exhaustive && !check_poisoned(t) && t->kind == TYPE_ENUM) {
        exhaustive = check_exhaustive(ck, n, t);
    }
    if (exhaustive) {
        n->ann |= CHECK_ANN_EXHAUSTIVE;
    }
}

// ---- defer, return, break and continue (D7.8, D7.11, D8.5) -------------------------

// `return`, `break` and `continue` may not appear inside deferred code
// (D7.8).
static bool check_outside_defer(check_t* ck, ast_node_t* n, const char* what) {
    if (ck->defers == 0) {
        return true;
    }
    check_msg_begin(ck);
    msg_str(&ck->msg, "'");
    msg_str(&ck->msg, what);
    msg_str(&ck->msg, "' inside deferred code");
    check_msg_end(ck, n->loc);
    return false;
}

static void check_return(check_t* ck, ast_node_t* n) {
    if (!check_outside_defer(ck, n, "return")) {
        return;
    }
    if (check_poisoned(ck->ret)) {
        // The signature failed to check, so nothing is known about what this
        // returns: the error type silences it (D14.2).
        if (n->a != NULL) {
            expr_t e;
            check_expr_default(ck, n->a, &e);
        }
        return;
    }
    if (ck->ret_noreturn) {
        // A `noreturn` function may not contain `return` (D8.5).
        check_error(ck, n->loc, "'return' in a noreturn function");
        return;
    }
    if (n->a == NULL) {
        if (!ck->ret_void) {
            // `return;` in a non-void function is an error (D7.11).
            check_msg_begin(ck);
            msg_str(&ck->msg, "'return' without a value in a function returning ");
            check_msg_type(ck, ck->ret);
            check_msg_end(ck, n->loc);
        }
        return;
    }
    if (ck->ret_void) {
        // `return e;` in a void function is an error (D7.11).
        check_error(ck, n->loc, "'return' with a value in a void function");
        expr_t e;
        check_expr_default(ck, n->a, &e);
        return;
    }
    expr_t e;
    check_expr_as(ck, n->a, ck->ret, "the return value", &e);
}

static void check_break(check_t* ck, ast_node_t* n, bool cont) {
    if (!check_outside_defer(ck, n, cont ? "continue" : "break")) {
        return;
    }
    // `break` exits the innermost loop or switch, `continue` continues the
    // innermost loop (D7.6).
    if (cont ? ck->loops == 0 : ck->loops == 0 && ck->switches == 0) {
        check_error(
            ck, n->loc, cont ? "'continue' outside a loop" : "'break' outside a loop or switch");
    }
}

static void check_defer(check_t* ck, ast_node_t* n) {
    // `defer` takes an assignment, a `++`/`--`, a call statement or a block
    // (D7.8), which the parser has already enforced.
    ck->defers++;
    check_stmt(ck, n->a);
    ck->defers--;
}

// ---- terminating statements (D8.4) -------------------------------------------------

// A `break` that targets the statement: the walk does not enter a nested loop
// or switch, which a `break` inside would target instead (D7.6).
static bool has_break(const ast_node_t* n) {
    if (n == NULL) {
        return false;
    }
    switch (n->kind) {
    case AST_BREAK:
        return true;
    case AST_WHILE:
    case AST_DO_WHILE:
    case AST_FOR:
    case AST_RANGE_FOR:
    case AST_SWITCH:
        return false;
    case AST_BLOCK:
    case AST_IF:
    case AST_DEFER:
        break;
    default:
        return false;
    }
    if (has_break(n->a) || has_break(n->b) || has_break(n->c) || has_break(n->d)) {
        return true;
    }
    for (uint64_t i = 0; i < ast_len(n); i++) {
        if (has_break(ast_child(n, i))) {
            return true;
        }
    }
    return false;
}

// `while (true)`: the literal, which is the only spelling D8.4 gives.
static bool is_true_literal(const ast_node_t* n) {
    return n != NULL && n->kind == AST_BOOL && n->ival != 0;
}

bool check_terminates(const ast_node_t* n) {
    if (n == NULL) {
        return false;
    }
    switch (n->kind) {
    case AST_RETURN:
        return true;
    case AST_CALL_STMT:
        // A call to a `noreturn` function, `panic` included, terminates
        // (D8.4).
        return n->a != NULL && (n->a->ann & CHECK_ANN_NORETURN) != 0;
    case AST_IF:
        // An `if` with an `else` whose branches all terminate (D8.4).
        return n->c != NULL && check_terminates(n->b) && check_terminates(n->c);
    case AST_WHILE:
        return is_true_literal(n->a) && !has_break(n->b);
    case AST_FOR:
        // `for (;;)` or any `for` with an empty condition, with no `break`
        // targeting it (D8.4).
        return n->b == NULL && !has_break(n->d);
    case AST_SWITCH: {
        // A switch all of whose cases terminate, that has a `default` or is
        // an exhaustive enum switch (D8.4, D7.7).
        if ((n->ann & CHECK_ANN_EXHAUSTIVE) == 0 || ast_len(n) == 0) {
            return false;
        }
        for (uint64_t i = 0; i < ast_len(n); i++) {
            const ast_node_t* clause = ast_child(n, i);
            if (clause->kind != AST_CASE || !check_terminates(clause->a)) {
                return false;
            }
        }
        return true;
    }
    case AST_BLOCK: {
        const uint64_t count = ast_len(n);
        for (uint64_t i = 0; i < count; i++) {
            // A block that holds the tokens of a syntax error is not judged:
            // what it would have terminated with is unknown (D14.2).
            if (ast_child(n, i)->kind == AST_ERROR) {
                return true;
            }
        }
        // A block whose last statement terminates (D8.4).
        return count > 0 && check_terminates(ast_child(n, count - 1));
    }
    default:
        break;
    }
    return false;
}

// ---- blocks and bodies -------------------------------------------------------------

void check_stmt(check_t* ck, ast_node_t* n) {
    switch (n->kind) {
    case AST_BLOCK:
        // A bare block is a statement and a scope (D7.3).
        check_block(ck, n);
        break;
    case AST_VAR_DECL:
        check_local(ck, n);
        break;
    case AST_ASSIGN:
        check_assign(ck, n);
        break;
    case AST_INCDEC:
        check_incdec(ck, n);
        break;
    case AST_CALL_STMT:
        check_call_stmt(ck, n);
        break;
    case AST_IF:
        check_if(ck, n);
        break;
    case AST_WHILE:
        check_while(ck, n, false);
        break;
    case AST_DO_WHILE:
        check_while(ck, n, true);
        break;
    case AST_FOR:
        check_for(ck, n);
        break;
    case AST_RANGE_FOR:
        check_range_for(ck, n);
        break;
    case AST_SWITCH:
        check_switch(ck, n);
        break;
    case AST_DEFER:
        check_defer(ck, n);
        break;
    case AST_RETURN:
        check_return(ck, n);
        break;
    case AST_BREAK:
        check_break(ck, n, false);
        break;
    case AST_CONTINUE:
        check_break(ck, n, true);
        break;
    case AST_ERROR:
        // The tokens a syntax error made the parser skip; every pass that
        // walks a statement list skips one (D14.2).
        ck->block_errs++;
        break;
    default:
        fatal_internal("check: not a statement");
    }
}

void check_block(check_t* ck, ast_node_t* n) {
    if (n == NULL || n->kind != AST_BLOCK) {
        return;
    }
    scope_t block;
    scope_init(&block, SCOPE_BLOCK, ck->scope);
    ck->scope = &block;
    for (uint64_t i = 0; i < ast_len(n); i++) {
        check_stmt(ck, ast_child(n, i));
    }
    ck->scope = block.parent;
    scope_free(&block);
}

void check_function_body(check_t* ck, ast_node_t* fn, const sym_t* sym) {
    const type_t* signature = sym->type;
    const bool poisoned = check_poisoned(signature) || signature->kind != TYPE_FN;
    ck->ret = poisoned ? type_error(&ck->types) : signature->elem;
    ck->ret_void = !poisoned && ck->ret->kind == TYPE_VOID;
    ck->ret_noreturn = !poisoned && signature->noreturn;
    ck->in_function = true;
    ck->fn_sym = sym;
    ck->loops = 0;
    ck->switches = 0;
    ck->defers = 0;
    ck->block_errs = 0;
    // The parameters are a scope of their own around the body, so that a
    // local may not reuse a parameter's name (D7.9).
    scope_t params;
    scope_init(&params, SCOPE_BLOCK, NULL);
    ck->scope = &params;
    for (uint64_t i = 0; i < ast_len(fn); i++) {
        ast_node_t* p = ast_child(fn, i);
        if (p->kind != AST_PARAM || p->sym == NULL) {
            continue;
        }
        if (scope_declare(&params, p->name, BIND_PARAM, p->name_loc, p) == NULL) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "duplicate parameter ");
            msg_quote(&ck->msg, p->name);
            check_msg_end(ck, p->name_loc);
        }
    }
    check_block(ck, fn->b);
    if (ck->block_errs == 0 && !check_poisoned(ck->ret) && !check_terminates(fn->b)) {
        if (ck->ret_noreturn) {
            // A `noreturn` function must end in a terminating statement
            // (D8.5).
            check_error(ck,
                        check_close_brace(fn->b->loc),
                        "a noreturn function must end in a terminating statement");
        } else if (!ck->ret_void) {
            // A non-void function body must end in a terminating statement,
            // reported at the body's closing brace (D8.4, D14.2).
            check_error(ck, check_close_brace(fn->b->loc), "missing return");
        }
    }
    ck->scope = NULL;
    scope_free(&params);
    ck->in_function = false;
    ck->fn_sym = NULL;
    ck->ret = NULL;
}

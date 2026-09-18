// The statements of the checker; see check.h.
//
// Checks statements and manages block scopes. Each statement form has one
// function. check_terminates applies structural termination rules. It reads
// annotations from this pass instead of walking the tree again.
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

// ---- locals ------------------------------------------------------------------------

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
        // reported at the inner declaration
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

// `Type name = init;`: the initializer is checked before the name is declared,
// so a local is visible only after its own declaration.
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

// ---- assignment --------------------------------------------------------------------

// The operator of a compound assignment: `lv op= e` has the operand rules and
// the overflow behavior of `lv op e`.
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

// Assignment, compound assignment, `++` and `--` all require a mutable lvalue.
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
            // the diagnostic says which value is immutable
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
    check_operand(ck, n->a, &lv);
    const bool ok = check_target(ck, n->a, &lv, "assign to");
    if (n->op == TOK_ASSIGN) {
        expr_t e;
        check_expr_as(ck, n->b, ok ? lv.type : type_error(&ck->types), "the assignment", &e);
        return;
    }
    expr_t rhs;
    if (!ok) {
        // the target already failed, so the operator says nothing
        check_expr_default(ck, n->b, &rhs);
        return;
    }
    check_expr(ck, n->b, &rhs);
    expr_t result;
    check_operands(ck, n->loc, compound_operator(n->op), n->a, &lv, n->b, &rhs, &result);
}

static void check_incdec(check_t* ck, ast_node_t* n) {
    expr_t lv;
    check_operand(ck, n->a, &lv);
    if (!check_target(ck, n->a, &lv, "modify")) {
        return;
    }
    // integer types only, with the checks of `+` and `-`
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
// result owns memory nothing could then free.
static void check_call_stmt(check_t* ck, ast_node_t* n) {
    expr_t e;
    check_expr(ck, n->a, &e);
    if (check_poisoned(e.type)) {
        return;
    }
    (void)check_owning_temporary(
        ck, n->loc, &e, "bind it to an 'own' place, pass it on or 'del' it");
}

// ---- control flow ------------------------------------------------------------------

static void check_if(check_t* ck, ast_node_t* n) {
    check_condition(ck, n->a, "a condition");
    check_block(ck, n->b);
    if (n->c != NULL) {
        check_stmt(ck, n->c);
    }
}

static void check_while(check_t* ck, ast_node_t* n, bool tail) {
    ck->loops++;
    // the separate scopes give this for free
    if (tail) {
        check_block(ck, n->a);
        check_condition(ck, n->b, "a condition");
    } else {
        check_condition(ck, n->a, "a condition");
        check_block(ck, n->b);
    }
    ck->loops--;
}

// `for (init; cond; step) { }`: a variable declared in `init` is scoped to the
// loop, so the header and the body share one scope.
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

// Checks `for (T x: coll) { }`. The collection is an array, span, or string.
// The variable gets a fresh element copy. The loop borrows an owning collection.
static void check_range_for(check_t* ck, ast_node_t* n) {
    scope_t header;
    scope_init(&header, SCOPE_BLOCK, ck->scope);
    ck->scope = &header;
    expr_t coll;
    check_operand(ck, n->b, &coll);
    if (check_owning_temporary(ck, n->b->loc, &coll, "the loop only lends its collection")) {
        coll.type = type_error(&ck->types);
    }
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
        // the element type loses its outermost `own`
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
    } else if (elem != NULL && check_owning(elem) && !type_is_reference(elem)) {
        // a copy would need a `move`, so iterate by index
        check_error(ck, n->b->loc, "the elements are owning: iterate by index");
    }
    (void)check_declare_local(ck, n, SYM_LOCAL, decl.type, decl.mut0);
    ck->loops++;
    check_block(ck, n->c);
    ck->loops--;
    ck->scope = header.parent;
    scope_free(&header);
}

// ---- switch ------------------------------------------------------------------------

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

// Every member of the enum is listed, or the switch has a `default`.
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
        // reported at the closing brace of the switch
        check_msg_end(ck, check_close_brace(n->loc));
    }
    return complete;
}

static void check_switch(check_t* ck, ast_node_t* n) {
    expr_t op;
    check_expr_default(ck, n->a, &op);
    const type_t* t = op.type;
    if (!check_poisoned(t) && t->kind != TYPE_ENUM &&
        (t->kind != TYPE_PRIM || t->prim == PRIM_BOOL || prim_is_float(t->prim))) {
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
        if (clause->kind != AST_CASE) {
            continue;
        }
        if ((clause->flags & AST_FLAG_DEFAULT) != 0) {
            if (first_default != NULL) {
                check_error(ck, clause->loc, "a switch has at most one 'default'");
            }
            first_default = clause;
        }
        for (uint64_t k = 0; k < ast_len(clause); k++) {
            ast_node_t* label = ast_child(clause, k);
            expr_t e;
            check_expr_as(ck, label, t, "a case label", &e);
            if (check_poisoned(e.type)) {
                continue;
            }
            if (e.value.kind == CV_NONE) {
                check_error(ck, label->loc, "a case label must be a constant expression");
                continue;
            }
            if (label_with_value(ck, n, e.value, label) != NULL) {
                check_msg_begin(ck);
                msg_str(&ck->msg, "duplicate case value ");
                cv_to_str(e.value, &ck->msg);
                check_msg_end(ck, label->loc);
            }
        }
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

// ---- defer, return, break and continue ---------------------------------------------

// `return` may not appear inside deferred code: it would leave the function in
// the middle of an unwind. `break` and `continue` may, and bind to a loop or a
// switch written inside the deferred block.
static bool check_return_outside_defer(check_t* ck, ast_node_t* n) {
    if (ck->defers == 0) {
        return true;
    }
    check_error(ck, n->loc, "'return' inside deferred code");
    return false;
}

static void check_return(check_t* ck, ast_node_t* n) {
    if (!check_return_outside_defer(ck, n)) {
        return;
    }
    if (check_poisoned(ck->ret)) {
        // the signature failed, so the error type silences this
        if (n->a != NULL) {
            expr_t e;
            check_expr_default(ck, n->a, &e);
        }
        return;
    }
    if (ck->ret_noreturn) {
        check_error(ck, n->loc, "'return' in a noreturn function");
        return;
    }
    if (n->a == NULL) {
        if (!ck->ret_void) {
            check_msg_begin(ck);
            msg_str(&ck->msg, "'return' without a value in a function returning ");
            check_msg_type(ck, ck->ret);
            check_msg_end(ck, n->loc);
        }
        return;
    }
    if (ck->ret_void) {
        check_error(ck, n->loc, "'return' with a value in a void function");
        expr_t e;
        check_expr_default(ck, n->a, &e);
        return;
    }
    expr_t e;
    // the one `own` place reached without `move`
    check_return_value(ck, n, ck->ret, &e);
}

// Counts loops and switches that `check_defer` left reachable. A `break` or
// `continue` in deferred code sees only constructs inside the deferred block.
static void check_break(check_t* ck, ast_node_t* n, bool cont) {
    if (cont ? ck->loops == 0 : ck->loops == 0 && ck->switches == 0) {
        check_error(
            ck, n->loc, cont ? "'continue' outside a loop" : "'break' outside a loop or switch");
    }
}

// A `break` or `continue` in deferred code binds inside the deferred block.
// Constructs around the `defer` are not reachable while it is checked. Without
// an inner target, these statements are errors. The message matches a `break`
// outside all loops and switches.
static void check_defer(check_t* ck, ast_node_t* n) {
    // the parser has already enforced this
    const uint64_t loops = ck->loops;
    const uint64_t switches = ck->switches;
    ck->loops = 0;
    ck->switches = 0;
    ck->defers++;
    check_stmt(ck, n->a);
    ck->defers--;
    ck->switches = switches;
    ck->loops = loops;
}

// ---- terminating statements --------------------------------------------------------

// Whether a `break` targets this statement. The walk skips nested loops and
// switches because their breaks target them. It also skips deferred code because
// its breaks target constructs inside the deferred block.
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
    case AST_DEFER:
        return false;
    case AST_BLOCK:
    case AST_IF:
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

// `while (true)`: the literal, which is the only spelling the rule gives.
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
        return n->a != NULL && (n->a->ann & CHECK_ANN_NORETURN) != 0;
    case AST_IF:
        return n->c != NULL && check_terminates(n->b) && check_terminates(n->c);
    case AST_WHILE:
        return is_true_literal(n->a) && !has_break(n->b);
    case AST_FOR:
        return n->b == NULL && !has_break(n->d);
    case AST_SWITCH: {
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
            // what the block would have terminated with is unknown
            if (ast_child(n, i)->kind == AST_ERROR) {
                return true;
            }
        }
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
    // a local may not reuse a parameter's name
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
            check_error(ck,
                        check_close_brace(fn->b->loc),
                        "a noreturn function must end in a terminating statement");
        } else if (!ck->ret_void) {
            // reported at the body's closing brace
            check_error(ck, check_close_brace(fn->b->loc), "missing return");
        }
    }
    ck->scope = NULL;
    scope_free(&params);
    ck->in_function = false;
    ck->fn_sym = NULL;
    ck->ret = NULL;
}

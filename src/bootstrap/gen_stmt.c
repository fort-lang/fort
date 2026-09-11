// The statements of the LLVM IR emitter (toolchain.md 6 item 10, D19.4); see
// gen.h.
//
// One function per statement form. Control flow is explicit blocks, so after
// a terminating statement the emitter opens a fresh `%L<N>` block for the
// unreachable statements D14.2 allows.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ast.h"
#include "check.h"
#include "diag.h"
#include "gen.h"
#include "lexer.h"
#include "str.h"
#include "sym.h"
#include "types.h"

// The operator a compound assignment applies, or TOK_ASSIGN for a plain one:
// `lv op= e` has the operand rules and the overflow behavior of `lv op e`
// (D7.2).
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

// `Type name = init;` (D7.1): the local's storage is the entry-block alloca
// gen_function made, so the statement is the initializer alone.
static void gen_var_decl(gen_t* g, ast_node_t* n) {
    if (n->b == NULL || n->sym == NULL) {
        return;
    }
    gen_expr_into(g, n->b, gen_slot_place(g, n->sym));
}

static void gen_assign(gen_t* g, ast_node_t* n) {
    // The target is evaluated before the value, which is the source order the
    // walk keeps (D6.3).
    const gen_place_t target = gen_expr_place(g, n->a);
    if (g->failed) {
        return;
    }
    const int32_t op = compound_operator(n->op);
    if (op == TOK_ASSIGN) {
        if (!g->opts.release && type_is_reference(target.type) && target.type->own) {
            // Storing over a live `own` value traps in the checked mode
            // (D11.1, D17.11), and that check is T-022's: the bare store below
            // is the release-mode lowering and would silently be a checked
            // build without its check (item 18).
            gen_todo(g, n->loc, "an assignment to an owning reference");
            return;
        }
        gen_expr_into(g, n->b, target);
        return;
    }
    const gen_val_t lhs = gen_load_place(g, target);
    const gen_val_t rhs = gen_expr_value(g, n->b);
    // A compound assignment uses the intrinsics of its operator (item 15).
    gen_store_place(g, target, gen_arith(g, n->loc, op, n->a->type, lhs, n->b->type, rhs));
}

// `++` and `--` use the same intrinsics as `+` and `-` (item 15).
static void gen_incdec(gen_t* g, ast_node_t* n) {
    const gen_place_t target = gen_expr_place(g, n->a);
    if (g->failed) {
        return;
    }
    const gen_val_t before = gen_load_place(g, target);
    const gen_val_t one = gen_const_unsigned(g, before.ty, 1);
    const int32_t op = n->op == TOK_PLUS_PLUS ? TOK_PLUS : TOK_MINUS;
    gen_store_place(g, target, gen_arith(g, n->loc, op, n->a->type, before, n->a->type, one));
}

static void gen_return(gen_t* g, ast_node_t* n) {
    if (n->a == NULL) {
        gen_ins(g);
        gen_text_append(g, "ret void");
        gen_ins_end(g);
        g->terminated = true;
        return;
    }
    if (gen_is_aggregate(n->a->type)) {
        // An aggregate result is written through the `sret` pointer and the
        // function returns `void` (item 7).
        gen_place_t sret;
        sret.addr.ty = str_from_cstr("ptr");
        sret.addr.val = str_from_cstr("%ret.sret");
        sret.type = n->a->type;
        gen_expr_into(g, n->a, sret);
        gen_ins(g);
        gen_text_append(g, "ret void");
        gen_ins_end(g);
        g->terminated = true;
        return;
    }
    const gen_val_t v = gen_expr_value(g, n->a);
    gen_ins(g);
    gen_text_append(g, "ret ");
    gen_text_append_str(g, v.ty);
    gen_text_append(g, " ");
    gen_text_append_str(g, v.val);
    gen_ins_end(g);
    g->terminated = true;
}

// ---- control flow (item 10, D19.4) ------------------------------------------------

// `if (cond) { } else if (cond) { } else { }` (D7.4): the condition branches
// to the then block and to the else block, or to the continuation when there
// is no `else`, and each branch that has not terminated branches to the
// continuation. Every block the emitter opens therefore ends in exactly one
// terminator (item 10).
static void gen_if(gen_t* g, ast_node_t* n) {
    const gen_val_t cond = gen_expr_value(g, n->a);
    if (g->failed) {
        return;
    }
    const bool has_else = n->c != NULL;
    // Labels are `%L<N>` in creation order, so the order they are allocated
    // in is part of the emitted text (D19.5).
    const uint64_t then_label = gen_label(g);
    const uint64_t else_label = has_else ? gen_label(g) : 0;
    const uint64_t done = gen_label(g);
    gen_br_cond(g, cond, then_label, has_else ? else_label : done);
    gen_block_begin(g, then_label);
    gen_block(g, n->b);
    if (!g->terminated) {
        gen_br(g, done);
    }
    if (has_else) {
        gen_block_begin(g, else_label);
        // The `else` is a block, or the `if` of an `else if` chain (D7.4).
        gen_stmt(g, n->c);
        if (!g->terminated) {
            gen_br(g, done);
        }
    }
    gen_block_begin(g, done);
}

// The body of a loop, with `break` and `continue` targeting it: they target
// the innermost enclosing loop, so the enclosing targets are saved and
// restored around it (D7.5).
static void gen_loop_body(gen_t* g, ast_node_t* body, uint64_t brk, uint64_t cont) {
    const uint64_t saved_break = g->loop_break;
    const uint64_t saved_continue = g->loop_continue;
    g->loop_break = brk;
    g->loop_continue = cont;
    g->loop_depth++;
    gen_block(g, body);
    g->loop_depth--;
    g->loop_break = saved_break;
    g->loop_continue = saved_continue;
}

// `while (cond) { }` (D7.5): a head block that re-evaluates the condition, a
// body that branches back to it, and a continuation. `continue` targets the
// head, `break` the continuation.
static void gen_while(gen_t* g, ast_node_t* n) {
    const uint64_t head = gen_label(g);
    const uint64_t body = gen_label(g);
    const uint64_t done = gen_label(g);
    gen_br(g, head);
    gen_block_begin(g, head);
    const gen_val_t cond = gen_expr_value(g, n->a);
    if (g->failed) {
        return;
    }
    gen_br_cond(g, cond, body, done);
    gen_block_begin(g, body);
    gen_loop_body(g, n->b, done, head);
    if (!g->terminated) {
        gen_br(g, head);
    }
    gen_block_begin(g, done);
}

// `for (init; cond; step) { }` (D7.5): `init` runs in the block the loop
// stands in, the head tests the condition, the body branches to the step
// block and the step branches back to the head. `continue` targets the step
// block, because `continue` in a `for` runs `step`; an empty `cond` means
// `true`, which is what `for (;;)` is.
static void gen_for(gen_t* g, ast_node_t* n) {
    if (n->a != NULL) {
        gen_stmt(g, n->a);
    }
    if (g->failed) {
        return;
    }
    const uint64_t head = gen_label(g);
    const uint64_t body = gen_label(g);
    const uint64_t step = gen_label(g);
    const uint64_t done = gen_label(g);
    if (!g->terminated) {
        // `init` may be a call (D7.5) and a call to a `noreturn` function
        // already ended the block (D8.4), so the entry edge is conditional on
        // the block still being open: every block ends in exactly one
        // terminator (item 10).
        gen_br(g, head);
    }
    gen_block_begin(g, head);
    if (n->b == NULL) {
        // An empty condition is `true`, so the head branches straight in
        // (D7.5).
        gen_br(g, body);
    } else {
        const gen_val_t cond = gen_expr_value(g, n->b);
        if (g->failed) {
            return;
        }
        gen_br_cond(g, cond, body, done);
    }
    gen_block_begin(g, body);
    gen_loop_body(g, n->d, done, step);
    if (!g->terminated) {
        gen_br(g, step);
    }
    gen_block_begin(g, step);
    if (n->c != NULL) {
        gen_stmt(g, n->c);
    }
    if (g->failed) {
        return;
    }
    if (!g->terminated) {
        // `step` may be a call too, with the same consequence as `init`.
        gen_br(g, head);
    }
    gen_block_begin(g, done);
}

// The copy of one element into the loop variable's slot: a scalar is loaded
// and stored, an aggregate is copied with `llvm.memcpy` (D19.3).
static void copy_into(gen_t* g, gen_place_t dst, gen_place_t src) {
    if (gen_is_aggregate(dst.type)) {
        const uint64_t align = type_alignof(dst.type);
        gen_memcpy(g, dst.addr, align, src.addr, align, type_sizeof(dst.type));
        return;
    }
    gen_store_place(g, dst, gen_load_place(g, src));
}

// The place the collection of a range `for` is iterated over (D7.5, D17.10).
// The loop lends an owning collection, which is iterated in place and never
// moved or copied; a collection that owns nothing is evaluated as a value
// into a temporary, so that it is evaluated once before the first iteration
// and the loop iterates over the copy.
static gen_place_t range_collection(gen_t* g, ast_node_t* coll) {
    if (coll->type != NULL && (type_is_owning_aggregate(coll->type) ||
                               (type_is_reference(coll->type) && coll->type->own))) {
        return gen_expr_place(g, coll);
    }
    const gen_place_t tmp = gen_temp_place(g, coll->type);
    gen_expr_into(g, coll, tmp);
    return tmp;
}

// `for (T x : coll) { }` (D7.5): an `i64` counter the compiler invents walks
// the collection, and the loop variable is a fresh copy of each element taken
// at the start of its iteration. The counter is bounded by the collection's
// length, so its increment is a plain `add` and not a checked one (item 15).
static void gen_range_for(gen_t* g, ast_node_t* n) {
    const gen_place_t coll = range_collection(g, n->b);
    if (g->failed || n->sym == NULL || n->b->type == NULL) {
        return;
    }
    const str_t i64 = str_from_cstr("i64");
    const gen_place_t index = gen_temp_place_raw(g, i64, (uint64_t)sizeof(uint64_t));
    gen_store(g, gen_const_unsigned(g, i64, 0), index.addr, (uint64_t)sizeof(uint64_t));
    const uint64_t head = gen_label(g);
    const uint64_t body = gen_label(g);
    const uint64_t step = gen_label(g);
    const uint64_t done = gen_label(g);
    gen_br(g, head);
    gen_block_begin(g, head);
    const gen_val_t at = gen_load(g, i64, index.addr, (uint64_t)sizeof(uint64_t));
    const gen_val_t len = gen_length_of(g, n->b->type, coll.addr);
    // The counter never passes the length, so the compare is unsigned and no
    // bounds check is needed on the element address (item 16).
    gen_br_cond(g, gen_icmp(g, "ult", at, len), body, done);
    gen_block_begin(g, body);
    gen_place_t element;
    element.addr = gen_element_addr(g, n->b->type, n->sym->type, coll.addr, at);
    element.type = n->sym->type;
    copy_into(g, gen_slot_place(g, n->sym), element);
    gen_loop_body(g, n->c, done, step);
    if (!g->terminated) {
        gen_br(g, step);
    }
    gen_block_begin(g, step);
    const gen_val_t before = gen_load(g, i64, index.addr, (uint64_t)sizeof(uint64_t));
    gen_store(g,
              gen_binary(g, "add", before, gen_const_unsigned(g, i64, 1)),
              index.addr,
              (uint64_t)sizeof(uint64_t));
    gen_br(g, head);
    gen_block_begin(g, done);
}

// `break` and `continue` target the innermost enclosing loop (D7.5). A
// `break` with no enclosing loop is not a checker error, since D7.6 lets one
// stand in a `switch` alone, so this is unreachable only while `switch` is a
// gen_todo: T-020 sets a target of its own, because a `break` inside a
// `switch` inside a loop exits the switch (D7.6) and not the loop.
static void gen_break(gen_t* g, bool cont) {
    if (g->loop_depth == 0) {
        fatal_internal("gen: break or continue outside a loop");
    }
    gen_br(g, cont ? g->loop_continue : g->loop_break);
}

void gen_stmt(gen_t* g, ast_node_t* n) {
    if (n == NULL || g->failed) {
        return;
    }
    switch (n->kind) {
    case AST_BLOCK:
        gen_block(g, n);
        return;
    case AST_VAR_DECL:
        gen_var_decl(g, n);
        return;
    case AST_ASSIGN:
        gen_assign(g, n);
        return;
    case AST_INCDEC:
        gen_incdec(g, n);
        return;
    case AST_CALL_STMT:
        // A call statement evaluates the call for its effects (D7.3).
        gen_expr_discard(g, n->a);
        return;
    case AST_RETURN:
        gen_return(g, n);
        return;
    case AST_ERROR:
        // The tokens a syntax error made the parser skip: the module is not
        // emitted at all when one was reported (D14.2).
        return;
    case AST_IF:
        gen_if(g, n);
        return;
    case AST_WHILE:
        gen_while(g, n);
        return;
    case AST_DO_WHILE:
        // The parser rejects `do`-`while` in the bootstrap subset, so this
        // arm is defensive and no later ticket owns it (D15).
        gen_todo(g, n->loc, "a do-while loop");
        return;
    case AST_FOR:
        gen_for(g, n);
        return;
    case AST_RANGE_FOR:
        gen_range_for(g, n);
        return;
    case AST_SWITCH:
        gen_todo(g, n->loc, "a switch statement");
        return;
    case AST_BREAK:
        gen_break(g, false);
        return;
    case AST_CONTINUE:
        gen_break(g, true);
        return;
    case AST_DEFER:
        gen_todo(g, n->loc, "a deferred statement");
        return;
    default:
        break;
    }
    gen_todo(g, n->loc, "this statement");
}

void gen_block(gen_t* g, ast_node_t* n) {
    if (n == NULL) {
        return;
    }
    for (uint64_t i = 0; i < ast_len(n); i++) {
        if (g->failed) {
            return;
        }
        if (g->terminated) {
            // After a terminating statement the emitter opens a fresh block
            // for the unreachable statements D14.2 allows (item 10).
            gen_block_begin(g, gen_label(g));
        }
        gen_stmt(g, ast_child(n, i));
    }
}

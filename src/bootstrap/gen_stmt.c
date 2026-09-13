// The statements of the LLVM IR emitter (toolchain.md 6 item 10); see gen.h.
// D19.4
//
// One function per statement form. Control flow is explicit blocks, so after a
// terminating statement the emitter opens a fresh `%L<N>` block for the
// unreachable statements that may follow.
// D14.2
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

// The width a failure entry point takes a value at: every value the runtime
// reports arrives sign-extended to 64 bits (toolchain.md 5.1).
enum { FAIL_VALUE_BITS = 64 };

// The operator a compound assignment applies, or TOK_ASSIGN for a plain one:
// `lv op= e` has the operand rules and the overflow behavior of `lv op e`.
// D7.2
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

// Whether an assignment to a place of type `t` carries the overwrite check: only
// an `own` reference, only in the checked mode. An owning aggregate is not
// checked field by field, a declaration's initializer is never checked, and
// `--no-bounds-check` removes the index and span branches alone, so it leaves
// this one standing (item 18).
// D10.6, D11.1, D17.11
static bool overwrite_checked(const gen_t* g, const type_t* t) {
    return !g->opts.release && type_is_reference(t) && t->own;
}

// The check itself (item 18): the current pointer word is loaded -- field 0 of a
// span or `string` header, the value itself for a pointer or `void*` -- and a
// non-null one branches to `std.rt.fail_overwrite`, because the previous
// allocation would leak. `loc` is the `=` token.
// D11.4, D17.11
static void gen_overwrite_check(gen_t* g, gen_place_t target, loc_t loc) {
    const gen_val_t held =
        gen_is_aggregate(target.type) ? gen_span_ptr(g, target.addr) : gen_load_place(g, target);
    const gen_val_t live = gen_icmp(g, "ne", held, gen_literal(g, str_from_cstr("ptr"), "null"));
    gen_args_t args;
    gen_args_init(&args);
    gen_check(g, live, true, RT_FAIL_OVERWRITE, &args, loc);
    gen_args_free(&args);
}

// A plain assignment into an `own` reference: the right-hand side is fully
// evaluated, the check runs, and only then does the store happen (item 18).
static void gen_checked_store(gen_t* g, gen_place_t target, ast_node_t* value, loc_t loc) {
    if (!gen_is_aggregate(target.type)) {
        // A pointer's value is in a register, so the check stands between its
        // evaluation and the store.
        const gen_val_t v = gen_expr_value(g, value);
        if (g->failed) {
            return;
        }
        gen_overwrite_check(g, target, loc);
        gen_store_place(g, target, v);
        return;
    }
    // D19.3: a span lands in a temporary, copied over after the check
    const gen_place_t tmp = gen_temp_place(g, target.type);
    gen_expr_into(g, value, tmp);
    if (g->failed) {
        return;
    }
    gen_overwrite_check(g, target, loc);
    const uint64_t align = type_alignof(target.type);
    gen_memcpy(g, target.addr, align, tmp.addr, align, type_sizeof(target.type));
}

// `Type name = init;`: the local's storage is the entry-block alloca
// gen_function made, so the statement is the initializer alone -- and, for an
// owning local, the overwrite check, since a declaration is a store like any
// other. gen_function zeroed the slot once in the entry block, so the first
// execution passes and a second one, in a loop whose body did not `del`, traps
// instead of leaking. The check is reported at the declared name, the declaration
// having no operator token of its own.
// D7.1, D11.4, D17.11
static void gen_var_decl(gen_t* g, ast_node_t* n) {
    if (n->b == NULL || n->sym == NULL) {
        return;
    }
    const gen_place_t slot = gen_slot_place(g, n->sym);
    if (overwrite_checked(g, slot.type)) {
        gen_checked_store(g, slot, n->b, n->name_loc);
        return;
    }
    gen_expr_into(g, n->b, slot);
}

static void gen_assign(gen_t* g, ast_node_t* n) {
    // D6.3: the target is evaluated before the value
    const gen_place_t target = gen_expr_place(g, n->a);
    if (g->failed) {
        return;
    }
    const int32_t op = compound_operator(n->op);
    if (op == TOK_ASSIGN) {
        if (overwrite_checked(g, target.type)) {
            // D11.1, D17.11: storing over a live `own` value is a runtime error
            gen_checked_store(g, target, n->b, n->loc);
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

// ---- deferred statements -----------------------------------------------------------
// D7.8

// What leaves a block, for deciding which scopes an exit unwinds through:
// falling off the end of a block is the block's own business and needs no
// kind of its own.
enum {
    EXIT_RETURN = 0,
    EXIT_BREAK = 1,
    EXIT_CONTINUE = 2,
};

static void scope_push(gen_t* g, gen_scope_kind_t kind) {
    intvec_push(&g->scope_kinds, kind);
    intvec_push(&g->scope_first, (int64_t)g->defers.len);
}

// Closes the innermost scope: its deferred statements go out of scope with it,
// so nothing an exit below it crosses can run them.
// D7.8
static void scope_pop(gen_t* g) {
    g->defers.len = (uint64_t)intvec_pop(&g->scope_first);
    (void)intvec_pop(&g->scope_kinds);
}

// The deferred statements of the scope at `depth`, in reverse textual order:
// those registered after that scope opened and before the next one did, so an
// unwind through an outer scope runs that scope's own statements alone. A
// deferred statement that ends the block -- a call to a `noreturn` function --
// stops the expansion, the rest of the exit not being reached.
// D7.8
static void run_scope_defers(gen_t* g, uint64_t depth) {
    const uint64_t first = (uint64_t)g->scope_first.items[depth];
    uint64_t end = g->defers.len;
    if (depth + 1 < g->scope_first.len) {
        end = (uint64_t)g->scope_first.items[depth + 1];
    }
    // D7.8: an exit met below may not unwind past the scopes open now
    const uint64_t saved_floor = g->defer_floor;
    g->defer_floor = g->scope_kinds.len;
    for (uint64_t i = end; i > first; i--) {
        if (g->failed || g->terminated) {
            break;
        }
        ast_node_t* deferred = (ast_node_t*)g->defers.items[i - 1];
        // D7.8: nothing is captured at `defer` time; this is ordinary code
        gen_stmt(g, deferred->a);
    }
    g->defer_floor = saved_floor;
}

// Whether an exit of kind `exit_kind` stops at a scope of kind `kind`: `break`
// leaves the innermost loop body or case body, `continue` the innermost loop
// body, and `return` unwinds out to the function body.
// D7.6, D7.8
static bool exit_stops_at(int32_t exit_kind, gen_scope_kind_t kind) {
    if (exit_kind == EXIT_RETURN) {
        return kind == GEN_SCOPE_FN;
    }
    if (exit_kind == EXIT_CONTINUE) {
        return kind == GEN_SCOPE_LOOP;
    }
    return kind == GEN_SCOPE_LOOP || kind == GEN_SCOPE_CASE;
}

// The deferred code an exit runs, copied into the exit itself: the set is
// static, so every exited scope is expanded here, innermost first and in reverse
// order within each. How many scopes the exit leaves is read off the scope stack,
// which is the shape of the tree being walked, so no counter is kept beside it.
// D7.8
static void gen_unwind(gen_t* g, int32_t exit_kind) {
    uint64_t depth = g->scope_kinds.len;
    while (depth > g->defer_floor) {
        depth--;
        run_scope_defers(g, depth);
        if (g->failed || g->terminated) {
            return;
        }
        if (exit_stops_at(exit_kind, (gen_scope_kind_t)g->scope_kinds.items[depth])) {
            return;
        }
    }
    // The scope an exit stops at always exists: the checker refused `return`,
    // `break` and `continue` inside deferred code and `break` and `continue` with no
    // construct around them (check_stmt.c), so an exit that walks past the floor is a
    // checker that let one through, not a program. Without the floor the walk would
    // re-enter the deferred statement that holds the exit, without end.
    // D7.8
    fatal_internal("gen: return, break or continue inside deferred code");
}

// `return x` of a bare `own` local or parameter is an implicit `move`, which the
// checker marked: the operand is emptied once its value has been read, so that a
// `defer del(x)` written above sees the zero value and frees nothing (item 18).
// D7.8, D17.5, D17.7
static void gen_return_move(gen_t* g, ast_node_t* n) {
    if ((n->ann & CHECK_ANN_MOVE) == 0 || g->failed) {
        return;
    }
    const gen_place_t src = gen_expr_place(g, n->a);
    if (!g->failed) {
        gen_zero_owner(g, src);
    }
}

static void gen_return(gen_t* g, ast_node_t* n) {
    if (n->a == NULL) {
        gen_unwind(g, EXIT_RETURN);
        if (g->failed || g->terminated) {
            return;
        }
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
        // The `sret` block belongs to the caller, which may have passed a pointer to it
        // as an argument as well (`s = f(&s)` hands one block to both), so deferred code
        // can reach it. The result therefore lands in a temporary of the callee's own,
        // which nothing outside names, and is copied out after the deferred code has run:
        // `return e` evaluates `e` before deferred code runs, which cannot then change
        // the value returned. A function with nothing deferred writes the block directly
        // and pays for no temporary.
        // D7.8, D8.2
        const bool through_temp = g->defers.len > 0;
        const gen_place_t target = through_temp ? gen_temp_place(g, n->a->type) : sret;
        gen_expr_into(g, n->a, target);
        // D17.5: the operand is emptied before the deferred code too
        gen_return_move(g, n);
        gen_unwind(g, EXIT_RETURN);
        if (g->failed || g->terminated) {
            return;
        }
        if (through_temp) {
            const uint64_t align = type_alignof(n->a->type);
            gen_memcpy(g, sret.addr, align, target.addr, align, type_sizeof(n->a->type));
        }
        gen_ins(g);
        gen_text_append(g, "ret void");
        gen_ins_end(g);
        g->terminated = true;
        return;
    }
    const gen_val_t v = gen_expr_value(g, n->a);
    gen_return_move(g, n);
    // D7.8: the value is in a register before the deferred code runs
    gen_unwind(g, EXIT_RETURN);
    if (g->failed || g->terminated) {
        return;
    }
    gen_ins(g);
    gen_text_append(g, "ret ");
    gen_text_append_str(g, v.ty);
    gen_text_append(g, " ");
    gen_text_append_str(g, v.val);
    gen_ins_end(g);
    g->terminated = true;
}

// ---- control flow (item 10) -------------------------------------------------------
// D19.4

// `if (cond) { } else if (cond) { } else { }`: the condition branches to the then
// block and to the else block, or to the continuation when there is no `else`,
// and each branch that has not terminated branches to the continuation. Every
// block the emitter opens therefore ends in exactly one terminator (item 10).
// D7.4
static void gen_if(gen_t* g, ast_node_t* n) {
    const gen_val_t cond = gen_expr_value(g, n->a);
    if (g->failed) {
        return;
    }
    const bool has_else = n->c != NULL;
    // D19.5: the order labels are allocated in is part of the text
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
        // D7.4
        gen_stmt(g, n->c);
        if (!g->terminated) {
            gen_br(g, done);
        }
    }
    gen_block_begin(g, done);
}

// The body of a loop, with `break` and `continue` targeting it: they target
// the innermost enclosing loop, so the enclosing targets are saved and
// restored around it.
// D7.5
static void gen_loop_body(gen_t* g, ast_node_t* body, uint64_t brk, uint64_t cont) {
    const uint64_t saved_break = g->break_label;
    const uint64_t saved_continue = g->continue_label;
    const bool had_break = g->has_break;
    const bool had_continue = g->has_continue;
    g->break_label = brk;
    g->continue_label = cont;
    g->has_break = true;
    g->has_continue = true;
    // D7.8: `break` and `continue` both leave the loop body
    gen_block_scoped(g, body, GEN_SCOPE_LOOP);
    g->break_label = saved_break;
    g->continue_label = saved_continue;
    g->has_break = had_break;
    g->has_continue = had_continue;
}

// `while (cond) { }`: a head block that re-evaluates the condition, a body that
// branches back to it, and a continuation. `continue` targets the head, `break`
// the continuation.
// D7.5
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

// `for (init; cond; step) { }`: `init` runs in the block the loop stands in, the
// head tests the condition, the body branches to the step block and the step
// branches back to the head. `continue` targets the step block, because
// `continue` in a `for` runs `step`; an empty `cond` means `true`, which is what
// `for (;;)` is.
// D7.5
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
        // D7.5, D8.4: `init` may end the block, so the entry edge is guarded
        gen_br(g, head);
    }
    gen_block_begin(g, head);
    if (n->b == NULL) {
        // D7.5: an empty condition is `true`
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
// and stored, an aggregate is copied with `llvm.memcpy`.
// D19.3
static void copy_into(gen_t* g, gen_place_t dst, gen_place_t src) {
    if (gen_is_aggregate(dst.type)) {
        const uint64_t align = type_alignof(dst.type);
        gen_memcpy(g, dst.addr, align, src.addr, align, type_sizeof(dst.type));
        return;
    }
    gen_store_place(g, dst, gen_load_place(g, src));
}

// The place the collection of a range `for` is iterated over. The loop lends an
// owning collection, which is iterated in place and never moved or copied; a
// collection that owns nothing is evaluated as a value into a temporary, so that
// it is evaluated once before the first iteration and the loop iterates over the
// copy.
// D7.5, D17.10
static gen_place_t range_collection(gen_t* g, ast_node_t* coll) {
    if (coll->type != NULL && (type_is_owning_aggregate(coll->type) ||
                               (type_is_reference(coll->type) && coll->type->own))) {
        return gen_expr_place(g, coll);
    }
    const gen_place_t tmp = gen_temp_place(g, coll->type);
    gen_expr_into(g, coll, tmp);
    return tmp;
}

// `for (T x : coll) { }`: an `i64` counter the compiler invents walks the
// collection, and the loop variable is a fresh copy of each element taken at the
// start of its iteration. The counter is bounded by the collection's length, so
// its increment is a plain `add` and not a checked one (item 15).
// D7.5
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
    element.addr = gen_element_addr(g, n->b->type, coll.addr, at);
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

// `break` targets the innermost enclosing loop or `switch` and `continue` the
// innermost enclosing loop alone. The checker refused a `break` with neither
// construct around it and a `continue` outside every loop, so a target is always
// set here (check_stmt.c).
// D7.5, D7.6
static void gen_break(gen_t* g, bool cont) {
    if (cont ? !g->has_continue : !g->has_break) {
        fatal_internal("gen: break or continue outside a loop or switch");
    }
    // D7.8: in a `for`, `continue` runs them before `step`
    gen_unwind(g, cont ? EXIT_CONTINUE : EXIT_BREAK);
    if (g->failed || g->terminated) {
        return;
    }
    gen_br(g, cont ? g->continue_label : g->break_label);
}

// ---- switch ------------------------------------------------------------------------
// D7.6, D7.7

// The body of one case clause, with `break` targeting the switch: a `break`
// inside a `switch` inside a loop exits the switch and not the loop, and
// `continue` still targets the loop the switch stands in.
// D7.6
static void gen_case_body(gen_t* g, ast_node_t* body, uint64_t brk) {
    const uint64_t saved_break = g->break_label;
    const bool had_break = g->has_break;
    g->break_label = brk;
    g->has_break = true;
    // D7.6, D7.8
    gen_block_scoped(g, body, GEN_SCOPE_CASE);
    g->break_label = saved_break;
    g->has_break = had_break;
}

// The clause that is the `default`, or `clauses` when the switch has none: at
// most one, in any position.
// D7.6
static uint64_t default_clause(const ast_node_t* n, uint64_t clauses) {
    for (uint64_t i = 0; i < clauses; i++) {
        const ast_node_t* clause = ast_child(n, i);
        if (clause->kind == AST_CASE && (clause->flags & AST_FLAG_DEFAULT) != 0) {
            return i;
        }
    }
    return clauses;
}

// `switch (e) { case a, b: ... default: ... }`: one LLVM `switch` on the operand
// with one case per label, a block per clause and a default block, which is the
// `default` clause wherever it stands, the failure block for an enum switch that
// has no `default`, and the continuation itself otherwise (item 10). Each case
// body is an implicit block scope with an implicit `break` at its end, so a body
// that has not terminated branches to the continuation and no clause ever falls
// into the next.
// D7.6, D7.7
static void gen_switch(gen_t* g, ast_node_t* n) {
    const gen_val_t operand = gen_expr_value(g, n->a);
    if (g->failed) {
        return;
    }
    const uint64_t clauses = ast_len(n);
    const type_t* ot = n->a->type;
    const uint64_t fallback_clause = default_clause(n, clauses);
    // A `switch` over an enum with no `default` lists every member and still meets
    // values outside the member set, since a zeroed enum holds 0 whether or not 0 is
    // a member and int-to-enum is unchecked. That is not in the list of undefined
    // behavior and cannot be diagnosed, so the compiler gives the switch a `default`
    // of its own that reports a runtime error naming the enum and the value.
    // D3.9, D7.7, D10.7
    const bool generated_default =
        fallback_clause == clauses && ot != NULL && ot->kind == TYPE_ENUM;
    gen_val_t reported = operand;
    if (generated_default) {
        // D19.4, D19.6: widened in the block that dominates the failure
        reported = gen_resize(
            g, operand, gen_int_bits(ot), str_from_cstr("i64"), (uint32_t)FAIL_VALUE_BITS, true);
    }
    // D19.5: one block per clause in clause order, the continuation last
    const uint64_t first = g->labels;
    for (uint64_t i = 0; i < clauses; i++) {
        (void)gen_label(g);
    }
    const uint64_t done = gen_label(g);
    // The default target: the `default` clause wherever it stands, the failure block
    // whose label follows the continuation's, or the continuation itself.
    // D7.7, D19.6
    uint64_t fallback = done;
    if (generated_default) {
        fallback = gen_label(g);
    } else if (fallback_clause != clauses) {
        fallback = first + fallback_clause;
    }
    gen_switch_begin(g, operand, fallback);
    for (uint64_t i = 0; i < clauses; i++) {
        const ast_node_t* clause = ast_child(n, i);
        if (clause->kind != AST_CASE) {
            continue;
        }
        for (uint64_t k = 0; k < ast_len(clause); k++) {
            // D7.6, D19.5: a folded label prints with the operand type's sign
            const ast_node_t* label = ast_child(clause, k);
            gen_switch_case(
                g, gen_const_value(g, n->a->type, check_node_value(g->ck, label)), i + first);
        }
    }
    gen_switch_end(g);
    if (generated_default) {
        // Emitted here, while its label is the smallest one outstanding, so that the
        // failure blocks of the case bodies follow it in ascending order. It is not a
        // bounds check, so neither `--release` nor `--no-bounds-check` removes it.
        // D7.7, D10.6, D19.6
        gen_args_t args;
        gen_args_init(&args);
        gen_args_add(&args, reported);
        gen_args_add(&args, gen_literal(g, str_from_cstr("ptr"), gen_str_ref(g, ot->name).ptr));
        gen_fail_block(g, fallback, RT_FAIL_ENUM, &args, n->loc);
        gen_args_free(&args);
    }
    for (uint64_t i = 0; i < clauses; i++) {
        ast_node_t* clause = ast_child(n, i);
        gen_block_begin(g, first + i);
        if (clause->kind == AST_CASE) {
            gen_case_body(g, clause->a, done);
        }
        if (g->failed) {
            return;
        }
        if (!g->terminated) {
            // D7.6
            gen_br(g, done);
        }
    }
    gen_block_begin(g, done);
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
        // D7.3
        gen_expr_discard(g, n->a);
        return;
    case AST_RETURN:
        gen_return(g, n);
        return;
    case AST_ERROR:
        // D14.2: the module is not emitted when a syntax error was reported
        return;
    case AST_IF:
        gen_if(g, n);
        return;
    case AST_WHILE:
        gen_while(g, n);
        return;
    case AST_DO_WHILE:
        // D15: the parser rejects `do`-`while`, so this arm is defensive
        gen_todo(g, n->loc, "a do-while loop");
        return;
    case AST_FOR:
        gen_for(g, n);
        return;
    case AST_RANGE_FOR:
        gen_range_for(g, n);
        return;
    case AST_SWITCH:
        gen_switch(g, n);
        return;
    case AST_BREAK:
        gen_break(g, false);
        return;
    case AST_CONTINUE:
        gen_break(g, true);
        return;
    case AST_DEFER:
        // D7.8: a `defer` emits nothing where it stands; exits copy it out
        ptrvec_push(&g->defers, n);
        return;
    default:
        break;
    }
    gen_todo(g, n->loc, "this statement");
}

void gen_block(gen_t* g, ast_node_t* n) {
    gen_block_scoped(g, n, GEN_SCOPE_BLOCK);
}

void gen_block_scoped(gen_t* g, ast_node_t* n, gen_scope_kind_t kind) {
    if (n == NULL) {
        return;
    }
    scope_push(g, kind);
    for (uint64_t i = 0; i < ast_len(n); i++) {
        if (g->failed) {
            break;
        }
        if (g->terminated) {
            // D14.2: a fresh block for the unreachable statements after an exit
            gen_block_begin(g, gen_label(g));
        }
        gen_stmt(g, ast_child(n, i));
    }
    if (!g->failed && !g->terminated) {
        // D7.8: falling off the end runs them in reverse order
        run_scope_defers(g, g->scope_kinds.len - 1);
    }
    scope_pop(g);
}

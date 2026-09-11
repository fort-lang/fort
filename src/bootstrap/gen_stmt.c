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
    case AST_WHILE:
    case AST_DO_WHILE:
    case AST_FOR:
    case AST_RANGE_FOR:
        gen_todo(g, n->loc, "control flow");
        return;
    case AST_SWITCH:
        gen_todo(g, n->loc, "a switch statement");
        return;
    case AST_BREAK:
    case AST_CONTINUE:
        gen_todo(g, n->loc, "break and continue");
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

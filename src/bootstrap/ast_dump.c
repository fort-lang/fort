// The S-expression printer for the syntax tree; see ast_dump.h.
#include "ast_dump.h"

#include <stddef.h>
#include <stdint.h>

#include "ast.h"
#include "diag.h"
#include "lexer.h"
#include "prim.h"
#include "str.h"
#include "types.h"

// Bytes outside this range print as \xHH inside a string literal.
enum { PRINTABLE_LOW = 0x20, PRINTABLE_HIGH = 0x7E, HEX_RADIX = 16 };

static void sexp_open(sb_t* out, const char* head) {
    sb_push(out, '(');
    sb_append(out, head);
}

static void sexp_close(sb_t* out) {
    sb_push(out, ')');
}

static void sexp_word(sb_t* out, const char* w) {
    sb_push(out, ' ');
    sb_append(out, w);
}

static void sexp_name(sb_t* out, str_t s) {
    sb_push(out, ' ');
    sb_append_str(out, s);
}

static void sexp_uint(sb_t* out, uint64_t v) {
    sb_push(out, ' ');
    msg_uint(out, v);
}

static void sexp_quoted(sb_t* out, str_t s) {
    static const char* const HEX_DIGITS = "0123456789ABCDEF";
    sb_append(out, " \"");
    for (uint64_t i = 0; i < s.len; i++) {
        const unsigned char byte = (unsigned char)s.ptr[i];
        if (byte == '"' || byte == '\\') {
            sb_push(out, '\\');
            sb_push(out, (char)byte);
        } else if (byte < PRINTABLE_LOW || byte > PRINTABLE_HIGH) {
            sb_append(out, "\\x");
            sb_push(out, HEX_DIGITS[byte / HEX_RADIX]);
            sb_push(out, HEX_DIGITS[byte % HEX_RADIX]);
        } else {
            sb_push(out, (char)byte);
        }
    }
    sb_push(out, '"');
}

static void sexp_child(const ast_node_t* n, sb_t* out) {
    sb_push(out, ' ');
    ast_dump(n, out);
}

static void sexp_list(const ast_node_t* n, sb_t* out) {
    for (uint64_t i = 0; i < ast_len(n); i++) {
        sexp_child(ast_child(n, i), out);
    }
}

// The `own` and `mut` of a type position, in the order the source writes them:
// an `own` precedes the `mut` in a position.
// D17.2
static void sexp_marks(const ast_node_t* n, sb_t* out) {
    if (ast_is_own(n)) {
        sexp_word(out, "own");
    }
    if (ast_is_mut(n)) {
        sexp_word(out, "mut");
    }
}

static void dump_suffix(const ast_node_t* n, sb_t* out) {
    switch ((suffix_kind_t)n->op) {
    case SUFFIX_PTR:
        sexp_open(out, "ptr");
        break;
    case SUFFIX_SPAN:
        sexp_open(out, "span");
        break;
    case SUFFIX_ARRAY:
        sexp_open(out, "array");
        sexp_child(n->a, out);
        break;
    }
    sexp_marks(n, out);
    sexp_close(out);
}

static void dump_type(const ast_node_t* n, sb_t* out) {
    switch (n->kind) {
    case AST_TYPE:
        sexp_open(out, "type");
        sexp_child(n->a, out);
        sexp_marks(n, out);
        sexp_list(n, out);
        break;
    case AST_TYPE_PRIM:
        sexp_open(out, "prim");
        sexp_word(out, prim_name((prim_kind_t)n->op));
        break;
    case AST_TYPE_NAME:
        sexp_open(out, "name");
        sexp_name(out, n->name);
        if (n->a != NULL) {
            sexp_name(out, n->a->name);
        }
        break;
    case AST_TYPE_FN:
        sexp_open(out, "fn-type");
        sexp_child(n->a, out);
        sexp_list(n, out);
        break;
    default:
        sexp_open(out, ast_kind_name(n->kind));
        break;
    }
    sexp_close(out);
}

static void dump_decl(const ast_node_t* n, sb_t* out) {
    switch (n->kind) {
    case AST_IMPORT:
        sexp_open(out, "import");
        sexp_child(n->a, out);
        sexp_child(n->b, out);
        sexp_list(n, out);
        break;
    case AST_IMPORT_ITEM:
        sexp_open(out, "item");
        sexp_name(out, n->name);
        sexp_child(n->a, out);
        break;
    case AST_PATH:
        // The segments are identifiers, printed as words: (path std io).
        sexp_open(out, "path");
        for (uint64_t i = 0; i < ast_len(n); i++) {
            sexp_name(out, ast_child(n, i)->name);
        }
        break;
    case AST_FN_DECL:
        sexp_open(out, (n->flags & AST_FLAG_EXTERN) != 0 ? "extern-fn" : "fn");
        sexp_child(n->a, out);
        sexp_name(out, n->name);
        sb_append(out, " (params");
        sexp_list(n, out);
        sexp_close(out);
        sexp_child(n->b, out);
        break;
    case AST_PARAM:
    case AST_FIELD_DECL:
        sexp_open(out, ast_kind_name(n->kind));
        sexp_child(n->a, out);
        sexp_name(out, n->name);
        break;
    case AST_ENUM_MEMBER:
        sexp_open(out, "member");
        sexp_name(out, n->name);
        sexp_child(n->a, out);
        break;
    case AST_VAR_DECL:
        sexp_open(out, "var");
        sexp_name(out, n->name);
        sexp_child(n->a, out);
        sexp_child(n->b, out);
        break;
    case AST_STRUCT_DECL:
    case AST_ENUM_DECL:
        sexp_open(out, ast_kind_name(n->kind));
        sexp_name(out, n->name);
        sexp_list(n, out);
        break;
    default:
        sexp_open(out, ast_kind_name(n->kind));
        sexp_list(n, out);
        break;
    }
    sexp_close(out);
}

static void dump_stmt(const ast_node_t* n, sb_t* out) {
    switch (n->kind) {
    case AST_ASSIGN:
        sexp_open(out, "assign");
        sexp_word(out, tok_kind_name((tok_kind_t)n->op));
        sexp_child(n->a, out);
        sexp_child(n->b, out);
        break;
    case AST_INCDEC:
        sexp_open(out, "incdec");
        sexp_word(out, tok_kind_name((tok_kind_t)n->op));
        sexp_child(n->a, out);
        break;
    case AST_IF:
        sexp_open(out, "if");
        sexp_child(n->a, out);
        sexp_child(n->b, out);
        sexp_child(n->c, out);
        break;
    case AST_FOR:
        sexp_open(out, "for");
        sexp_child(n->a, out);
        sexp_child(n->b, out);
        sexp_child(n->c, out);
        sexp_child(n->d, out);
        break;
    case AST_RANGE_FOR:
        sexp_open(out, "range-for");
        sexp_child(n->a, out);
        sexp_name(out, n->name);
        sexp_child(n->b, out);
        sexp_child(n->c, out);
        break;
    case AST_SWITCH:
        sexp_open(out, "switch");
        sexp_child(n->a, out);
        sexp_list(n, out);
        break;
    case AST_CASE:
        sexp_open(out, "case");
        if ((n->flags & AST_FLAG_DEFAULT) != 0) {
            sexp_word(out, "default");
        } else {
            sb_append(out, " (labels");
            sexp_list(n, out);
            sexp_close(out);
        }
        sexp_child(n->a, out);
        break;
    case AST_BRACE_INIT:
        sexp_open(out, "init");
        sexp_list(n, out);
        break;
    case AST_DESIGNATOR:
        sexp_open(out, "designator");
        sexp_name(out, n->name);
        sexp_child(n->a, out);
        break;
    case AST_BLOCK:
        sexp_open(out, "block");
        sexp_list(n, out);
        break;
    case AST_WHILE:
    case AST_DO_WHILE:
        sexp_open(out, ast_kind_name(n->kind));
        sexp_child(n->a, out);
        sexp_child(n->b, out);
        break;
    default:
        sexp_open(out, ast_kind_name(n->kind));
        if (n->kind != AST_BREAK && n->kind != AST_CONTINUE) {
            sexp_child(n->a, out);
        }
        break;
    }
    sexp_close(out);
}

static void dump_expr(const ast_node_t* n, sb_t* out) {
    switch (n->kind) {
    case AST_INT:
    case AST_CHAR:
        sexp_open(out, ast_kind_name(n->kind));
        sexp_uint(out, n->ival);
        break;
    case AST_FLOAT:
    case AST_STRING:
        sexp_open(out, ast_kind_name(n->kind));
        sexp_quoted(out, n->name);
        break;
    case AST_BOOL:
        sexp_open(out, "bool");
        sexp_word(out, n->ival != 0 ? "true" : "false");
        break;
    case AST_IDENT:
        sexp_open(out, "ident");
        sexp_name(out, n->name);
        break;
    case AST_UNARY:
    case AST_BINARY:
        sexp_open(out, ast_kind_name(n->kind));
        sexp_word(out, tok_kind_name((tok_kind_t)n->op));
        sexp_child(n->a, out);
        if (n->kind == AST_BINARY) {
            sexp_child(n->b, out);
        }
        break;
    case AST_TERNARY:
    case AST_SPAN:
        sexp_open(out, ast_kind_name(n->kind));
        sexp_child(n->a, out);
        sexp_child(n->b, out);
        sexp_child(n->c, out);
        break;
    case AST_CALL:
        sexp_open(out, "call");
        sexp_child(n->a, out);
        sexp_list(n, out);
        break;
    case AST_FIELD:
    case AST_ARROW:
        sexp_open(out, ast_kind_name(n->kind));
        sexp_child(n->a, out);
        sexp_name(out, n->name);
        break;
    case AST_SIZEOF:
        sexp_open(out, "sizeof");
        sexp_child(n->a, out);
        break;
    default:
        sexp_open(out, ast_kind_name(n->kind));
        if (n->kind != AST_NULL) {
            sexp_child(n->a, out);
            sexp_child(n->b, out);
        }
        break;
    }
    sexp_close(out);
}

void ast_dump(const ast_node_t* n, sb_t* out) {
    if (n == NULL) {
        sb_append(out, "nil");
        return;
    }
    switch (n->kind) {
    case AST_ERROR:
        // D14.2
        sb_append(out, "(error)");
        return;
    case AST_TYPE:
    case AST_TYPE_PRIM:
    case AST_TYPE_STRING:
    case AST_TYPE_VOID:
    case AST_TYPE_NORETURN:
    case AST_TYPE_NAME:
    case AST_TYPE_FN:
        dump_type(n, out);
        return;
    case AST_TYPE_SUFFIX:
        dump_suffix(n, out);
        return;
    case AST_MODULE:
    case AST_IMPORT:
    case AST_PATH:
    case AST_IMPORT_ITEM:
    case AST_FN_DECL:
    case AST_PARAM:
    case AST_STRUCT_DECL:
    case AST_FIELD_DECL:
    case AST_ENUM_DECL:
    case AST_ENUM_MEMBER:
    case AST_VAR_DECL:
        dump_decl(n, out);
        return;
    case AST_BLOCK:
    case AST_ASSIGN:
    case AST_INCDEC:
    case AST_CALL_STMT:
    case AST_IF:
    case AST_WHILE:
    case AST_DO_WHILE:
    case AST_FOR:
    case AST_RANGE_FOR:
    case AST_SWITCH:
    case AST_CASE:
    case AST_DEFER:
    case AST_RETURN:
    case AST_BREAK:
    case AST_CONTINUE:
    case AST_BRACE_INIT:
    case AST_DESIGNATOR:
        dump_stmt(n, out);
        return;
    default:
        dump_expr(n, out);
        return;
    }
}

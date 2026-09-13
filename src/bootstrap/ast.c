// The syntax tree; see ast.h. The arena hands out nodes from blocks of
// AST_ARENA_BLOCK_NODES and frees them, with each node's list, all at once
// (toolchain.md 8, memory).
#include "ast.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "containers.h"
#include "str.h"

const char* ast_kind_name(ast_kind_t kind) {
    switch (kind) {
    case AST_NONE:
        return "none";
    case AST_MODULE:
        return "module";
    case AST_IMPORT:
        return "import";
    case AST_PATH:
        return "path";
    case AST_IMPORT_ITEM:
        return "item";
    case AST_FN_DECL:
        return "fn";
    case AST_PARAM:
        return "param";
    case AST_STRUCT_DECL:
        return "struct";
    case AST_FIELD_DECL:
        return "field-decl";
    case AST_ENUM_DECL:
        return "enum";
    case AST_ENUM_MEMBER:
        return "member";
    case AST_VAR_DECL:
        return "var";
    case AST_TYPE:
        return "type";
    case AST_TYPE_PRIM:
        return "prim";
    case AST_TYPE_STRING:
        return "string";
    case AST_TYPE_VOID:
        return "void";
    case AST_TYPE_NORETURN:
        return "noreturn";
    case AST_TYPE_NAME:
        return "name";
    case AST_TYPE_FN:
        return "fn-type";
    case AST_TYPE_SUFFIX:
        return "suffix";
    case AST_BLOCK:
        return "block";
    case AST_ASSIGN:
        return "assign";
    case AST_INCDEC:
        return "incdec";
    case AST_CALL_STMT:
        return "call-stmt";
    case AST_IF:
        return "if";
    case AST_WHILE:
        return "while";
    case AST_DO_WHILE:
        return "do";
    case AST_FOR:
        return "for";
    case AST_RANGE_FOR:
        return "range-for";
    case AST_SWITCH:
        return "switch";
    case AST_CASE:
        return "case";
    case AST_DEFER:
        return "defer";
    case AST_RETURN:
        return "return";
    case AST_BREAK:
        return "break";
    case AST_CONTINUE:
        return "continue";
    case AST_BRACE_INIT:
        return "init";
    case AST_DESIGNATOR:
        return "designator";
    case AST_INT:
        return "int";
    case AST_FLOAT:
        return "float";
    case AST_CHAR:
        return "char";
    case AST_STRING:
        return "str";
    case AST_BOOL:
        return "bool";
    case AST_NULL:
        return "null";
    case AST_IDENT:
        return "ident";
    case AST_UNARY:
        return "unary";
    case AST_BINARY:
        return "binary";
    case AST_TERNARY:
        return "ternary";
    case AST_CALL:
        return "call";
    case AST_INDEX:
        return "index";
    case AST_SPAN:
        return "span";
    case AST_FIELD:
        return "field";
    case AST_ARROW:
        return "arrow";
    case AST_CAST:
        return "cast";
    case AST_SIZEOF:
        return "sizeof";
    case AST_NEW:
        return "new";
    case AST_STRUCT_LIT:
        return "struct-lit";
    case AST_ARRAY_LIT:
        return "array-lit";
    // D14.2
    case AST_ERROR:
        return "error";
    case AST_KIND_COUNT:
        break;
    }
    return "?";
}

// ---- the arena ------------------------------------------------------------

void ast_arena_init(ast_arena_t* ar) {
    ar->blocks = NULL;
    ar->block_len = 0;
    ar->block_cap = 0;
    ar->used = 0;
}

// The nodes in use in block `i`: every block but the last is full.
static uint64_t block_nodes(const ast_arena_t* ar, uint64_t i) {
    if (i + 1 < ar->block_len) {
        return AST_ARENA_BLOCK_NODES;
    }
    return ar->used;
}

void ast_arena_free(ast_arena_t* ar) {
    for (uint64_t i = 0; i < ar->block_len; i++) {
        ast_node_t* block = ar->blocks[i];
        const uint64_t n = block_nodes(ar, i);
        for (uint64_t j = 0; j < n; j++) {
            ptrvec_free(&block[j].list);
        }
        mem_free(block);
    }
    mem_free((void*)ar->blocks);
    ast_arena_init(ar);
}

uint64_t ast_arena_count(const ast_arena_t* ar) {
    if (ar->block_len == 0) {
        return 0;
    }
    return mem_add(mem_mul(ar->block_len - 1, AST_ARENA_BLOCK_NODES), ar->used);
}

static void arena_grow(ast_arena_t* ar) {
    if (ar->block_len == ar->block_cap) {
        const uint64_t cap = mem_grown_cap(ar->block_cap, mem_add(ar->block_len, 1));
        ast_node_t** blocks = (ast_node_t**)mem_alloc(mem_mul(cap, sizeof(ast_node_t*)));
        for (uint64_t i = 0; i < ar->block_len; i++) {
            blocks[i] = ar->blocks[i];
        }
        mem_free((void*)ar->blocks);
        ar->blocks = blocks;
        ar->block_cap = cap;
    }
    ar->blocks[ar->block_len] =
        (ast_node_t*)mem_alloc(mem_mul(AST_ARENA_BLOCK_NODES, sizeof(ast_node_t)));
    ar->block_len++;
    ar->used = 0;
}

ast_node_t* ast_new(ast_arena_t* ar, ast_kind_t kind, loc_t loc) {
    if (ar->block_len == 0 || ar->used == AST_ARENA_BLOCK_NODES) {
        arena_grow(ar);
    }
    ast_node_t* n = &ar->blocks[ar->block_len - 1][ar->used];
    ar->used++;
    n->kind = kind;
    n->loc = loc;
    return n;
}

// ---- children -------------------------------------------------------------

void ast_push(ast_node_t* parent, ast_node_t* child) {
    if (child == NULL) {
        fatal_internal("ast_push of a null child");
    }
    ptrvec_push(&parent->list, child);
}

uint64_t ast_len(const ast_node_t* n) {
    return n->list.len;
}

ast_node_t* ast_child(const ast_node_t* n, uint64_t i) {
    if (i >= n->list.len) {
        fatal_internal("ast_child out of range");
    }
    return (ast_node_t*)n->list.items[i];
}

// The marks of a type position, each written once and after what it qualifies.
// D5.3, D17.2
bool ast_is_own(const ast_node_t* n) {
    return (n->flags & AST_FLAG_OWN) != 0;
}

bool ast_is_mut(const ast_node_t* n) {
    return (n->flags & AST_FLAG_MUT) != 0;
}

#!/bin/bash
# Stage2 prints the C variable-tail mark after the fixed parameter.
# D9.8, D14.1
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: stage2_ast_variadic_test.sh <stage2>" >&2
    exit 2
fi

fort=$1
fixed=$("$fort" --ast test/fort/ast/fixed_extern.ft)
variadic=$("$fort" --ast test/fort/ast/variadic_extern.ft)
want_fixed='(module (extern-fn (type (void)) c (params (param (type (prim i32)) x)) nil))'
want_variadic='(module (extern-fn (type (void)) c (params (param (type (prim i32)) x) ...) nil))'

if [ "$fixed" != "$want_fixed" ] || [ "$variadic" != "$want_variadic" ]; then
    echo "stage2_ast_variadic_test.sh: the AST mark differs from toolchain.md 1" >&2
    echo "fixed: $fixed" >&2
    echo "variadic: $variadic" >&2
    exit 1
fi

echo "stage2 --ast prints one bare '...' mark after one fixed parameter"

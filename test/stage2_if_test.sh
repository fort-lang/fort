#!/bin/bash
# Test stage2 `$if` selection before semantic analysis.
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: stage2_if_test.sh <stage2> <std-dir>" >&2
    exit 2
fi

fort=$1
std=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

write_source() {
    local file=$1
    shift
    printf '%s\n' "$@" >"$file"
}

expect_check_error() {
    local file=$1
    local want=$2
    shift 2
    local status=0
    "$fort" --check --std-dir "$std" "$@" "$file" >"$work/out" 2>"$work/err" || status=$?
    if [ "$status" -ne 1 ] || ! grep -Fq "$want" "$work/err"; then
        echo "stage2_if_test.sh: expected error: $want" >&2
        cat "$work/err" >&2
        exit 1
    fi
}

write_chain() {
    local file=$1
    local count=$2
    local i=1
    printf '$if (false) { i32 CHOICE = 0; }\n' >"$file"
    while [ "$i" -lt "$count" ]; do
        if [ "$i" -eq $((count - 1)) ]; then
            printf 'else $if (true) { i32 CHOICE = 1; }\n' >>"$file"
        else
            printf 'else $if (false) { i32 CHOICE = 0; }\n' >>"$file"
        fi
        i=$((i + 1))
    done
}

write_nested_chains() {
    local file=$1
    local levels=$2
    local level=0
    local condition
    : >"$file"
    while [ "$level" -lt "$levels" ]; do
        printf '$if (false) { i32 UNUSED_%s_0 = 0; }\n' "$level" >>"$file"
        condition=1
        while [ "$condition" -lt 256 ]; do
            if [ "$condition" -eq 255 ]; then
                printf 'else $if (true) {\n' >>"$file"
            else
                printf 'else $if (false) { i32 UNUSED_%s_%s = 0; }\n' \
                    "$level" "$condition" >>"$file"
            fi
            condition=$((condition + 1))
        done
        level=$((level + 1))
    done
    printf 'fn main() i32 { return 0; }\n' >>"$file"
    while [ "$level" -gt 0 ]; do
        printf '}\n' >>"$file"
        level=$((level - 1))
    done
}

# The AST keeps all declaration and statement alternatives.
write_source "$work/ast.ft" \
    '$if (true) {' \
    '    i32 THEN_DECL = 1;' \
    '} else $if (false) {' \
    '    i32 ELSE_IF_DECL = 2;' \
    '} else {' \
    '    i32 ELSE_DECL = 3;' \
    '}' \
    'fn main() i32 {' \
    '    $if (true) { return 0; } else { return 1; }' \
    '}'
ast=$($fort --ast "$work/ast.ft")
if [ "$(grep -o 'compile-if' <<<"$ast" | wc -l)" -ne 3 ]; then
    echo 'stage2_if_test.sh: --ast omitted a $if or else $if node' >&2
    echo "$ast" >&2
    exit 1
fi
for name in THEN_DECL ELSE_IF_DECL ELSE_DECL; do
    if ! grep -Fq "$name" <<<"$ast"; then
        echo "stage2_if_test.sh: --ast omitted $name" >&2
        exit 1
    fi
done

# A chain accepts 256 conditions. It reports one error at the 257th condition.
write_chain "$work/chain_256.ft" 256
"$fort" --check --std-dir "$std" "$work/chain_256.ft"
write_chain "$work/chain_257.ft" 257
status=0
"$fort" --check --std-dir "$std" "$work/chain_257.ft" \
    >"$work/out" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] || [ "$(grep -c ' error: ' "$work/err")" -ne 1 ] \
    || ! grep -Fq ':257:6: error: a $if chain can have at most 256 conditions' "$work/err"; then
    echo 'stage2_if_test.sh: the 256-condition chain boundary failed' >&2
    cat "$work/err" >&2
    exit 1
fi

# Chain traversal does not add each chain's length to the selected-tree call
# depth. This case stays two levels below the parser's nesting limit.
write_nested_chains "$work/nested_chains.ft" 254
"$fort" --ast "$work/nested_chains.ft" >"$work/nested_chains.ast"
if [ "$(grep -o 'compile-if' "$work/nested_chains.ast" | wc -l)" -ne 65024 ]; then
    echo 'stage2_if_test.sh: --ast omitted a node from the nested chains' >&2
    exit 1
fi
"$fort" --std-dir "$std" -S -o "$work/nested_chains.ll" "$work/nested_chains.ft"

# Constants are order-independent. The logical operators keep ordinary
# precedence. Three alternative pairs declare one name without a duplicate.
write_source "$work/select.ft" \
    'bool FIRST = BASE;' \
    'string OS = $cfg(target_os);' \
    'string EMPTY = {};' \
    '$if ((!false && FIRST || false) && EMPTY == "") {' \
    '    fn selected_value() i32 { return 6; }' \
    '} else {' \
    '    fn selected_value() i32 { return 90; }' \
    '}' \
    'bool BASE = true;' \
    '$if (false) {' \
    '    i32 SELECTED_NUMBER = 90;' \
    '} else {' \
    '    i32 SELECTED_NUMBER = 4;' \
    '}' \
    '$if (OS == "linux") {' \
    '    fn main() i32 {' \
    '        $if (false) {' \
    '            return missing_value;' \
    '        } else $if (!false && true || false) {' \
    '            return selected_value() + SELECTED_NUMBER - 10;' \
    '        } else {' \
    '            return 91;' \
    '        }' \
    '    }' \
    '} else {' \
    '    fn main() i32 { return 92; }' \
    '}'
"$fort" --std-dir "$std" -o "$work/select" "$work/select.ft"
"$work/select"

# A symbol import sees the selected declaration during the module-prefix probe.
mkdir "$work/closure"
write_source "$work/closure/dep.ft" \
    '$if (true) {' \
    '    fn chosen() i32 { return 3; }' \
    '} else {' \
    '    fn chosen() i32 { return 90; }' \
    '}'
write_source "$work/closure/main.ft" \
    'import dep.chosen;' \
    'fn main() i32 { return chosen() - 3; }'
"$fort" --std-dir "$std" -I "$work/closure" -o "$work/closure/program" \
    "$work/closure/main.ft"
"$work/closure/program"

# Six unselected operands must not be evaluated.
write_source "$work/short.ft" \
    '$if (false && MISSING_AND) { i32 A = 1; } else { i32 A = 0; }' \
    '$if (true || MISSING_OR) { i32 B = 0; } else { i32 B = 1; }' \
    '$if (false && (1 / 0 == 0)) { i32 C = 1; } else { i32 C = 0; }' \
    '$if (true || (1 / 0 == 0)) { i32 D = 0; } else { i32 D = 1; }' \
    '$if (true ? true : MISSING_TERNARY) { i32 G = 0; } else { i32 G = 1; }' \
    '$if (false ? (1 / 0 == 0) : true) { i32 H = 0; } else { i32 H = 1; }' \
    '$if (false) { $if (MISSING_NESTED) { i32 E = 1; } } else { i32 E = 0; }' \
    '$if (1 + 2 * 3 == 7 && 1 + 1.5 == 2.5) { i32 F = 0; } else { i32 F = 1; }' \
    'fn main() i32 { return A + B + C + D + E + F + G + H; }'
"$fort" --check --std-dir "$std" "$work/short.ft"

# Short-circuiting skips values, but it does not remove locally knowable
# logical and ternary type rules.
write_source "$work/short_rules.ft" \
    '$if (false && 1) { i32 A = 1; }' \
    '$if (true ? true : 1) { i32 B = 1; }'
status=0
"$fort" --check --std-dir "$std" "$work/short_rules.ft" \
    >"$work/out" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] || [ "$(grep -c ' error: ' "$work/err")" -ne 2 ] \
    || ! grep -Fq 'logical operands in a $if condition must be bool' "$work/err" \
    || ! grep -Fq "the operands of '?:' in a \$if condition must agree" "$work/err"; then
    echo 'stage2_if_test.sh: unselected local type errors did not report 2 errors' >&2
    cat "$work/err" >&2
    exit 1
fi

# Short-circuiting still validates each operator rule that literal shapes
# prove. Each family reports once without evaluating its skipped expression.
write_source "$work/skipped_unary.ft" \
    '$if (false && -true) { i32 VALUE = 1; }'
write_source "$work/skipped_arithmetic.ft" \
    '$if (false && (true + true == true)) { i32 VALUE = 1; }'
write_source "$work/skipped_bitwise.ft" \
    '$if (false && (true & true == true)) { i32 VALUE = 1; }'
write_source "$work/skipped_shift.ft" \
    '$if (false && (true << 1 == true)) { i32 VALUE = 1; }'
write_source "$work/skipped_ordering.ft" \
    '$if (false && (true < false)) { i32 VALUE = 1; }'
for family in unary arithmetic bitwise shift ordering; do
    status=0
    "$fort" --check --std-dir "$std" "$work/skipped_$family.ft" \
        >"$work/out" 2>"$work/err" || status=$?
    if [ "$status" -ne 1 ] || [ "$(grep -c ' error: ' "$work/err")" -ne 1 ]; then
        echo "stage2_if_test.sh: skipped $family operator did not report once" >&2
        cat "$work/err" >&2
        exit 1
    fi
done

# Evaluated and skipped operators use the same exact primitive rules. Mixed
# integer widths and typed unsigned negation each report in both forms.
write_source "$work/exact_integer_pair.ft" \
    '$if ((cast(1, i32) + cast(1, i64)) == 2) { i32 A = 1; }' \
    '$if (false && ((cast(1, i32) + cast(1, i64)) == 2)) { i32 B = 1; }'
status=0
"$fort" --check --std-dir "$std" "$work/exact_integer_pair.ft" \
    >"$work/out" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] \
    || [ "$(grep -Fc 'operator operands in a $if condition must be numeric' "$work/err")" \
        -ne 2 ]; then
    echo 'stage2_if_test.sh: mixed exact integer types did not report twice' >&2
    cat "$work/err" >&2
    exit 1
fi
write_source "$work/unsigned_neg_pair.ft" \
    '$if (-cast(1, u8) == 0) { i32 A = 1; }' \
    '$if (false && (-cast(1, u8) == 0)) { i32 B = 1; }'
status=0
"$fort" --check --std-dir "$std" "$work/unsigned_neg_pair.ft" \
    >"$work/out" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] \
    || [ "$(grep -Fc "unary '-' in a \$if condition requires a signed type" "$work/err")" \
        -ne 2 ]; then
    echo 'stage2_if_test.sh: typed unsigned negation did not report twice' >&2
    cat "$work/err" >&2
    exit 1
fi

# Untyped characters can fold as integers for shifts and bitwise operations.
# The same forms stay valid when short-circuiting skips their values.
write_source "$work/character_operators.ft" \
    '$if (('"'"'a'"'"' << 1) == 194) { i32 A = 0; } else { i32 A = 1; }' \
    '$if (('"'"'a'"'"' & 15) == 1) { i32 B = 0; } else { i32 B = 1; }' \
    '$if (true || (('"'"'a'"'"' << 1) == 194)) { i32 C = 0; } else { i32 C = 1; }' \
    '$if (true || (('"'"'a'"'"' & 15) == 1)) { i32 D = 0; } else { i32 D = 1; }' \
    'fn main() i32 { return A + B + C + D; }'
"$fort" --std-dir "$std" -o "$work/character_operators" "$work/character_operators.ft"
"$work/character_operators"

# The selector does not follow an unconditional dependency cycle in an
# unselected operand. The ordinary checker still reports the declarations.
write_source "$work/skipped_cycle.ft" \
    'bool FIRST_CYCLE = SECOND_CYCLE;' \
    'bool SECOND_CYCLE = FIRST_CYCLE;' \
    '$if (true || FIRST_CYCLE) { fn main() i32 { return 0; } }'
status=0
"$fort" --check --std-dir "$std" "$work/skipped_cycle.ft" \
    >"$work/out" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] || [ "$(grep -c ' error: ' "$work/err")" -ne 1 ] \
    || ! grep -Fq ":1:6: error: 'FIRST_CYCLE' is defined in terms of itself" "$work/err"; then
    echo 'stage2_if_test.sh: an unselected operand followed a dependency cycle' >&2
    cat "$work/err" >&2
    exit 1
fi

# Three literal types are constants, but they are not Boolean constants.
write_source "$work/non_bool.ft" \
    '$if (1) { i32 A = 1; }' \
    '$if ("x") { i32 B = 1; }' \
    '$if ('"'"'x'"'"') { i32 C = 1; }'
status=0
"$fort" --check --std-dir "$std" "$work/non_bool.ft" >"$work/out" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] || [ "$(grep -Fc 'a $if condition must be bool' "$work/err")" -ne 3 ]; then
    echo 'stage2_if_test.sh: expected 3 non-Boolean condition errors' >&2
    cat "$work/err" >&2
    exit 1
fi
for kind in integer string char; do
    if ! grep -Fq "not $kind" "$work/err"; then
        echo "stage2_if_test.sh: missing the $kind condition error" >&2
        exit 1
    fi
done

# Invalid constant operators report diagnostics instead of reaching a constant
# helper with an unsupported type.
write_source "$work/invalid_operator.ft" \
    'bool FLAG = true;' \
    '$if (FLAG < FLAG) { i32 A = 1; }' \
    '$if (null == null) { i32 B = 1; }'
status=0
"$fort" --check --std-dir "$std" "$work/invalid_operator.ft" \
    >"$work/out" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] || [ "$(grep -c ' error: ' "$work/err")" -ne 2 ]; then
    echo 'stage2_if_test.sh: invalid constant operators did not report 2 errors' >&2
    cat "$work/err" >&2
    exit 1
fi

# Unary minus checks signedness before it calls the typed constant helper.
write_source "$work/unsigned_neg.ft" \
    'u8 VALUE = 1;' \
    '$if (-VALUE == 0) { fn main() i32 { return 0; } }'
expect_check_error "$work/unsigned_neg.ft" "requires a signed type"

# A shift count can use an integer type that differs from the left operand.
# The result keeps the left type. The maximum i64 also takes its default type
# before a cast.
write_source "$work/operator_boundaries.ft" \
    'u8 LEFT = 1;' \
    'u64 LAST = 7;' \
    'i8 SIGNED = -128;' \
    'u16 ONE = 1;' \
    '$if ((LEFT << LAST) == cast(128, u8)' \
    '    && (SIGNED >> ONE) == -64' \
    '    && cast(9223372036854775807, u64) == 9223372036854775807) {' \
    '    fn main() i32 { return 0; }' \
    '} else {' \
    '    fn main() i32 { return 1; }' \
    '}'
"$fort" --std-dir "$std" -o "$work/operator_boundaries" "$work/operator_boundaries.ft"
"$work/operator_boundaries"

# Both values outside the u8 shift-count range report diagnostics. A cast
# cannot let the first integer above i64 maximum avoid its default type.
write_source "$work/shift_boundaries.ft" \
    'u8 LEFT = 1;' \
    'i8 BELOW = -1;' \
    'u64 ABOVE = 8;' \
    '$if ((LEFT << BELOW) == 0) { i32 A = 1; }' \
    '$if ((LEFT << ABOVE) == 0) { i32 B = 1; }'
status=0
"$fort" --check --std-dir "$std" "$work/shift_boundaries.ft" \
    >"$work/out" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] \
    || [ "$(grep -Fc 'outside the left operand width' "$work/err")" -ne 2 ]; then
    echo 'stage2_if_test.sh: shift boundaries did not report 2 errors' >&2
    cat "$work/err" >&2
    exit 1
fi
write_source "$work/cast_boundary.ft" \
    '$if (cast(9223372036854775808, u64) == 9223372036854775808) {' \
    '    fn main() i32 { return 0; }' \
    '}'
expect_check_error "$work/cast_boundary.ft" "constant expression out of range"
write_source "$work/shift_char.ft" \
    'i64 ONE = 1;' \
    '$if ((ONE << '\''\0'\'') == 1) { fn main() i32 { return 0; } }'
expect_check_error "$work/shift_char.ft" "a shift count in a \$if condition must be an integer"

# Constant expressions permit numeric and character casts. They do not
# permit a cast from or to bool.
write_source "$work/bool_cast.ft" \
    '$if (cast(true, i32) == 1) { i32 A = 1; }' \
    '$if (cast(1, bool)) { i32 B = 1; }'
status=0
"$fort" --check --std-dir "$std" "$work/bool_cast.ft" \
    >"$work/out" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] || [ "$(grep -Fc 'cannot use bool' "$work/err")" -ne 2 ]; then
    echo 'stage2_if_test.sh: bool casts did not report 2 errors' >&2
    cat "$work/err" >&2
    exit 1
fi

# `sizeof` supplies an untyped integer constant before semantic checking. The
# selector computes fixed scalar, reference, span, and array layouts.
write_source "$work/sizeof.ft" \
    '$if (sizeof(i32) == 4 && sizeof(i16[3]) == 6' \
    '    && sizeof(void*) == 8 && sizeof(i32@) == 16) {' \
    '    fn main() i32 { return 0; }' \
    '} else {' \
    '    fn main() i32 { return 1; }' \
    '}'
"$fort" --std-dir "$std" -o "$work/sizeof" "$work/sizeof.ft"
"$work/sizeof"

# Fixed-array `.len` uses the declared type. It does not evaluate the array.
write_source "$work/array_len.ft" \
    'i32[3] ITEMS = {};' \
    '$if (ITEMS.len == 3) {' \
    '    fn main() i32 { return 0; }' \
    '} else {' \
    '    fn main() i32 { return 1; }' \
    '}'
"$fort" --std-dir "$std" -o "$work/array_len" "$work/array_len.ft"
"$work/array_len"
write_source "$work/sizeof_invalid.ft" \
    '$if (sizeof(void) == 0) { i32 A = 1; }' \
    '$if (sizeof(i64[1152921504606846976]) == 0) { i32 B = 1; }'
status=0
"$fort" --check --std-dir "$std" "$work/sizeof_invalid.ft" \
    >"$work/out" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] || [ "$(grep -c ' error: ' "$work/err")" -ne 2 ] \
    || ! grep -Fq "'sizeof' needs a sized selection type" "$work/err" \
    || ! grep -Fq 'type is too large' "$work/err"; then
    echo 'stage2_if_test.sh: invalid sizeof types did not report 2 errors' >&2
    cat "$work/err" >&2
    exit 1
fi

# A fixed outer size does not make an unresolved named type available to
# selection. The rule also inspects function parameter and result types.
write_source "$work/sizeof_named.ft" \
    '$if (sizeof(missing*) == 8) { i32 POINTER = 1; }' \
    '$if (sizeof(fn (missing) i32) == 8) { i32 FUNCTION = 1; }' \
    '$if (sizeof(missing[1]) == 8) { i32 ARRAY = 1; }'
status=0
"$fort" --check --std-dir "$std" "$work/sizeof_named.ft" \
    >"$work/out" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] \
    || [ "$(grep -Fc "'sizeof' cannot use an unresolved named type" "$work/err")" -ne 3 ]; then
    echo 'stage2_if_test.sh: sizeof accepted an unresolved named type' >&2
    cat "$work/err" >&2
    exit 1
fi

# A declared same-module name is available when a reference size does not need
# the named value layout.
write_source "$work/sizeof_named_pointer.ft" \
    'struct node { i32 value; }' \
    '$if (sizeof(node*) == 8) {' \
    '    fn main() i32 { return 0; }' \
    '} else {' \
    '    fn main() i32 { return 1; }' \
    '}'
"$fort" --std-dir "$std" -o "$work/sizeof_named_pointer" \
    "$work/sizeof_named_pointer.ft"
"$work/sizeof_named_pointer"

# Selection cannot compute a named value layout. Short-circuiting does not
# compute that layout or report its error.
write_source "$work/sizeof_named_value.ft" \
    'struct node { i32 value; }' \
    '$if (sizeof(node) == 4) { i32 VALUE = 1; }'
expect_check_error "$work/sizeof_named_value.ft" "'sizeof' needs a sized selection type"
write_source "$work/sizeof_named_value_skipped.ft" \
    'struct node { i32 value; }' \
    '$if (true || sizeof(node) == 4) {' \
    '    fn main() i32 { return 0; }' \
    '} else {' \
    '    fn main() i32 { return 1; }' \
    '}'
"$fort" --std-dir "$std" -o "$work/sizeof_named_value_skipped" \
    "$work/sizeof_named_value_skipped.ft"
"$work/sizeof_named_value_skipped"

# Array-literal `.len` validates the element type before it reads the fixed
# length. A missing element type is not hidden by the suffix.
write_source "$work/array_literal_len_name.ft" \
    '$if (missing[1]{}.len == 1) { i32 VALUE = 1; }'
expect_check_error "$work/array_literal_len_name.ft" \
    "a fixed-array '.len' cannot use an unresolved named element type"
write_source "$work/array_literal_len_void.ft" \
    '$if (void[1]{}.len == 1) { i32 VALUE = 1; }'
expect_check_error "$work/array_literal_len_void.ft" \
    "a fixed-array '.len' cannot use void elements"

# A fixed-array length enters the dependency state before it evaluates the
# length expression. A self-cycle reports once and terminates.
write_source "$work/array_len_cycle.ft" \
    'i32[ITEMS.len] ITEMS = {};' \
    '$if (ITEMS.len == 1) { i32 VALUE = 1; }'
status=0
"$fort" --check --std-dir "$std" "$work/array_len_cycle.ft" \
    >"$work/out" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] || [ "$(grep -c ' error: ' "$work/err")" -ne 1 ] \
    || ! grep -Fq "'ITEMS' is defined in terms of itself" "$work/err"; then
    echo 'stage2_if_test.sh: fixed-array length cycle did not report once' >&2
    cat "$work/err" >&2
    exit 1
fi

# A failed child expression owns its diagnostic. Its cast or array-length
# parent does not add a wrapper diagnostic.
write_source "$work/cast_cycle.ft" \
    'i64 A = cast(B, i64);' \
    'i64 B = A;' \
    '$if (A == 0) { i32 VALUE = 1; }'
write_source "$work/array_wrapper_cycle.ft" \
    'i64 A = B;' \
    'i64 B = A;' \
    '$if (sizeof(i32[A]) == 4) { i32 VALUE = 1; }'
for wrapper in cast array_wrapper; do
    status=0
    "$fort" --check --std-dir "$std" "$work/${wrapper}_cycle.ft" \
        >"$work/out" 2>"$work/err" || status=$?
    if [ "$status" -ne 1 ] || [ "$(grep -c ' error: ' "$work/err")" -ne 1 ] \
        || ! grep -Fq "is defined in terms of itself" "$work/err"; then
        echo "stage2_if_test.sh: $wrapper cycle gained a wrapper diagnostic" >&2
        cat "$work/err" >&2
        exit 1
    fi
done

# Parsing does not skip an inactive declaration branch or statement branch.
write_source "$work/syntax_decl.ft" \
    '$if (true) { i32 OK = 1; } else { i32 BAD = ; }'
expect_check_error "$work/syntax_decl.ft" "expected an expression"
write_source "$work/syntax_stmt.ft" \
    'fn main() i32 {' \
    '    $if (true) { return 0; } else { i32 BAD = ; return 1; }' \
    '}'
expect_check_error "$work/syntax_stmt.ft" "expected an expression"

# A failed construct stops before the next `$if`. The parser then reports the
# second syntax error inside the compile-time condition.
write_source "$work/recover_decl.ft" \
    'i32 FIRST = ' \
    '$if (true) { i32 SECOND = ; }'
write_source "$work/recover_stmt.ft" \
    'fn main() i32 {' \
    '    i32 FIRST = ' \
    '    $if (true) { i32 SECOND = ; }' \
    '}'
for context in decl stmt; do
    status=0
    "$fort" --ast "$work/recover_$context.ft" >"$work/out" 2>"$work/err" || status=$?
    if [ "$status" -ne 1 ] || [ "$(grep -Fc 'expected an expression' "$work/err")" -ne 2 ]; then
        echo "stage2_if_test.sh: $context recovery swallowed a \$if diagnostic" >&2
        cat "$work/err" >&2
        exit 1
    fi
done

# Declaration recovery inside a compile-time branch leaves the closing brace
# for the branch parser. One bad initializer therefore reports once.
write_source "$work/recover_compile_decl.ft" \
    '$if (true) { i32 BAD = ; }' \
    'fn main() i32 { return 0; }'
status=0
"$fort" --ast "$work/recover_compile_decl.ft" \
    >"$work/out" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] || [ "$(grep -c ' error: ' "$work/err")" -ne 1 ] \
    || ! grep -Fq 'expected an expression' "$work/err" \
    || ! grep -Fq ' main ' "$work/out"; then
    echo 'stage2_if_test.sh: declaration-branch recovery lost its closing brace' >&2
    cat "$work/err" >&2
    exit 1
fi

# These four semantic errors are inactive in good mode. Bad mode proves that
# all four source constructs are errors. The selected branch also determines
# return analysis and ownership analysis.
write_source "$work/semantic.ft" \
    'fn main() i32 {' \
    '    $if ($cfg(mode) == "good") {' \
    '        return 0;' \
    '    } else {' \
    '        bool wrong = 1;' \
    '        i32 missing_copy = missing_value;' \
    '        break;' \
    '        i32 mut* own first = new(i32);' \
    '        i32 mut* own second = first;' \
    '        del(first);' \
    '        del(second);' \
    '        return 1;' \
    '    }' \
    '}'
"$fort" --check --std-dir "$std" --cfg mode=good "$work/semantic.ft" \
    >"$work/out" 2>"$work/err"
if [ -s "$work/err" ]; then
    echo 'stage2_if_test.sh: an inactive semantic branch reported a diagnostic' >&2
    cat "$work/err" >&2
    exit 1
fi
status=0
"$fort" --check --std-dir "$std" --cfg mode=bad "$work/semantic.ft" \
    >"$work/out" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] || [ "$(grep -c ' error: ' "$work/err")" -lt 4 ]; then
    echo 'stage2_if_test.sh: bad mode did not expose 4 semantic errors' >&2
    cat "$work/err" >&2
    exit 1
fi
for want in 'does not become bool' "unknown name 'missing_value'" \
    "'break' outside a loop or switch" "copying an owning value requires 'move'"; do
    if ! grep -Fq "$want" "$work/err"; then
        echo "stage2_if_test.sh: bad mode omitted: $want" >&2
        cat "$work/err" >&2
        exit 1
    fi
done

# The index contains selected declarations and references only.
write_source "$work/index.ft" \
    'bool SELECT_FLAG = true;' \
    '$if (SELECT_FLAG) {' \
    '    i32 SELECTED_VALUE = 7;' \
    '    fn selected_fn() i32 { return SELECTED_VALUE; }' \
    '} else {' \
    '    i32 INACTIVE_VALUE = 8;' \
    '    fn inactive_fn() i32 { return INACTIVE_VALUE; }' \
    '}' \
    'fn main() i32 { return selected_fn() - 7; }'
"$fort" --index --std-dir "$std" "$work/index.ft" >"$work/index.json"
if [ "$(grep -o '"name":"SELECT_FLAG"' "$work/index.json" | wc -l)" -ne 2 ]; then
    echo 'stage2_if_test.sh: the index omitted the selected condition reference' >&2
    exit 1
fi
for name in SELECTED_VALUE selected_fn; do
    if ! grep -Fq "\"name\":\"$name\"" "$work/index.json"; then
        echo "stage2_if_test.sh: the index omitted $name" >&2
        exit 1
    fi
done
for name in INACTIVE_VALUE inactive_fn; do
    if grep -Fq "$name" "$work/index.json"; then
        echo "stage2_if_test.sh: the index included $name" >&2
        exit 1
    fi
done

# A selection error makes the module unavailable to semantic and index walks.
# Neither unresolved branch contributes an index name.
write_source "$work/index_selection_error.ft" \
    '$if (MISSING_SELECTION) {' \
    '    i32 THEN_AFTER_ERROR = 1;' \
    '} else {' \
    '    i32 ELSE_AFTER_ERROR = 2;' \
    '}'
status=0
"$fort" --index --std-dir "$std" "$work/index_selection_error.ft" \
    >"$work/index_error.json" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] || grep -Eq 'THEN_AFTER_ERROR|ELSE_AFTER_ERROR' \
    "$work/index_error.json"; then
    echo 'stage2_if_test.sh: an unresolved selection branch reached the index' >&2
    cat "$work/index_error.json" >&2
    exit 1
fi

# Unknown-name guidance scans the selected declaration tree for enum members.
write_source "$work/selected_enum.ft" \
    '$if (true) {' \
    '    enum color { red, }' \
    '}' \
    'fn main() i32 { return red; }'
expect_check_error "$work/selected_enum.ft" 'an enum member is written color.red'

# The inactive declaration branch emits no symbol and no string data.
write_source "$work/ir.ft" \
    '$if (true) {' \
    '    string ACTIVE_TEXT = "active_bytes";' \
    '    fn active_symbol() i32 { return 0; }' \
    '} else {' \
    '    string INACTIVE_TEXT = "inactive_bytes_unique";' \
    '    fn inactive_symbol_unique() i32 { return 1; }' \
    '}' \
    'fn main() i32 {' \
    '    println(ACTIVE_TEXT);' \
    '    $if (true) {' \
    '        return active_symbol();' \
    '    } else {' \
    '        return active_symbol() * 37;' \
    '    }' \
    '}'
"$fort" --std-dir "$std" -S -o "$work/ir.ll" "$work/ir.ft"
if [ "$(grep -Fc 'inactive_symbol_unique' "$work/ir.ll" || true)" -ne 0 ] \
    || [ "$(grep -Fc 'inactive_bytes_unique' "$work/ir.ll" || true)" -ne 0 ]; then
    echo 'stage2_if_test.sh: inactive content reached LLVM IR' >&2
    exit 1
fi
sed -n '/define dso_local i32 @"ir.main"()/,/^}/p' "$work/ir.ll" >"$work/user_main.ll"
if ! grep -Fq 'define dso_local i32 @"ir.main"()' "$work/user_main.ll" \
    || grep -Fq 'mul i32' "$work/user_main.ll" \
    || grep -Eq '^[[:space:]]+br i1 ' "$work/user_main.ll"; then
    echo 'stage2_if_test.sh: statement selection reached user-function LLVM IR' >&2
    cat "$work/user_main.ll" >&2
    exit 1
fi

# Only direct unconditional constants belong to the selection environment.
mkdir "$work/modules"
write_source "$work/modules/dep.ft" 'bool IMPORTED = true;'
write_source "$work/modules/imported.ft" \
    'import dep.IMPORTED;' \
    '$if (IMPORTED) { fn main() i32 { return 0; } }'
expect_check_error "$work/modules/imported.ft" "'IMPORTED' is not available"
write_source "$work/conditional_name.ft" \
    '$if (true) {' \
    '    bool INNER = true;' \
    '    $if (INNER) { fn main() i32 { return 0; } }' \
    '}'
expect_check_error "$work/conditional_name.ft" "'INNER' is not available"
write_source "$work/runtime_name.ft" \
    'bool mut RUNTIME = true;' \
    '$if (RUNTIME) { fn main() i32 { return 0; } }'
expect_check_error "$work/runtime_name.ft" "'RUNTIME' is not available"

# A condition can read only a primitive or plain string constant value. A
# fixed-array `.len` above reads the array type, not its value.
write_source "$work/excluded_types.ft" \
    'enum choice { one, }' \
    'i32* POINTER = null;' \
    'i32@ SPAN = {};' \
    'i32[1] ARRAY = {};' \
    'choice ENUM_VALUE = {};' \
    '$if (POINTER == null) { i32 A = 1; }' \
    '$if (SPAN == SPAN) { i32 B = 1; }' \
    '$if (ARRAY == ARRAY) { i32 C = 1; }' \
    '$if (ENUM_VALUE == ENUM_VALUE) { i32 D = 1; }'
status=0
"$fort" --check --std-dir "$std" "$work/excluded_types.ft" \
    >"$work/out" 2>"$work/err" || status=$?
if [ "$status" -ne 1 ] \
    || [ "$(grep -Fc 'must have an unqualified primitive or plain string type' "$work/err")" \
        -ne 4 ]; then
    echo 'stage2_if_test.sh: 4 excluded constant type families were not rejected' >&2
    cat "$work/err" >&2
    exit 1
fi
write_source "$work/cycle.ft" \
    'bool A = B;' \
    'bool B = A;' \
    '$if (A) { fn main() i32 { return 0; } }'
expect_check_error "$work/cycle.ft" "is defined in terms of itself"
if [ "$(grep -Fc 'is defined in terms of itself' "$work/err")" -ne 1 ]; then
    echo 'stage2_if_test.sh: the dependency cycle did not report once' >&2
    exit 1
fi

# Five other grammar positions reject `$if`.
write_source "$work/field.ft" 'struct item { $if (true) { i32 value; } }'
write_source "$work/member.ft" 'enum item { $if (true) { value, } }'
write_source "$work/parameter.ft" 'fn f($if (true) { i32 value }) void {}'
write_source "$work/type.ft" 'fn f() $if {}'
write_source "$work/expression.ft" 'i32 VALUE = $if;'
for context in field member parameter type expression; do
    status=0
    "$fort" --ast "$work/$context.ft" >"$work/out" 2>"$work/err" || status=$?
    if [ "$status" -ne 1 ] || ! grep -Fq 'error:' "$work/err"; then
        echo "stage2_if_test.sh: $context position accepted \$if" >&2
        cat "$work/err" >&2
        exit 1
    fi
done

echo 'stage2 preserved 3 AST nodes, accepted bool, and rejected 3 non-Boolean conditions'
echo 'stage2 accepted 256 conditions and rejected the 257th condition once'
echo 'stage2 selected 254 nested branches with 256 conditions in each chain'
echo 'stage2 printed 65024 compile-time condition AST nodes without a signal'
echo 'stage2 used logical precedence, skipped 6 operands, and rejected 2 local type errors'
echo 'stage2 rejected 5 skipped unary and binary operator families once each'
echo 'stage2 matched 2 exact-type and 2 unsigned-negation errors across evaluated and skipped forms'
echo 'stage2 accepted 2 evaluated and 2 skipped untyped-character operator forms'
echo 'stage2 enforced unsigned negation, 5 shift cases, 2 cast boundaries, and 2 bool casts'
echo 'stage2 evaluated 4 sizeof forms and rejected 2 invalid sizeof types'
echo 'stage2 rejected 3 sizeof forms with unresolved named types'
echo 'stage2 accepted 1 sizeof pointer to a same-module named type'
echo 'stage2 rejected 1 direct named-value sizeof and skipped its paired evaluation'
echo 'stage2 evaluated 1 fixed-array length and rejected 4 constant type families'
echo 'stage2 rejected 2 array-literal lengths with invalid element types'
echo 'stage2 reported 3 dependency cycles once without wrapper diagnostics'
echo 'stage2 reported syntax from 2 inactive branches and after 2 prior failures'
echo 'stage2 preserved 1 declaration-branch closing brace after recovery'
echo 'stage2 accepted 3 alternative declaration pairs with 0 duplicate diagnostics'
echo 'stage2 reported 0 diagnostics from 4 inactive semantic errors'
echo 'stage2 selected control-flow and ownership analysis'
echo 'stage2 indexed 1 condition reference and omitted 2 inactive names'
echo 'stage2 indexed 0 branch names after 1 selection error'
echo 'stage2 preserved 1 selected enum-member spelling hint'
echo 'LLVM IR contains 0 inactive symbols, bytes, instructions, and runtime branches'
echo 'stage2 rejected 3 name classes, 4 type families, 1 cycle, and 5 grammar positions'

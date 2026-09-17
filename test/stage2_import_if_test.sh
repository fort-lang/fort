#!/bin/bash
# The source compiler selects module imports before any import path operation.
# D21.3
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: stage2_import_if_test.sh <stage2> <std-dir>" >&2
    exit 2
fi

fort=$1
std=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

write_source() {
    local file=$1
    shift
    mkdir -p "$(dirname "$file")"
    printf '%s\n' "$@" >"$file"
}

expect_one_error() {
    local file=$1
    local want=$2
    shift 2
    local status=0
    "$fort" --check --std-dir "$std" -I "$(dirname "$file")" "$@" "$file" \
        >"$work/out" 2>"$work/err" || status=$?
    if [ "$status" -ne 1 ] || [ "$(grep -c ' error: ' "$work/err")" -ne 1 ] \
        || ! grep -Fq "$want" "$work/err"; then
        echo "stage2_import_if_test.sh: expected one error: $want" >&2
        cat "$work/err" >&2
        exit 1
    fi
}

# An inactive missing import reports nothing. The same selected import reports
# the ordinary one import diagnostic.
write_source "$work/missing_inactive.ft" \
    '$if (false) { import missing; }' \
    'fn main() i32 { return 0; }'
"$fort" --check --std-dir "$std" -I "$work" "$work/missing_inactive.ft"
write_source "$work/missing_active.ft" \
    '$if (true) { import missing; }' \
    'fn main() i32 { return 0; }'
expect_one_error "$work/missing_active.ft" "module 'missing' not found"

# Two names and two calls fail before the compiler reads poison.ft. Its syntax
# error would add a second diagnostic if any condition reached import loading.
write_source "$work/poison.ft" 'fn broken() i32 { return ; }'
for case_name in first_name second_name first_call second_call; do
    case "$case_name" in
    first_name) condition='PICK' ;;
    second_name) condition='OTHER' ;;
    first_call) condition='choose()' ;;
    second_call) condition='choose(true)' ;;
    esac
    write_source "$work/$case_name.ft" \
        "\$if ($condition) { import poison; }" \
        'bool PICK = true;' \
        'bool OTHER = false;' \
        'i32[1] VALUES = {};' \
        'fn choose(bool value) bool { return value; }' \
        'fn main() i32 { return 0; }'
    if [[ "$case_name" == *_name ]]; then
        want='is not available to a $if condition'
    else
        want='expression is not available in a $if condition'
    fi
    expect_one_error "$work/$case_name.ft" "$want"
    if grep -Fq 'poison.ft' "$work/err"; then
        echo "stage2_import_if_test.sh: $case_name read poison.ft" >&2
        exit 1
    fi
done

# An all-empty nested chain stays a declaration selector. The import pass
# evaluates it when its containing import branch is selected. It skips the
# same shape in the inactive branch.
write_source "$work/nested_empty.ft" \
    '$if (false) {' \
    '    $if (INACTIVE_NESTED) {} else {}' \
    '    import poison;' \
    '} else {' \
    '    $if (REACHED_NESTED) {} else {}' \
    '    import safe;' \
    '}' \
    'fn main() i32 { return 0; }'
expect_one_error "$work/nested_empty.ft" "'REACHED_NESTED' is not available"
if grep -Eq 'INACTIVE_NESTED|poison.ft' "$work/err"; then
    echo 'stage2_import_if_test.sh: nested empty selection entered an inactive branch' >&2
    exit 1
fi

# Short-circuit selection resolves no missing name and evaluates no division
# trap. The selected second import proves that selection still continues.
write_source "$work/safe.ft" 'fn value() i32 { return 0; }'
write_source "$work/short.ft" \
    '$if (false && MISSING) { import absent; }' \
    '$if (true || (1 / 0 == 0)) { import safe; }' \
    'fn main() i32 { return safe.value(); }'
"$fort" --check --std-dir "$std" -I "$work" "$work/short.ft"

# The same real file in two module identities is harmless when inactive. It is
# the ordinary identity error when selected.
write_source "$work/identity/source.ft" 'fn value() i32 { return 0; }'
ln -s source.ft "$work/identity/alias.ft"
write_source "$work/identity/inactive.ft" \
    'import source;' \
    '$if (false) { import alias; }' \
    'fn main() i32 { return source.value(); }'
"$fort" --check --std-dir "$std" -I "$work/identity" "$work/identity/inactive.ft"
write_source "$work/identity/active.ft" \
    'import source;' \
    '$if (true) { import alias; }' \
    'fn main() i32 { return source.value(); }'
expect_one_error "$work/identity/active.ft" 'is the same file as module'

# A selected alias collides with an earlier binding. Its inactive pair causes
# no binding operation.
write_source "$work/duplicate/one.ft" 'fn value() i32 { return 0; }'
write_source "$work/duplicate/two.ft" 'fn value() i32 { return 0; }'
write_source "$work/duplicate/inactive.ft" \
    'import one as choice;' \
    '$if (false) { import two as choice; }' \
    'fn main() i32 { return choice.value(); }'
"$fort" --check --std-dir "$std" -I "$work/duplicate" "$work/duplicate/inactive.ft"
write_source "$work/duplicate/active.ft" \
    'import one as choice;' \
    '$if (true) { import two as choice; }' \
    'fn main() i32 { return choice.value(); }'
expect_one_error "$work/duplicate/active.ft" "redeclaration of 'choice'"

# A back edge does not form a cycle while its import is inactive. The selected
# pair reports the ordinary cycle at the closing import.
write_source "$work/cycle/loop_inactive.ft" 'import inactive;'
write_source "$work/cycle/inactive.ft" \
    '$if (false) { import loop_inactive; }' \
    'fn main() i32 { return 0; }'
"$fort" --check --std-dir "$std" -I "$work/cycle" "$work/cycle/inactive.ft"
write_source "$work/cycle/loop_active.ft" 'import active;'
write_source "$work/cycle/active.ft" \
    '$if (true) { import loop_active; }' \
    'fn main() i32 { return 0; }'
expect_one_error "$work/cycle/active.ft" 'circular import:'

# Parsed order applies before selection. Each late form reports once, including
# an inactive conditional import.
write_source "$work/order_dep.ft" 'fn value() i32 { return 0; }'
write_source "$work/order_direct.ft" \
    'fn before() i32 { return 0; }' \
    'import order_dep;'
expect_one_error "$work/order_direct.ft" 'an import comes before every declaration'
write_source "$work/order_conditional.ft" \
    'fn before() i32 { return 0; }' \
    '$if (false) { import order_dep; }'
expect_one_error "$work/order_conditional.ft" 'an import comes before every declaration'

# One source selects a different module for each target. The AST keeps both
# branches. The index and LLVM IR keep only the selected import and module.
write_source "$work/target/linux_dep.ft" \
    'fn selected_value() i32 { return 0; }' \
    'fn linux_symbol_unique() i32 { return 0; }'
write_source "$work/target/darwin_dep.ft" \
    'fn selected_value() i32 { return 0; }' \
    'fn darwin_symbol_unique() i32 { return 0; }'
write_source "$work/target/main.ft" \
    '$if ($cfg(target_os) == "linux") {' \
    '    import linux_dep as chosen;' \
    '} else {' \
    '    import darwin_dep as chosen;' \
    '}' \
    'fn main() i32 { return chosen.selected_value(); }'
"$fort" --ast "$work/target/main.ft" >"$work/target.ast"
for name in linux_dep darwin_dep; do
    if ! grep -Fq "$name" "$work/target.ast"; then
        echo "stage2_import_if_test.sh: the AST omitted $name" >&2
        exit 1
    fi
done

linux=x86_64-linux-gnu
darwin=arm64-apple-macosx15.0.0
"$fort" --index --target "$linux" --std-dir "$std" -I "$work/target" \
    "$work/target/main.ft" >"$work/linux.json"
"$fort" --index --target "$darwin" --std-dir "$std" -I "$work/target" \
    "$work/target/main.ft" >"$work/darwin.json"
if ! grep -Fq 'linux_dep' "$work/linux.json" || grep -Fq 'darwin_dep' "$work/linux.json" \
    || ! grep -Fq 'darwin_dep' "$work/darwin.json" \
    || grep -Fq 'linux_dep' "$work/darwin.json"; then
    echo 'stage2_import_if_test.sh: the index did not follow the selected import' >&2
    exit 1
fi

"$fort" -S --target "$linux" --std-dir "$std" -I "$work/target" \
    -o "$work/linux.ll" "$work/target/main.ft"
"$fort" -S --target "$darwin" --std-dir "$std" -I "$work/target" \
    -o "$work/darwin.ll" "$work/target/main.ft"
if ! grep -Fq 'linux_symbol_unique' "$work/linux.ll" \
    || grep -Fq 'darwin_symbol_unique' "$work/linux.ll" \
    || ! grep -Fq 'darwin_symbol_unique' "$work/darwin.ll" \
    || grep -Fq 'linux_symbol_unique' "$work/darwin.ll"; then
    echo 'stage2_import_if_test.sh: LLVM IR contains an inactive module symbol' >&2
    exit 1
fi

echo 'conditional imports skipped 1 existing and 1 missing inactive module'
echo 'conditional imports rejected 2 names and 2 calls before 0 dependency reads'
echo 'conditional imports evaluated 1 reached nested-empty selector and skipped 1 inactive peer'
echo 'conditional imports skipped 1 missing lookup and 1 division trap'
echo '6 module cases covered 2 identities, 2 duplicate bindings, and 2 cycles'
echo '2 late import forms each reported 1 parse diagnostic'
echo '2 targets selected 2 modules, 2 index sets, and 2 LLVM modules'

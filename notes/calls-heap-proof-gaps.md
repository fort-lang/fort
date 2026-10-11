# Declared `calls_heap` bodies without proof

## Scope

This analysis measures revision `b5b33381554d9dd40726c08e2c0de568b9e2e803`.
The audit compiles 69 ownership roots. These roots reach 4,051 source body identities.
The `calls_heap` scope has 820 declared identities. It proves 117 identities.
It leaves 703 identities without proof.

The analysis uses `(file, name, declaration line, declaration column, instance)` as one identity.
It keeps declaration and terminal positions separate. The traced dump and ownership audit contain
exactly the same 703 identities without proof. Each set difference has 0 identities.

The trace wrapper returns each input proof status without a change. It records each rule that makes
an incomplete terminal. A dump has one result for each root and a completion marker. The classifier
checks the marker, each result status, each result digest, and the exact result set.

## Groups

The classifier records the first stopping rule for each body. It tests simultaneous summary and
transfer stops first. Thus, the table partitions first stops. It does not partition all rules that
a body needs for complete proof. Removing one stop can expose another rule from a different group.

| rule front | count | first stopping rule |
|---|---:|---|
| Control-flow merge | 186 | `follow_targets` |
| Projected access | 189 | path or read |
| Aggregate value | 118 | `aggregate_value` |
| Terminator or loan | 51 | `extract` |
| Call or publication | 31 | call or publication |
| Indirect owning output | 23 | `output_ready` |
| Owning-reference result | 22 | result type |
| Local input or allocation | 21 | seed or assignment |
| Transfer precondition | 57 | transfer validation |
| Combined summary and transfer | 5 | two rules |

Each group has these three examples:

- Control-flow merge: `ast.is_type_kind`, `check.check_poisoned`, and
  `check_stmt.check_compile_if`. Their terminal operations are `goto` terminators.
- Projected access: `ast.list_reserve`, `check.syms_reserve`, and `comptime.reserve`. Each
  terminal operation is a projected less-than assignment.
- Aggregate value: `check.check_close_brace`, `comptime.value_none`, and
  `containers.pool_create`. Their operations are a struct return copy and two constants.
- Terminator or loan: `ast.sym_kind_name`, `check.default_rank`, and
  `check_stmt.compound_operator`. Their terminal operations are non-boolean switches.
- Call or publication: `check.check_node_value`, `check_stmt.check_defer`, and
  `comptime.local_shift_result`. Their terminal operations assign fort call results.
- Indirect owning output: `containers.pool_free`, `fir_lower.unsupported`, and
  `ownership_combinations.free`. Their operations are two owner moves and one aggregate store.
- Owning-reference result: `ast.ast_child_mut`, `check.struct_of`, and `fir.struct_decl`. Their
  operations are two comparisons and one `live` statement.
- Local input or allocation: `fir.module_add_function`, `fir_flow.bits_new`, and
  `fir_parse.decimal`. Their operations create an owning span, allocate storage, and copy a string.
- Transfer precondition: `ast.ast_arena_count`, `check.check_sym_count`, and
  `consts.cv_to_i64`. Their operations are two projected reads and one pointer output.
- Combined summary and transfer: `ownership_heap.free_leaves`,
  `ownership_source_raw.free_value`, and `ownership_summary.free_publication`. Their operations
  are two constant outputs and one projected release.

The counts sum to 703. The reasons are 514 `unknown_effect`, 183 `missing_path`, and 6
`missing_correspondence`.

## Cyclic control-flow overlap

The dump uses graph reduction to find directed CFG cycles. Block numbering does not affect this
measure. It finds 162 bodies with a cyclic CFG. The control-flow group contains 32 of them.
The other 130 bodies have these first-stop groups:

| first-stop group | bodies with a cyclic CFG |
|---|---:|
| Projected access | 102 |
| Aggregate value | 9 |
| Terminator or loan | 4 |
| Call or publication | 0 |
| Indirect owning output | 3 |
| Owning-reference result | 5 |
| Local input or allocation | 7 |
| Transfer precondition | 0 |
| Combined summary and transfer | 0 |

`diag.loc_is_ordered` has an acyclic edge to a lower-numbered block. The old block-number measure
incorrectly counted it as a loop. The exact measure excludes it.

`ast.list_reserve` has a cyclic CFG. It requires projected-access and control-flow rules.
`containers.pool_free` also has a cyclic CFG. It requires owning-output and control-flow rules.
Their scoped rule-front work remains independent. Each ticket measures its rule front. It does not
require complete body proof.

`types.array_size` has an acyclic CFG. The combined group has 0 cyclic bodies. Thus, the combined
work has no control-flow dependency. It follows call, owning-output, and transfer work.

The trace cannot identify each later stopping rule before it removes the earlier rule. Therefore,
each implementation measures its assigned rule front. It does not require complete proof for its
baseline identities. Each implementation reruns the classification and records each next result.
This rule lets the nine base groups proceed in parallel.

## Rule boundaries

The control-flow and projected-access groups implement the path rules in D17.14-D17.15.
The aggregate group also implements transfer, move, aggregate, and overwrite rules in
D17.5-D17.7 and D17.11. The terminator group preserves the range-loan rule in D17.10.
The owning-output, owning-result, local-operation, and transfer groups implement the storage rules
in D17.5-D17.7 and D17.14-D17.15. The owning-output and transfer groups also implement D17.11.
D17.18 defines the proof scope for all groups.

The five combined identities have these pairs:

- `types.array_size` has call extraction and read validation.
- `ownership_heap.free_leaves` has output extraction and source validation.
- `ownership_source_raw.free_value` has output extraction and source validation.
- `ownership_summary.free_publication` has output extraction and release application.
- `ownership_summary_model.free_renamed` has output extraction and release application.

The nine base rule fronts can proceed independently. Complete body proof remains an integration
result. Enforcement follows all rule-front work and any new group that a later stop exposes.

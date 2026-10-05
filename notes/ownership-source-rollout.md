# Incremental ownership checks for compiler source

Status: approved on 2026-10-05. The user authorizes implementation after the Plan mode workflow.
Source base: `592ae40fe7fd69ba15544d2425697becd847346b`.
Normative contract: D19.8 and `spec/toolchain.md` 1.1.

## Objective

Connect checked-in ownership analyses to actual compiler source. Add diagnostics in small
increments.
First audit coverage and missing producers. Then repair source and enforce measured proof scopes.
Use independent review, current-head green PR `ci-status`, and GitHub rebase merge for each
increment.

Keep trusted FFI, existing syntax, representations, and ABI.
Add no lifetime annotations, unsafe escape, foreign contract database, or user-program runtime
fields.
Do not weaken an analysis to make compiler source pass.

## Current coverage

The compiler checks owning temporary leaks, required moves, and invalid move or del operands.
It checks range-loop storage changes, unresolved range calls, and owning range variables.
These checks already run during compiler builds. They do not establish cross-call UAF protection.

Source evidence: `src/fort/check.ft` and `src/fort/check_stmt.ft`.
The driver imports no ownership analysis module at this base. Its --check stops after source
checking.
It does not implement --ownership-check. Ownership module tests consume supplied checked facts.
Those tests do not establish compiler-source integration.

The inventory contains 50 compiler files and 19 ownership modules.
It contains 2,417 top-level function declarations, including 722 in ownership modules.
These lexical counts do not count checked, lowered, or proved bodies.

Reproduce these counts at the source base:

```sh
rg --files src/fort -g '*.ft' | wc -l
rg --files src/fort -g 'ownership_*.ft' | wc -l
rg '^fn\s+' src/fort -g '*.ft' | wc -l
rg '^fn\s+' src/fort -g 'ownership_*.ft' | wc -l
```

The main.ft closure omits the 19 ownership modules. It cannot establish the 50-file inventory.
Audit main.ft first, then each remaining tracked compiler file as a root.
Discover the root inventory automatically. New files enter the denominator without a count constant.
Keep each checked closure and its target runtime bodies. Include imported uncalled functions.
Count selected bodies separately from inactive target branches and lexical declarations.
Deduplicate source counts by source identity. Retain each distinct context-body proof occurrence.
Never combine symbols, numeric keys, or FIR pointers across checker sessions.

## Analysis inventory

This table maps 19 modules. A role does not imply a ready source producer.

| Module | Role | Required source integration |
|---|---|---|
| `ownership_api` | Proof records | Keys, source ranges, proof status |
| `ownership_keys` | Canonical identities | Module, function, place, operation, event keys |
| `ownership_limits` | Counted proof limits | Production meters and retained first refusals |
| `ownership_events` | Ordered event storage | Validated events from actual source operations |
| `ownership_diag` | Text and JSON diagnostics | Source files and validated event witnesses |
| `ownership_state` | Storage facts | Allocations, slots, sources, obligations |
| `ownership_scope` | Private proof scopes | Producer storage and retained bindings |
| `ownership_combinations` | Finite combinations | Conditions, alternatives, correspondence |
| `ownership_type_work` | Counted type traversal | Checked types and complete traversal inputs |
| `ownership_graph` | Call targets and components | Verified bodies and stable function identities |
| `ownership_liveness` | Future semantic use | Local slots, accesses, ordered call effects |
| `ownership_transfer` | Local effects | Allocations, moves, releases, reads, storage ends |
| `ownership_places` | Aggregate storage | Projections, aliases, destinations |
| `ownership_regions` | Dynamic elements | Index choices, ranges, storage correspondence |
| `ownership_borrows` | Retained sources | Contents, sources, liveness, bindings |
| `ownership_raw_facts` | Canonical raw facts | Layout, lengths, addresses, alternatives |
| `ownership_raw` | Raw storage proof | Origins, windows, accesses, byte effects |
| `ownership_join` | Conservative joins | Corresponding core, condition, and raw states |
| `ownership_flow` | FIR path solving | Entry facts, effects, places, events, outcomes |

Call summaries, recursive heap cleanup, and process-boundary integration still need implementation.
Storage integration waits for complete production core and consumer activation.
Graph and liveness source producers can start after the shared contract merges.
Complete proof joins the missing features before default enablement.

## Compiler and report contract

Use one --ownership-check compiler flag. Bare selection always requests complete closure proof.
Add no stage flag or separate audit compiler executable.
Complete proof and zero violations exit 0. Violations or incomplete proof exit 1.
Usage, tool, and internal failures exit 2. Incomplete selected proof prevents code generation.
Ordinary unselected builds retain their existing behavior.

The first implementation supports check and build modes.
Reject selection with token, AST, and FIR inspection modes.
Analysis runs before build-mode transformations. Ownership verdicts remain mode independent.
The selected closure includes imported uncalled bodies and available runtime bodies.
The report runs every integrated analysis. Missing source producers remain incomplete.

Add `--ownership-report <file>`, requiring --ownership-check.
Keep text diagnostics and diagnostic JSON version 1 unchanged.
Use the existing diagnostic format for incomplete proof.
Publish the separate version 1 report atomically. Report-writing failure exits 2.
Protect source inputs and compiler outputs from report-path replacement.

```sh
fort --check --ownership-check --ownership-report audit.json src/fort/main.ft
```

During the first milestone, missing producers make this command exit 1.
A complete report records the missing producers. It establishes no complete ownership acceptance.
Solver completion never establishes ownership safety by itself.

The shared report contract resides in `spec/toolchain.md` 1.1.
It fixes the compiler report, runner attestation, stage names, status values, identities, and
totals.
Compiler report identities use source declarations. Numeric function keys remain context-local.
The runner records source revision, compiler build revision, binary digest, and input digests.
The compiler needs no git command or SHA-256 implementation.
The runner uses fresh report paths. It rejects stale, missing, truncated, or inconsistent reports.
Capture input bytes before invocation and verify them afterward.
Retain failures and unexecuted rows. Missing rows never mean no effect.
Stream report rows or retain bounded summaries. Require no expanded proof-event history.

Trusted extern calls need no foreign body or hidden-effect summary (D17.13).
Apply checked argument rules and signature ownership transfers.
Keep known fort facts across the call. Do not infer that foreign code has no effects.
Missing fort-call summaries, indirect correspondence, and unknown fort effects remain incomplete.
Preserve the first budget refusal and its source location. Do not increase limits to obtain
acceptance.
Use production budget and event services. Do not use permissive test callbacks.

Graph construction and queries use an existing private W ledger.
Service dispatch and liveness use their actual service ledger.
Report each ledger and scope separately. Do not claim complete shared graph/liveness accounting.
Required accounting gaps remain incomplete. Never reset or split a computation to hide refusal.
A graph callback's unknown foreign-effect flag does not require a foreign body or summary.

## First milestone and parallel delivery

This documentation unit establishes the approved plan and the normative shared contract.
It changes no compiler, build, test, or user-program output implementation.
After this unit merges, assign these independent workstreams:

| Workstream | Owned behavior | Dependencies |
|---|---|---|
| Source producers | Source identities, verified FIR, graph and liveness inputs | Shared contract |
| Report tooling | Report validator, root inventory, attestation, aggregation | Shared contract |
| Driver and CI | Compiler options, report writer, source audit jobs | Both workstreams |

The source producer loads the actual compiler entry and imports with the selected standard root.
Enumerate checked bodies through fir_lower.program_functions. Record unsupported lowering
explicitly.
Verify FIR before graph or liveness analysis.
Produce canonical function keys, local-slot bindings, and direct-access facts from checked source.
Do not substitute unit-test facts for correspondence.
Run ownership_graph.build with its actual private ledger and production limits.
Bind graph queries through the production service wrapper.
Run ownership_liveness.solve with production budget and event services.
Keep checker, symbols, FIR, and proof contexts alive until dependent analyses finish teardown.

Unknown indirect accesses or fort effects remain incomplete even when a conservative solver
finishes.
Preserve unexecuted rows after prerequisite failure. A verifier failure remains an internal error.
The first milestone covers the automatic root inventory on native Linux and Darwin.
It reports source defects and missing producers. It includes no compiler-source ownership repairs.
It adds no local UAF diagnostic until local-effect integration supplies that proof.

## Later integration and source repairs

Each increment first reports across the complete compiler inventory.
Then repair measured source groups and enforce the increment's declared obligations.
Use separate deliverables for source repairs, producer defects, and analyzer defects.
Changes to shared representations or inferred contracts precede dependent source repairs.
Choose repair groups from measured proof coverage and dependency order, not filename order.

| Increment | Source producers | Diagnostic scope |
|---|---|---|
| Local effects | Allocation, move, release, storage end | Local UAF, lost owners, residual owners |
| Stored borrows | Paths, regions, sources, loans | Field and element UAF, escapes, retention |
| Raw storage | Layout, lengths, origins, byte effects | Invalid accesses and reconstruction |
| Calls and heap | Summaries, aliases, targets, cleanup | Cross-call UAF and recursive cleanup |
| Process exit | Globals, runtime owners, startup | Residual owners at executable exit |
| Complete proof | Complete closure, qualification, migration | Default ownership enablement |

Local integration follows complete production core and consumer activation.
Stored-borrow and raw-storage integration can proceed independently after local integration.
Call integration joins complete local, stored-borrow, and raw facts.
Process-exit integration follows complete call outcomes and cleanup facts.
Complete proof retains the existing qualification and mutation requirements.

Weak updates retain unselected obligations. Unknown aliases never establish separate storage.
Raw accesses require their original source, extent, and representation facts.
Unknown fort outcomes retain possible normal returns and cleanup obligations.
Executable normal exit requires empty owning globals in the complete checked closure.
Libraries retain ownership between calls and provide explicit cleanup.
No source group receives an exemption from complete selection.
Keep the existing range-call guard until complete selected proof establishes loan preservation.
Require complete qualification and source migration before enabling ownership analysis by default.

## Boundary cases and invariants

| Case | Invariant |
|---|---|
| Uncalled imported body | Selection includes its proof obligations. |
| Overlapping root closures | Each proof uses its own context. Only source counts deduplicate. |
| Inactive target branch | It never counts as a selected body. Its lexical count stays separate. |
| Missing source producer | Complete selection exits 1 and reports incomplete proof. |
| Empty selected-body inventory | All seven analyses still require complete closure outcomes. |
| Conservative solver completion | Correspondence and ownership acceptance remain separate. |
| Unknown fort effect | Preserve possible effects, continuation, and cleanup obligations. |
| Known extern without a body | Trust the boundary. Apply argument rules and signature transfers. |
| Failed checking or lowering | Retain failed rows and unexecuted dependent analysis rows. |
| Verifier or tool failure | Exit 2. CI rejects the attempt. Preserve available evidence. |
| Budget refusal | Preserve the first reason and location. No limit increase changes the verdict. |
| Truncated or stale report | CI rejects it. Atomic publication never accepts partial coverage. |
| Report path names source | Reject the path. Preserve source and compiler output files. |
| Scoped CI acceptance | It never changes the compiler verdict or establishes complete safety. |
| Library return | Preserve owning globals. Require explicit cleanup before host termination. |
| Normal executable exit | Runtime cleanup precedes the empty-global obligation boundary. |

## CI, tests, and review

The first CI gate checks report integrity, root coverage, and tool execution.
Accept exit 0 or 1 only with valid fresh reports and the runner attestation.
Violations and incomplete proofs remain informational during the first milestone.
Reject exit 2, missing roots, missing reports, and invalid evidence.
Reject incomplete selected-body enumeration. Discovered rows establish no complete denominator.
Retain the failed root and its report. Unsupported lowering can remain valid exit-1 evidence when
enumeration is complete.
Add no coverage-regression baseline yet.
Preserve reports as PR artifacts. Record PR head, tested revision, source revision, and compiler
revision.
Require the audit jobs in ci-status on native Linux and Darwin.
Keep existing build and fixpoint gates.

Later enforcement reads the same reports and its declared source and proof scope.
It rejects violations, missing bodies, and incomplete proof in that scope.
Unsupported obligations outside that scope remain visible and informational.
No allowlist converts incompleteness into complete proof or suppresses an ownership diagnostic.

Use these development tests:

- Direct source reads, writes, moves, branches, and loops.
- Imported uncalled functions, overlapping closures, new roots, and target-selected declarations.
- Unresolved indirect access and unknown fort-call effects.
- Trusted extern controls without foreign bodies or hidden-effect summaries.
- Failed checking, unsupported lowering, verifier failure, and budget refusal.
- Cleanup after refusal and separate solver, correspondence, and proof outcomes.
- Exit statuses, option combinations, preserved diagnostic JSON, and report-write failure.
- Missing rows, duplicate identities, incorrect totals, stale files, and truncated reports.
- Identical verdicts under checked, release, and bounds-check variants on both targets.
- Expected source diagnostics for repaired and rejected programs in each later increment.

Compare selected and ordinary LLVM for accepted programs. Analysis adds no ownership runtime code.
Supplied-fact unit tests supplement actual source tests. They do not replace source correspondence.
Run relevant development tests, format checks, and lint checks before commits.
Require no local final gate. Require independent deep review for producers and ownership
integration.
Require green current-head PR ci-status and an up-to-date branch before GitHub rebase merge.
Do not push or merge main locally.

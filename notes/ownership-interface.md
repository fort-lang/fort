# Ownership analyzer interface

The API defines compiler analysis data. It adds no emitted runtime data or user-program ABI field.
It defines no annotation, extern contract, transfer algorithm, solver, producer, or renderer.
`ownership_api` defines records. `ownership_keys` compares finite semantic keys.
Both modules import only existing compiler modules or each other in that direction.
FIR imports neither module. T-291 owns the production adapter and registration.

## Concrete definition and storage owners

The API defines 64 concrete records in 18 families. It also defines 21 enum types.
A family is a planning group. It is not a struct count.
T-315 owns each record definition below. The storage column names the production storage owner.
The caller owns request records, result records, and borrowed descriptor headers.
A descriptor never transfers the storage that its slices designate.
The storage owner copies nested slices when it retains a descriptor beyond its input lifetime.
Test `ownership_env` owns verified FIR before any build-mode pass.

| Family | Records | Count | Storage owner |
|---|---|---:|---|
| Identities | `declaration_id` | 1 | T-291 |
|  | `function_id` | 1 | T-291 |
|  | `entity_id` | 1 | T-291 |
|  | `checked_binding` | 1 | T-291 |
| Positions | `operation_position` | 1 | T-291 |
|  | `source_position` | 1 | T-291 |
|  | `deferred_occurrence` | 1 | T-291 |
| Paths | `scalar_fact` | 1 | T-316 |
|  | `projection` | 1 | T-316 |
|  | `place_path` | 1 | T-316 |
| Regions | `byte_window` | 1 | T-316 |
|  | `layout_leaf` | 1 | T-316 |
|  | `region_partition` | 1 | T-316 |
|  | `region` | 1 | T-316 |
| Sources | `source_fact` | 1 | T-316 |
| Contents | `borrow_relation` | 1 | T-316 |
|  | `contents_fact` | 1 | T-316 |
| Obligations | `multiplicity` | 1 | T-316 |
|  | `owner_location` | 1 | T-316 |
|  | `history_fact` | 1 | T-316 |
|  | `allocation_fact` | 1 | T-316 |
| Graphs | `graph_node` | 1 | T-316 |
|  | `graph_edge` | 1 | T-316 |
|  | `shape_description` | 1 | T-316 |
| Predicates | `predicate_atom` | 1 | T-316 |
|  | `guard_fact` | 1 | T-316 |
|  | `correlation` | 1 | T-316 |
|  | `lost_fact` | 1 | T-316 |
| Status | `terminal_failure` | 1 | caller |
|  | `proof_status` | 1 | caller |
|  | `mutation_result` | 1 | caller |
| Ordered effects | `ordering_constraint` | 1 | caller |
|  | `ordered_effect` | 1 | caller |
| Aliases | `alias_binding` | 1 | caller |
|  | `overlap_alternative` | 1 | caller |
| Result storage | `result_binding` | 1 | caller |
| Outcomes | `outcome_fact` | 1 | caller |
| Globals | `global_input` | 1 | T-291 |
|  | `entry_input` | 1 | T-291 |
| Events | `witness_link` | 1 | T-317 |
|  | `event_note` | 1 | T-317 |
|  | `event_descriptor` | 1 | T-317 |
| Limits | `exhaustion` | 1 | T-317 |
|  | `limit_table` | 1 | T-317 |
|  | `used_counts` | 1 | T-317 |
|  | `charge_item` | 1 | T-317 |
|  | `charge_request` | 1 | T-317 |
|  | `charge_result` | 1 | T-317 |
| Services | `state_view` | 1 | caller |
|  | `state_handle` | 1 | caller |
|  | `event_handle` | 1 | caller |
|  | `transfer_request` | 1 | caller |
|  | `transfer_result` | 1 | caller |
|  | `graph_request` | 1 | caller |
|  | `graph_result` | 1 | caller |
|  | `ffi_request` | 1 | caller |
|  | `ffi_result` | 1 | caller |
|  | `global_request` | 1 | caller |
|  | `global_result` | 1 | caller |
|  | `event_request` | 1 | caller |
|  | `event_result` | 1 | caller |
|  | `diagnostic_request` | 1 | caller |
|  | `diagnostic_result` | 1 | caller |
|  | `services` | 1 | caller |

`state_view` borrows T-316 storage. `state_handle` designates caller-owned T-316 state storage.
`event_handle` designates separate T-317 event storage. The caller owns both handle records.
`graph_result` borrows its graph service's output store. `ffi_result` borrows its FFI output store.
`global_result` borrows its global service's output store. Each service owns that output store.
`transfer_result.retained` borrows caller state. `event_result` transfers no event-store storage.
`mutation_result` reports a state update. It owns no state storage.
A precision refusal returns a loss fact and leaves state unchanged.
The caller forms a sound residual before it uses the refused result.
A callback owns no caller request, checked symbol, checked type, or caller state merely through use.

## Module owners and import direction

| Module or responsibility | Owner |
|---|---|
| ownership_api, ownership_keys, verified test inputs, contract clients | T-315 |
| ownership_state | T-316 |
| ownership_limits, ownership_events | T-317 |
| ownership_join and conservative composition | T-278 |
| ownership_transfer | T-279 |
| ownership_flow | T-280 |
| ownership_places | T-281 |
| ownership_regions | T-282 |
| ownership_borrows | T-283 |
| ownership_summary, ownership_calls | T-284 |
| ownership_recursive | T-285 |
| ownership_heap | T-286 |
| ownership_raw | T-287 |
| ownership_ffi | T-288 |
| ownership_globals | T-289 |
| ownership_diag | T-290 |
| ownership_fir_adapter and production registration | T-291 |
| ownership_graph | T-307 |
| FIR loan statements and preservation | `ownership_flow` |
| Loan statement placement | Compiler lowering (`spec/fir.md` 5.2, 9.5, 10) |

T-316 imports only this API, these keys, and existing merged modules.
T-317 imports only this API, these keys, and existing merged modules.
Neither unit needs its sibling's implementation. T-278 connects the services after both merges.
Four compiled clients test transfer, graph, combined FFI/globals, and diagnostic contracts.
Their callbacks supply test facts. They establish no production fallback proof.

## Identity and independent facts

The closure builder assigns finite u32 IDs before solving. It orders modules and declarations first.
A function key includes its declaration and finite instance number.
Entity kind and ordinal distinguish source, allocation, slot, captured value, and structural keys.
Zero numeric coordinates remain valid. `entity_kind.none` supplies an explicit absent entity.
The closure builder bounds structural instances. It creates no key per runtime iteration or call
depth.
Keys contain no address. Comparisons use unsigned coordinates without subtraction.
Declaration, function, entity, and deferred occurrence identities remain separate.
Each deferred range expansion takes a distinct function-local FIR loan ID.
Source positions do not identify loans. Runtime iterations create no loan ID.

Contents, source validity, allocation validity, and obligation location remain separate records.
Span length, owner emptiness, and allocation identity remain separate facts.
A zero-element allocation can retain one obligation and a live source.
Multiplicity records zero, one, and many independently from alternative source sets.
A residual path, partition, shape, or graph edge retains its represented uncertainty.
None of those flags proves release, separation, foreign trust, or owner emptiness.
Unknown fort outcomes retain possible caller returns and normal-exit requirements.
Noreturn can establish no caller return. It establishes no abort or normal-cleanup class.
Normal process exit follows actual ordered cleanup. A library return preserves persistent globals.
Trusted foreign sources remain distinct from unknown fort sources and known released sources.

## Numeric limits and charges

`LIMIT_VERSION` is 1. The table has nine positive u64 slots: D, R, P, G, H, T, E, W, and V.
The API rejects a zero slot or another version. T-317 selects and measures production values.
No numeric test value selects a production default.

| Slot | Scope | Unit |
|---|---|---|
| D | path | One access-path projection. |
| R | state | One region, representative, or partition. |
| P | state | One predicate atom. |
| G | state | One guarded alternative. |
| H | computation | One structural template or bounded parameter. |
| T | call | One explicit target alternative. |
| E | function | One retained ordered effect node or summary relation. |
| W | computation | One work unit from the table below. |
| V | report | One event, witness link, or rendered record. |

| Work kind | One work unit |
|---|---|
| transfer | One attempted operation transfer. |
| join | One attempted pairwise state join. |
| widening | One attempted state widening. |
| substitution | One attempted symbolic relation substitution. |
| summary_solver | One attempted summary work-item evaluation. |

Charge categories use D, R, P, G, H, T, E, W, V order.
A charge request has one positive item per used category, in that order, with the matching scope.
The simultaneous-limit order adds scope, work kind, and attempted operation coordinates.
Scope order is path, state, function, call, computation, report.
Work order is transfer, join, widening, substitution, summary_solver.
The budget service checks the whole request before granting permission.
It returns the first exhausted category under this order. It records used, bound, and attempted
units.
D requests absolute projection depth. R requests prospective retained region cardinality.
P requests prospective predicate cardinality in one represented state.
E requests prospective retained relation cardinality for one function computation.
The meter stores the greatest accepted D, R, P, and E amounts.
Separate states can use one meter. Repeated facts do not add R or P units.
Graph rebuilds and separate function computations do not accumulate E copies.
A graph counts each retained relation occurrence and each unknown-contents fallback.
A state history mutator counts its retained history occurrences plus the incoming history.
Source validity changes retain no ordered history row. They request W without E.
Join and widening request complete canonical history cardinality.
Refusal preserves history and obligations.
An explicit domain table can impose a stricter E bound than the service table.
That local refusal records loss and incomplete proof without inventing a service counter value.
Equal source keys alone do not identify equal destinations or value versions.
W remains cumulative. Each new cardinality-query step needs one W permission before work.
Canonical region keys identify allocation representatives, explicit regions, and borrowed regions.
A partition key identifies a partition under its region key.
A node uses its allocation key when present. Source-validity rows add no separate region.
Unkeyed retained borrows and explicit regions prove no equality. Count each separately.
An absent graph-edge range adds no region. Join covering uses the same region mapping.
Scalar contents without guarded alternatives add no G unit.
Raw capture includes borrowed core regions and predicates in its prospective R/P requests.
Its additional raw-region and predicate mapping remains subject to feature qualification.

Counter saturation means exhaustion. A saturated result cannot grant mutation permission.
A denied result without exhaustion evidence is an invalid service result, not a measured work limit.
Denied charges leave caller state and known obligations unchanged.
A successful charge precedes output mutation. An event service charges retained V storage itself.
API transfer and global dispatch charge one transfer unit. Graph dispatch charges one solver unit.
FFI dispatch charges one substitution unit. Later implementations charge their remaining work
explicitly.
These dispatch charges describe API calls. They implement no transfer or solver algorithm.

## Callback inputs, outputs, and failure

Each service has a separate borrowed context pointer and a typed function value.
The caller keeps contexts and immutable requests live until the callback returns.
A callback changes only its designated mutable output store.
Transfer uses `destination.context` for caller-owned state. The service never frees that state.
Graph, FFI, and global callbacks retain their output stores until the caller finishes using results.
Callbacks must not retain checked symbols or types beyond their checked environment.
The caller keeps result slices live during later calls that read them.
The next call to a service can replace its output store.
The caller finishes reading old outputs first.

Each missing analyzer handler returns incomplete proof. An empty result supplies no empty-effect
fact.
A missing graph handler also retains residual fort alternatives. It grants no foreign trust.
Each analyzer dispatch requires budget and event handlers before successful proof.
`charge` never calls a mutation handler. Transfer dispatch calls its handler only after allowed
charge.
Missing budget, missing event, denied charge, and saturation retain failure in
`proof_status.terminal`.
This inline terminal record uses no ordinary event-store slot and has no dynamic allocation.
A callback must retain known facts and obligations when its proof remains incomplete.
`successful` requires complete proof, no failed verdict, and no terminal failure.
Bottom denotes no reaching execution. Unknown denotes absent information. Neither proves a safe
operation.

An event callback copies retained event text, note text, witness links, paths, and conditions before
return.
T-316 submits borrowed event descriptors. It allocates or frees no T-317 event storage.
An absent event service or a rejected event leaves an inline failed verdict.
A reporting callback cannot replace a non-success input kind with complete proof.
This rule applies independently from `failed`. An accepted report retains the input terminal record.
A callback's new incomplete failure takes precedence over an incomplete input status.
An accepted complete callback retains an incomplete input status, even if it sets `failed`.
Report suppression changes no state or obligation.
A missing reporting service returns incomplete proof and retains any existing terminal record.
The existing terminal record takes precedence over the new missing-service reason.
V refusal returns incomplete failed proof with `report_limit` in the fixed terminal record.
An insertion that evicts an event returns this failure to its caller.
This new report failure takes precedence over the earlier result status and terminal record.
The immutable request retains its earlier analysis status.
V refusal establishes no successful proof.
A missing renderer returns incomplete proof. It cannot make failed compilation successful.
T-290 owns text and JSON rendering under `spec/toolchain.md` 4.2.
The event key order uses module/function, source range, operation, class, relation, then stable
event ID.
Notes use causal role, source range, and relation ID.

## Inputs and teardown

`ownership_env` lowers checked source and runs the existing FIR verifier before accepting a body.
It runs no build-mode transformation. It requires no driver option or analyzer registration.
The environment keeps borrowed symbols and types within the checked environment's lifetime.
`accept` requires a function type and the same checked type table.
It verifies caller-owned FIR before adoption. A refused input remains with its caller.
It also verifies its current owned body on each call.
A successful acceptance keeps that body's storage and callback handles.
A refused owned body stays in the environment and clears its verified flag.
Verification releases no checked symbol or type.

`loan_begin L p [h]` and `loan_end L` supply collection loans through actual FIR statements.
The flow solver captures header and containing allocation sources at begin.
A clone retains those captured facts until end. Local last use does not shorten the loan.
The solver reads direct effects from FIR and call effects from instantiated ordered outcomes.
A missing producer step cannot hide an assignment, move operand, release, or call.
An unknown call in an open loan yields incomplete proof.

`ownership_flow.place_binding` maps an actual FIR place to an ownership storage path.
The solver checks the current function identity, FIR base, and projection sequence.
Local roots name the same slot. Global roots name the same checked symbol.
All bindings for one global base use the same ownership root.
A projected global binding requires a complete root binding and matching projections.
A missing, mismatched, residual, or unresolved binding gives incomplete proof when needed.
It cannot establish preservation. A direct local place needs no separate binding.
These flow records change no frozen `ownership_api` record layout.

The caller invokes teardown once per owned output store.
Teardown invokes the state destructor, clears its handle, invokes the event destructor, and clears
its handle.
An empty handle requires no destructor. A nonempty handle requires its designated destructor.
Replacing FIR first releases state and events that can borrow it.
The environment then releases FIR before releasing checked source storage.
It frees no borrowed checked symbol or type. Repeated empty teardown does nothing.
The caller releases other callback output stores before releasing their borrowed input environment.

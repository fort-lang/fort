# fort compiler: the invariants of the passes

This document holds the invariants of the compiler itself: what each pass owns, what it may read,
and the rules a change to it must keep. It describes the implementation. Where it disagrees with
`spec/decisions.md`, `spec/grammar.md` or the specification documents, those files win (D1.2),
and a difference between this file and the code is a bug in one of the two.

T-098 created the headings below. T-099 moved into them the compiler bullets of the
`## Technical Standards` section of `AGENTS.md`, one bullet at a time and without a rewrite.
That section split four ways: its test bullets went to `notes/testing.md`, its convention bullets
went to `notes/style.md`, the JavaScript conventions went to `editors/README.md`, and the rest
came here.

## 1. Terms

- **"span" names the fort type `T@`** (D3.5), so a byte or source extent is a **range** everywhere
  in the compiler: `str_from_range`, token byte ranges, `ASSERT_TOK_RANGE`, and `loc_t` ranges in
  the editor-support work. Never call an extent a span.

## 2. The lexer

- **Lexer recovery**: a lexical error costs the lexer its line, and the parser then recovers from
  whatever that missing line broke (D14.2) -- nothing when the line was a statement of its own,
  the enclosing construct when it carried a `{`, a `(` or a declaration header. `lex_file` reports
  the error, drops every token already lexed on that line, resumes at the start of the next one
  and keeps returning whether the file was clean, but the array it leaves always covers the rest
  of the file and ends in TOK_EOF, so `parse_module` runs on it whatever was reported and a caller
  asks whether the file is usable by comparing `diag_count()`. Dropping the line's earlier tokens
  is what keeps the parser from reporting a syntax error over the half construct that stood before
  the error, which the user never typed and cannot annotate: `test/lang/fail/lexical` is the test,
  since a diagnostic on an unannotated line fails the run (D14.5) and `parser_recovery_test.c`
  walks the same corpus. The cap of twenty diagnostics is the file's, not the parser's: `lex_file`
  opens it with `diag_begin_file()` and both report while `diag_file_count() < DIAG_MAX_PER_FILE`.

## 3. The parser

- **Node ranges**: a node's `loc` is a range (D20.4). A parser function ends a node's range with
  `finish(p, n)` at every successful return in which it consumed the node's trailing tokens: the
  nodes it built itself, and also a node a callee built whose `;` it consumed, as
  `parse_simple_statement` and `parse_var_decl` do. It leaves a node alone when the tokens it
  consumed are not part of it, as `parse_paren_expr` does with the `)` of a parenthesized
  expression, whose start cannot move over the `(` (D20.4). A name is recorded with
  `expect_name(p, n)`, and a node's start never moves: the nodes named after an operator still
  start at it (`toolchain.md` 4). `bootstrap0/test/parser_range_test.c` catches a missed `finish`
  only when the node has a child that ends after the node's anchor token, since what it checks is
  that a child's range lies inside its parent's: dropping the `finish` of a childless node (`break`,
  `continue`), of a marker-only type suffix (`* mut`) or of a trailing `;` leaves it green. Those
  need an explicit `parser_loc_test` assertion on the source text the range covers. Add a new node
  kind to the corpus of the first suite and an assertion to the second.
- **Parser recovery**: `p->failed` means "unwinding the construct a syntax error hit" (D14.2),
  not "the file is dead". Everything between the report and the next recovery point is silent, so
  a new parse function needs no error handling of its own: return NULL and the recovery point
  above it skips, records an `AST_ERROR` and clears `failed`. Two rules constrain a change there.
  A construct may return non-NULL with `failed` set -- `parse_block_tail`, `parse_switch`,
  `parse_struct_decl` and `parse_enum_decl` do when their `}` is missing -- and every caller must
  then propagate quietly, so a caller that tests `!= NULL` keeps working; the recovery point
  pushes that node and clears `failed` without skipping, since the construct already stands at
  the boundary. `spec_begin` clears `failed` and `spec_rewind` restores it, because a speculation
  answers about the tokens ahead and every speculation reads `failed` afterwards to tell a type
  that parsed from one that did not: a speculation made during an unwind, which is what the
  first statement of a body with no `{` does, would otherwise read the unwind and answer no. A
  recovery point did the same until T-136 gave `at_decl_start` two tokens of lookahead in place
  of its speculation, so the rule still holds and one of its two examples is gone.
  And every recovery must consume a token unless it is
  at the end of the file, or `parse_module` loops forever: `skip_to_boundary` bumps once when the
  construct consumed nothing. A skip counts the `(` and `[` the failed construct left open, from
  its first token, so the `;` of a `for` header or of an argument list is not mistaken for a
  statement boundary; it deliberately does not count a `{` it left open, since which brace that
  was is not decidable from the tokens and assuming the construct owns the next `}` eats the
  enclosing block's. Both braces of a body have a rule of their own: a body ends at a top-level
  keyword, and the `{` of a function, struct or enum body may be missing, because a declaration
  header is complete before it. `test/lang/fail` is the cascade test (`parser_recovery_test.c`
  walks it): a diagnostic on a line with no `//! error:` annotation fails the suite, and the
  diagnostic counts of the files with two syntax errors are asserted beside it, since a walk that
  only forbids unannotated lines also passes with recovery switched off. It also asserts the
  number of files it walked (`CORPUS_FILES`), so a ticket that adds a test under `test/lang/fail`
  raises that constant in the same commit, and one that adds a suite of its own to
  `bootstrap0/test/` runs `tools/vm configure` before `build`, since the executables are globbed at
  configure time.

## 4. The checker

- **Checker annotations**: `type`, `sym`, `aux` and the `CHECK_ANN_*` bits of `ann` belong to
  the checker (`check.h`); `check_module` clears them before it writes them, so checking one
  module twice starts from the tree the parser left rather than reading symbols of a checker
  that is gone. `aux` indexes the checker's own value vector on a constant node and holds a
  field's byte offset on a field declaration, so it is never read without the bit that says
  which it is. A new bit is declared above the last one and doubles `CHECK_ANN_END`, so that
  `CHECK_ANN_ALL`, the one mask `clear_annotations` clears, cannot leave it out;
  `check_own_test.c` sets every bit of `CHECK_ANN_ALL` on a node the checker marks with none and
  asserts a re-check wipes them, and ties `CHECK_ANN_END` to the highest declared bit. Write that
  test shape for any "cleared before it is written" invariant: asserting that a bit *is* set
  after a second pass passes whether or not the clearing happens, so it proves nothing.
- **The checker writes the emptiability of an lvalue on the node.** `check_expr` in
  `src/fort/check.ft` sets `ANN_EMPTY_READONLY` on an lvalue that is a module-level constant or a
  member of one (D7.10). It sets `ANN_EMPTY_IMMUTABLE` on an lvalue below an immutable
  indirection. An lvalue with neither bit is emptiable (D17.9). `check.empty_of` reads the two
  bits. It panics on a node without `ANN_LVALUE` and on a node with both bits. `check_expr` writes
  the bits only beside `ANN_LVALUE`, because the checker leaves `expr.empty` at `empty_immutable`
  on an rvalue. `check_emptiable` reads the operand of `move` and `del` through `empty_of`, and
  the linear check (T-182) will read the same bits to judge emptiability on the tree (T-260).
  bootstrap-0 writes neither bit, because the freeze (section 8) leaves the C unchanged.
- **A range loan is part of FIR** (D17.10, D19.8, `spec/fir.md` 7, 9.3, 9.5, 10).
  The lowering captures the collection, then emits `loan_begin` before counter initialization.
  It preserves the source place before a held address. A copied non-owning array has no loan.
  Done begins with `loan_end`. Return unwind ends each crossed loan after that scope's dead markers.
  An aborting defer stops unwind. Continue, break, and fall-off leave the loan open until done.
  Each deferred expansion takes a new ID. V14 applies seven rules to reachable FIR statements.
  It checks unique begins, collection types, stack order, equal joins, and empty returns.
  It checks header reads.
  It prunes unselected constant-switch edges and imposes no loan condition on aborts.
  A wrong placement can fail V14. V14 does not prove storage preservation by an executable effect.
  The ownership flow solver reads those effects from FIR and instantiated ordered call outcomes.
  It captures allocation sources at begin.
  Unresolved place bindings and unknown calls stay incomplete.
  Direct and ordered writes invalidate address facts after storage resolution.
  Calls invalidate these facts before a loan and between iterations.
  A missing call step clears these facts outside a loan.
  An unresolved write or call prevents later loan capture on that path.
  Direct loan and address-fact work charges W before mutation.
  Guarded effects use the same overlap tests.
  Either an outcome guard or an effect guard requires a condition witness for definite invalidity.
  A guarded overlapping effect gives incomplete proof without a condition witness.
  Aggregate result storage belongs to the caller. It is not private local storage.
  The FIR aggregate classification includes span and string results.
  Widen only open loan captures. A captured-fact work failure stops its path before block effects.
  A proved empty unrelated owner permits deletion only when no owner can overlap it.
  The ordinary source guard remains until the driver selects the ownership proof.
- **`src/fort` carries the `mut` in the declaration, where the C casts a `const` away.** The
  bootstrap holds a tree of `const ast_node_t*` and a record of `const sym_t*` and casts the
  qualifier off at each of the ten places the resolution writes through one. T-085 made a
  mut-adding cast an error (D3.14), so the fort compiler cannot copy that. Five fields carry the
  mark instead -- `ast.node.sym`, `ast.sym.node`, `ast.sym.owner`, `scope.binding.node` and
  `types.node.decl` -- and `ast.ast_child_mut` answers a child of a node the same way. The shallow
  model is what makes this work: immutability of a path never propagates through a pointer the
  path contains (D5.9), so `b->node->sym` reads a `sym mut*` through an immutable `binding*` and
  no cast stands anywhere. Three comparisons pay for it, because `==` wants identical types
  (D6.2): `check.ft`, `gen.ft` and `index.ft` each drop the mark with a cast on one side. A new
  field that the checker writes through a read-only path takes the mark; a cast does not reach it.
- **A rule about two modules belongs in the checker, not in the loader.** The loader builds the
  namespaces before any type exists, so a cross-module comparison written there can only compare
  syntax, and syntax is not the rule: T-074 moved D9.8's "two extern declarations of one C symbol
  must have identical signatures" out of `modules.c` because comparing spellings both refused a
  program no spelling could express (an enum the declaring module can only call `color` and the
  importer only `shade.color`) and accepted two genuinely different local types of one name. One
  `check_t` spans the whole closure and `check_program` already visits every declaration in the
  dependency order of D9.10, so such a rule rides on the `resolve_*` that gives the declaration
  its type -- a map in `check_t` holding the first declaration of each C name -- and duplicates no
  walk. Two consequences for anything keyed that way: `check_module` may be called twice on one
  module through one checker (the editor mode of D20.2) and the second pass makes *new* symbols
  and new nominal types for the same tree, so an entry is replaced when its AST node is the one
  being checked rather than compared against itself; and the notes of such a diagnostic are
  written under `if (!ck->mute)`, since `check_error` mutes the error but `diag_note` is not
  routed through it.
- **Lazy struct layout in the checker** (D3.8, D7.10): the resolution edges a written type opens
  must be exactly the value-containment edges, because `check_layout` reads "this struct is still
  being resolved" as "it contains itself by value" and reports an infinite size. So `named_type`
  resolves a named struct only when the written type stores it: `suffixes_store_base` in
  `check.c` answers that off the written suffixes -- a `*` or `@` anywhere stops it, a fixed
  array carries it through -- and a function type's own result and parameters pass false whatever
  their suffixes say, since a function pointer is a word. That made
  `struct vec { node mut* mut@ own items; }` before `struct node { vec list; }` an "infinite
  size" error while the same two declarations in the other order compiled, contradicting D7.10
  outright (T-082). The mechanism is worth stating exactly, because it is not a recursion:
  `resolve_sym` already returns early for a struct it is resolving, so the eager resolve never
  re-entered `vec` -- it resolved `node` while `vec` was mid-layout, and `node`'s own `vec` field
  then asked `check_layout` for a layout that had not finished. A demand graph with an edge the
  layout does not need is enough; it does not have to close a loop. The rule is D3.8's, amended
  there: value containment is a field written
  `B` or a fixed array of any rank over it, and nothing else. The rule to keep: a declaration
  order that changes whether a program compiles is a bug in the demand graph, not a limitation.
  10 of `bootstrap0/test/check_layout_test.c`'s 17 tests check both declaration orders
  (`grep -c 'TEST_RUN(.*_in_either_order)'` against `grep -c 'TEST_RUN('`), and
  `bootstrap0/test/gen_aggregate_test.c` holds the two emitted modules against each other line by
  line, since a layout is only observable through the offsets it moves. `suffixes_store_base` is the
  third spelling of "look behind fixed arrays" beside `behind_arrays` in `types.c` and
  `struct_of` in `check.c`; keep the three in step by reading them together. Its generality was
  untestable in bootstrap-0 by construction, since that compiler refuses `b[2][3]` and `b[3] mut@`,
  and T-043 made it testable in stage2 and tested it:
  `test/fort/check_resolve_test.ft` writes `node mut*[2][3]` and `node[3] mut@ own` (identity
  only, both declaration orders) beside `node[2][3]` (value containment, an infinite size in both
  orders), which are the two answers the demand graph must tell apart at rank two. A rule stated
  as untestable is worth re-reading whenever the subset grows.
- **An untyped constant reports at the point a context fixes its type, so every position that is
  no context must ask for its default type.** `untyped()` gives an untyped integer in
  `[2^63, 2^64 - 1]` the error type and reports nothing, because D4.5 leaves that report to the
  context and a fold may still bring the value back into range: `9223372036854775808 >> 1` is a
  legal 2^62. A position that is no context (D4.1) therefore drops the constant with the poison
  and nothing is ever said. Thirteen programs compiled, exited 0 and printed nothing in both
  compilers until T-125: `*C`, `C.x`, `C->x`, `C.len`, `C[0]`, `C[0 .. 1]`, `C()`, `&C`,
  `del(C)`, `move(C)`, `C = 1`, `C++` and `for (i32 x : C)`. `check_operand` is the one treatment:
  it checks the operand and, when it comes back poisoned, calls `default_type`, which is a no-op
  for an operand that is no untyped constant. The rule to keep: **a new operator that drops a
  poisoned operand calls `check_operand` and not `check_expr`.** `grep -c 'check_operand(ck,'`
  reads 8 in `check.c` and 3 in `check_stmt.c`, and the same two numbers in the fort twins. The
  shift is the one exception and says so in its own comment: it carries the constant outward
  instead, because its fold may still succeed. A constant that folds back into range leaves
  `check_operand` **typed** rather than poisoned, so the operator answers by its own rule:
  `*(2^63 >> 1)` reports `cannot dereference i64`, and that shape was silent too.
  **The 11 raw `check_expr` calls that remain all reach a report, since T-128.** 9 of them hand
  the operand to a context. The other 2 are `check.c:2773` and `:2774` (fort `check.ft:3656` and
  `:3657`), which hand both operands to `check_operands`; that function has four routes and each
  one reports. A shift carries the constant outward to the context (T-117). A pair of untyped
  operands that folds is reported by the fold. A pair of which one operand carries no value stays
  untyped for every operator but a comparison, so the context walks both operands and reports
  there. **A comparison is the one route with no context above it**, because it yields a plain
  `bool`: `check_untyped_pair` fixes the default type there, over **both** operands together, and
  retypes each against it. It took `out->type = a->type` until T-128, which dropped the other
  operand's poison and its constant, and that was worse than the thirteen above. A program that
  prints nothing tells the author nothing; these printed something false.
  `println(9223372036854775808 > (1 << n))` with `n == 1` printed `false` where the program is an
  error, `println((1 << n) > 2147483648)` printed `true` where `false` is right, and
  `println(2147483648 > (1 << n))` gave one comparison two widths and clang refused the module.
  The rule to keep: **the type of an untyped pair comes from both sides, through
  `untyped_pair_type`, and never from one side.** An untyped pair is two untyped sides, and a
  unary operator ends one: `check_unary` gives an operand with no value its default type at once
  and alone, so `-(1 << n)` is a **typed** i32 and the constant beside it takes i32 from a typed
  operand (D4.1). `println((1 << n) > 2147483648)` therefore runs and prints `false`, while
  `println(-(1 << n) > 2147483648)` and the `~` form report `constant 2147483648 does not fit
  i32`, in both compilers. That is the mechanism D4.5's note of 2026-09-14 ratifies, whose only
  worked example is the float case where the asymmetry helps; the integer case turns a legal
  program into an error and is the one to expect a question about.
  `fail/constants/014` and `run/constants/014` hold both halves.
- **Ownership in the checker** (D17): `check_owning(t)` is the one answer to "does a value of
  this type own an allocation" -- an `own` reference or an owning aggregate -- and it guards the
  layout, since `type_is_owning_aggregate` fatals on a struct that has none. Every own place is
  reached through `convert`, so the transfer rule of D17.5 lives there and nowhere else; the
  `return` of a bare `own` local is the one exception and goes through `check_return_value`.
  `expr_t.mut` is level-0 mutability and answers "may this be assigned to" (D5.7), which is
  **not** what `move` and `del` ask: they empty storage, which a binding's own `mut` does not
  govern, so `expr_t.empty` carries the separate question of D17.6 -- a local's own storage and a
  mutable indirection are `EMPTY_OK`, a module-level constant and its members are
  `EMPTY_READONLY`, an immutable indirection is `EMPTY_IMMUTABLE`, and the read-only memory stops
  at the first indirection. Reading `mut` where `empty` is meant silently lets `move(view[0])`
  through or refuses `del(buf)` on an immutable binding.

## 5. Modules, the driver and the diagnostics

- **Diagnostic records**: a `diag_record_t` owns its file name as well as its message. `loc.file`
  is borrowed from whoever reported the diagnostic -- the module set, whose pool holds every file
  name it read -- and that set is freed before `fort --check --json` writes its document (D20.2),
  so a record that kept the borrowed pointer hands out freed bytes. The symptom is not a crash:
  the freed block still held a NUL, so the document printed `"file":""` while the text form,
  written at report time, was right. `record_append` interns the name in the sink's own pool, and
  anything else a record must outlive its reporter for is copied the same way.
- **The rendered lines under a header line come from a lookup the sink calls** (T-191). The sink
  cannot import the session, because `session` imports `diag`, so `diag.set_lookup` stores a
  `fn (void mut*, string, string mut*) bool` and a context pointer, and `session.show_sources`
  installs `session.lookup_source` with the session itself as the context. The driver calls it
  in `front_end`, `tokens_entry` and `ast_entry`, beside `diag.set_text`. The sink then holds a
  pointer into the session, so the session must not move while its sink writes text; the driver
  keeps it in one local for the whole run. `diag.report` writes the header line and its rendered
  lines together as the diagnostic arrives, so the text order stays the report order that
  `run_tests.py --check-json` holds against the document. The lookup reads through
  `session.read_source`, the overlay first, so the rendered line is the text the tree was built
  from. A sink with no lookup (`diag.create()`, a language server, a unit test) writes header
  lines alone, and a sink with its text off never calls the lookup.
- **A module's identity is its real path, and realpath of a relative path costs a getcwd.** On
  macOS getcwd walks the entries of every directory on the way up, so a `-S` round of
  `test/fort/driver_lifetime_closure_test.ft` spent 93% of its time there: 22 s for 4000 rounds
  against 3.4 s for the `--ast` mode, and 60 s under `-j 6`. `modules.real_path` reads the
  working directory once per module set (`working_dir`) and hands realpath an absolute path;
  the same test then takes 12 s (2026-09-25).
- **Reading a tree after the front end**: `driver_front_end` does not own the analysis. The
  module set (which owns every tree) and the checker (which owns every symbol and type the
  annotations point to) are a `driver_analysis_t` the caller prepares and frees, because a `sym`
  or `type` slot dangles the moment `check_free` runs (sym.h). A build frees it as soon as the
  front end returns; `--check --json` frees it after the document has been written, since the
  index walk of D20.3 reads the trees then. A new pass over an annotated tree goes in that
  window, not after it. A whole-closure pass also takes its order from `module_set_pass_at`
  (modules.h): the dependency order, then the modules the loader read but never ordered. The
  checker and the index walk share it because the index's file order is documented as the
  checker's order (D20.3), and two copies of that loop would drift with no test able to see it.

- **A diagnostic about two declarations stands at the one in the module being checked**, and the
  other declaration is its note (D14.2, amended 2026-09-14 by T-112). The closure is checked in
  dependency order (D9.10), so that declaration is the later of the two and the note can name a
  module the reader did not write. Two declarations of one module have no dependency order between
  them, so the error stands at the later of the two by position. The reason is D20.2's drop rule:
  a client that cannot open a file may drop that file's diagnostics, and a nested note goes with
  the error it follows, so an error in a file the reader cannot open takes the note in the
  reader's own file with it and shows nothing. Two diagnostics have this shape and no more:
  `conflicting declarations of extern '<name>'` (`error_extern_conflict` in `check.c` and
  `check.ft`) and `redeclaration of '<name>'` (`error_redeclaration` in `modules.c` and
  `modules.ft`). Measured over the whole corpus on 2026-09-14, both compilers: 666 tests, 994
  diagnostics under bootstrap-0 and 545 under stage2, 9 notes each, of which 5 stand in another file
  than their error and all 5 put the error in the module being checked. The three other
  note-carrying diagnostics put their note where their error stands: `module '<p>' not found`,
  `module '<a>' is the same file as module '<b>'`, and the struct-or-enum note of the extern
  conflict. The 60 corpus diagnostics that stand in a file the reader did not write are all
  `not supported by the bootstrap compiler: ?:` inside `std/math.ft`, which has one place and is
  not this shape. What holds the rule, and why a `fail` test alone does not, is
  `notes/testing.md` 3. Mutation-measured on 2026-09-14: swapping the two
  places of the extern conflict takes `check_extern_test` red and `lang` to 5 failed in bootstrap-0,
  and `fort-modules` red and `lang` to 5 failed in stage2; swapping them at the
  redeclaration takes `modules_closure_test` and `lang` red in bootstrap-0 and `fort-modules` and
  `lang` red in stage2.

## 6. The IR emitter

- **Ownership proof uses storage identity, not only place spelling** (T-273, D17.15).
  `spec/memory-model.md` 2.6 defines the contract. Structural FIR comparisons remain conservative
  optimization rules. The proof must bind dynamic indices to their current values and substitute
  actual caller aliases. Distinct element regions do not prove distinct pointed-to allocations.
  A weak update preserves unselected contents and conditional ownership obligations.
  It must not discharge all candidate allocations for one selected release.
  Whole aggregate moves transfer all fields. Projected moves empty only their selected subobject.
  `ownership_places` walks only inline struct fields and fixed-array elements.
  It stops at each reference leaf, including a reference to a recursive type.
  Its caller resolves heap indirection to a canonical storage root before a strong update.
  Its caller substitutes aliases before it treats different storage roots as disjoint.
  A copied or moved borrow names its new containing field and keeps its original source.
  Direct slot copies and moves in `ownership_transfer` follow the same rule. A containing place
  that begins with the source slot maps that prefix to the destination; another place stays.
  Each selected borrowed leaf needs a live source before a copy or move.
  A moved or released owning leaf has an empty-owner fact with exact zero length.
  A delegated aggregate failure keeps the mapped event key and its exhaustion data.
  The module changes a cloned state first. A failed later mutation leaves the input state unchanged.
  A value transfer pays one W unit for each row that a scan reads and each row of its state copy,
  before the work. A leaf of the plan pays one unit and one contents scan. The transfer charges
  through `ownership_state.unit_spend`, as the content counts do, but at another unit: a content
  count pays one unit for each 64 rows that it sorts (`rows_spend`), and a transfer scan pays one
  unit for each row that it compares with a place. A W refusal in the transfer reports one work
  failure through the mapped route, as a state mutator does. The dispatch unit of `apply`, the
  first charge of each action, reports its refusal in the same way (`dispatch_refusals` in
  `test/fort/ownership_places_test.ft`). The removal passes keep the other rows in their order
  in one pass each. The type walk reads no state and pays no W; the byte kernel pays for the
  type before the transfer. `test/fort/ownership_places_test.ft` pins
  `1 + 6C + 4A + S` W for C contents, A allocation and S source rows. That move has no reference
  leaf, and no fact stands below its two places.
  Neither move rebases an inline address. Abstract temporary ownership ends on transfer even when
  LLVM omits generated source clearing. Normal return ends by-value parameter storage after defer,
  including parameter copies without dead markers. Symbolic caller sources have separate lifetimes.
  Aggregate _0 binds to actual caller storage before effects. Check old overlapping destination
  leaves after operand effects. With a holding temporary, check the actual result destination at the
  final write after defer. The 17 normative traces in memory model 2.6 are design evidence, not
  compiler probes.
- **Foreign-call preparation reads actual caller facts** (D17.13).
  `ownership_ffi.prepare_boundary` validates a checked extern declaration and captured operands.
  Its producer supplies complete operation, source, event, declaration, and storage correspondence.
  The supplied public state view names the paired core state's arrays.
  Checked type metadata stays valid until the producer releases the private plan.
  Preparation reads all fixed and variadic arguments before it selects an owning operand.
  A selected allocation needs its actual unique live owner and caller obligation.
  Canonical-empty owners select no allocation. An orphan owned cycle supplies no external owner.
  Complete graph flags do not repair inconsistent nodes, allocations, edges, or owner locations.
  Owned descendants need actual complete holding contents and checked typed field correspondence.
  Borrowed edges preserve their allocations outside the selected closure. Borrowed cycles need no
  acyclic ownership proof.
  The private plan copies operand facts, owner paths, edge paths, and nested region arrays.
  These copies preserve the original borrow sources. Checked layout metadata remains borrowed.
  Each scan and copy receives separate work permission. Each work charge has amount one.
  Semantic field and array descent receives path permission before it creates the next path.
  Checked price overflow refuses before delegated work. Cleanup needs no later work permission.
  A failed preparation releases its partial plan. It preserves caller facts and retained results.
  Unknown source or owner correspondence supplies no unconditional diagnostic witness.
  Preparation creates no transfer, fresh foreign source, foreign outcome, or public result view.
  A captured empty owner can have no value version, because a transfer writes it so.
- **The signature hook applies the trusted extern boundary** (D17.13).
  `ownership_ffi.apply_boundary` runs preparation first. It then copies the input core with
  counted work and never writes the input.
  Each selected allocation transfers once. Its obligation becomes false and its owner detached.
  The transfer keeps allocation and source validity. It adds a transfer history entry.
  It adds no release.
  The holding operand of an own argument becomes canonical empty.
  A borrowed argument changes no fact. A symbol name changes no fact: an extern named `free`
  or `memmove` gets no release and no byte effect.
  The producer supplies the result choices, their guards and facts, and the fresh keys.
  One choice is unconditional. Two or more choices need distinct new guards and new fact keys.
  The hook adds the guard and the facts of a choice to that choice's state only, as
  `ownership_regions.retain_choice` does. So `ownership_flow.refine_edge` reads the null test of
  each alternative from its own fact. The caller state cannot already hold a nullness fact on the
  fresh value version. The hook refuses a fresh source, allocation, value, or relation key that
  exists.
  An owning result needs a nonempty choice. An empty choice needs a nullness fact on the fresh
  version with `lower` exact 0 and no fact with exact 1. A nonempty owning choice cannot hold a
  null fact.
  The result write checks the state after the transfers. An equal live owner refuses with lost
  ownership. A prefix or residual overlap refuses with incomplete proof. Distinct exact fields and
  indices do not overlap. A live owner at one of several choices has no validated witness.
  A borrowed pointer result gets one fresh trusted source. It has no allocation and no relation to
  an argument. A nonempty owning result also gets one fresh allocation: count one, the owner at
  the destination, and an unknown length. An empty choice writes canonical empty contents.
  The hook adds no nullness fact of its own. A scalar result writes scalar contents with no source.
  The outcome is `unresolved_foreign`. Only `noreturn` removes the possible caller return.
  Cleanup stays required and unproved. Hidden foreign effects stay outside the proof.
  Only a complete call replaces the retained result. A refusal reports one event. It keeps the
  input core and the prior result.
  `ownership_ffi.adapter_call` serves `ownership_api.foreign`. Its context holds the private inputs
  that the frozen request has no field for.
  The hook suites probe supplied facts. They establish no source producer: the driver reports
  `missing_producer` for the three example 19 programs.
  Two propagation suites run the existing helpers on supplied trusted sources. Copies, field
  copies, void pointer casts, and wrapper returns keep the trusted source. A rebuilt integer
  pointer, an unknown fort source, and an own-adding cast get no trust. A callback body keeps
  ordinary fort rules for its own allocations, stack views, and own inputs.
  `ownership_flow.unknown_call` (read, not probed) makes each non-trusted source validity unknown
  after an `unresolved_foreign` outcome. D17.13 keeps known fort facts across the call, so the
  integration applies the hook successor states instead.
- **Raw storage retains its typed source facts** (T-275, D17.17).
  `spec/memory-model.md` 2.8 defines source objects, byte offsets, access windows, alignment, and
  reference representation effects. Pointer casts preserve those facts through void pointers.
  `src/fort/ownership_raw.ft` evaluates supplied checked facts.
  Its producer supplies exhaustive sources, captured value versions, complete headers, and resolved
  layouts. Its context borrows its paired core state and type table.
  The caller keeps both alive until it destroys the raw result.
  Raw results own copied expressions, place paths, origins, and addresses.
  A failure releases those copies and retains the input core state.
  The kernel checks maximum projection depth separately from cumulative work.
  Paid iterative worklists traverse type nodes, type pairs, and inline layout.
  Type computation consumes W. It does not consume projection depth.
  Array sizing uses checked multiplication and never calls recursive size_checked on an array.
  A complete raw path at D remains precise. Child permission precedes path allocation and storage.
  Nominal identity ends type comparison. Inline layout does not follow pointer payloads.
  Range copies charge lower and upper operands before output allocation.
  Projection copies charge each retained path element before allocation or copying.
  Entry charges map validation and keeps its route in stack storage.
  Each raw consumer charges repeated source searches, comparisons, copies, and allocations.
  A metadata copy price pays no later arithmetic work. Each address alternative pays its own work.
  Typed access proves nonnegative, representable offsets and endpoints for foreign sources too.
  Foreign trust supplies no static extent or alignment.
  Endpoint proof charges and releases scratch expressions.
  Type casts pay iterative comparison work before they use the existing reference conversion matrix.
  A reference cast never adds own (D3.14). The kernel refuses that cast as the checker does.
  Header reads pay for their delegated borrow and source walks before transfer inspection.
  Cleanup requires no work permission after the first terminal refusal.
  Its producer validates actual source correspondence before driver integration.
  Typed subobject windows do not grow when a cast erases the pointee type.
  Raw ranges require proved bounds and sufficient extent. Extents can remain symbolic.
  Integer reconstruction supplies no source proof, including an exact unmodified u64 round trip.
  Infer complete borrow copies from ordered byte effects after actual alias substitution.
  Partial representations need proof before a reference read or escape.
  Destructive overlapping owner writes require the old obligation empty or transferred first.
  Raw byte copying supplies no semantic move. Encoded owner bits in a byte buffer create no owner.
  Generated copies that implement move instead transfer the owning operand's original obligation.
  Type erasure does not hide owned descendants from shallow del.
  Zero-element allocations retain ownership with zero logical access extent.
  Foreign byte effects remain outside proof under D17.13. Known allocation release still invalidates
  related borrows, even when foreign extent trust remains available.
  Trusted foreign owner identity needs a complete current borrow and a unique live obligation.
  It needs no invented span length or allocation length. Heap owner identity keeps length equality.
  Ordinary reads charge source and allocation searches before they use foreign extent trust.
  `ownership_transfer.read_sources` charges one W unit for each borrow and for each source and
  allocation candidate that it compares. The earlier `borrows_valid` charged none.
  A live `unknown_fort` source still passes a read. It supplies no extent trust.
  Every feasible source must be trusted before an unknown extent can pass.
  Retained graphs copy allocation keys and validity for known foreign allocation releases.
  These partial snapshot rows contain no owner path or history. They do not prove owner identity.
  The graph charges snapshot storage and each lookup. A refused lookup retains its complete keys.
  Foreign result storage owns copied successor states and nested path, region, and effect views.
  Checked type metadata stays borrowed. Its owner keeps it alive through result teardown.
  Each copied projection and record requires a separate work charge.
  Storage copies supplied paths. The semantic producer proves their depth.
  A failed replacement preserves the complete prior result and returns empty failed views.
  A residual incomplete proof retains its facts. A failed supplied proof permits no publication.
  Taking a successor transfers its stores. Result teardown cannot free those transferred stores.
  Repeated take returns empty. Cleanup and repeated teardown require no new work permission.
  The three foreign storage suites probe supplied facts. They establish no source producer.
  The 12 normative traces are design evidence. The two raw suites probe supplied facts.
  The two raw suites establish the supplied raw facts.
  `src/fort/ownership_raw_bytes.ft` owns a stable core object and its paired raw context.
  Its ledger keeps containing storage separate from the original reference sources.
  Ordered interval fragments keep the original byte positions and captured value versions.
  Snapshots copy nested relations and extend no original source lifetime.
  Clone preparation charges all copied facts before allocation.
  Comparisons and later consumers have separate work charges.
  Byte interpretation checks added ownership at each reference level (D3.14).
  Equal containing places permit no added ownership mark.
  Typed publication resolves one captured reference leaf and one canonical core contents row.
  The actual leaf type accepts the original reference marks without added mutability or ownership.
  A byte view supplies neither the actual leaf type nor the canonical destination place.
  Ambiguous mappings supply no reference fact. Existing canonical empty facts remain empty.
  Complete publication also requires nonresidual captured, leaf, and canonical paths.
  Byte writes set each reference or aggregate parent fact to unknown when it lies between the
  captured object and the changed leaf. Repeated origins apply this to the matched element path.
  Unproved affected paths refuse the transaction and preserve caller input.
  A pointer variable before a captured dereference lies outside that object.
  Semantic moves retain those original marks across later moves.
  Incomplete byte writes set corresponding core contents to unknown, including unseeded leaves.
  Repeated reference writes match the exact element index between the origin prefix and leaf path.
  Count-one origins also match index zero before they change core contents.
  A count other than literal one makes an origin repeated, whatever its repeated flag says.
  The match needs a finite count, checked stride, and one exact core contents row.
  Unavailable or residual element correspondence refuses before publication.
  Disjoint elements retain their original borrow sources and complete contents.
  Byte translations compute bounded differences before addition near maximum endpoints.
  Canonical empty facts retain checked obligation absence in the separate ledger fact store.
  Unknown bytes retain that absence proof. They establish no valid reference or owner.
  Complete zero writes restore canonical empty bytes only after the overlap guard proves absence.
  Empty byte intervals overlap no interval and discard no owner obligation.
  Empty writes still require source validity and snapshot correspondence.
  Definite nonzero owners can supply unconditional owner-loss witnesses.
  Possible counts, uncertain owner paths, and malformed facts supply incomplete evidence.
  The residual owner-path flag also supplies incomplete evidence with no validated witness.
  A complete typed replacement can recover correspondence without reading old borrowed bytes.
  An unseeded typed replacement creates complete byte correspondence.
  It also updates the actual core row.
  The replacement requires one original reference leaf.
  Its bytes, path, and reference marks must match.
  Overlapping wider interpretations become incomplete first.
  Then the selected core row receives its new source.
  Unchanged owner bytes keep the original obligation and owner location.
  Destructive owner effects require empty or transferred original obligations.
  An obligation whose owner place equals or lies inside the captured origin place needs an owning
  leaf there. An owner above the origin place designates that storage. An owner past a dereference
  below it lies in other storage. Neither holds window bytes.
  The guard reads the origin place, so a producer's window place cannot hide an owner.
  Owner byte checks read only the snapshot bytes that land on an owning leaf.
  A snapshot of a window over F fragments holds up to 2F + 1 parts and no more parts than bytes.
  Paired R counts each part as a partition, so a snapshot never holds more than R parts.
  Shallow release refuses an owner whose place begins with the containing place or is residual.
  The paired region count refuses two fragments of one window or snapshot that share a byte, so
  each byte has one answer for every consumer.
  A missing event route refuses at its own effect and reports no other effect's event.
  Canonical zero preserves a proved empty owner. Unknown bytes supply no canonical-zero fact.
  Checked semantic moves transfer original obligations before byte-ledger publication.
  Their borrows retain the original designated sources. Raw copying creates no owner.
  Paid iterative queries compare reference types and measure layouts.
  Query refusal keeps the first terminal proof and the mapped operation and source.
  The paired R request counts the semantic borrowed-region union and current raw leaves.
  The paired count appends each region identity and each partition. Then it sorts each list
  once and keeps one entry of each equal run (`ownership_state.region_settle`,
  `ownership_raw_bytes.partition_settle`). A region append pays one W unit. A partition append
  pays two. Each settle pays one unit for each 64 sorted rows, so the W is linear in the state.
  `owner_empty` pays the core price once and the path price once for each allocation.
  At 100 extra allocations, sorted insertion cost 7057 W and is now 1377. The owner scan cost
  the core price times the allocations, 142620 W, and is now 4035. A zero write cost 167275 W
  and is now 11650. `test/fort/ownership_raw_bytes_work_test.ft` pins four states and checks
  that their costs add.
  A semantic move pays its copy price once. The transfer of places pays for the rows it reads.
  Before, the move paid the copy price times (leaves + 1 + contents + allocations) times 8: 477403
  W over the populated owner state, and the bound (589824 W) with 128 more rows. Now it pays
  20085, 36989 and 53893 W with 0, 128 and 256 more rows. Each row adds 132 units, and 96 of them
  are the core price that the 4 rewritten representations pay.
  `ownership_state.measure_regions` counts with a batch too. Sorted insertion pays at least two
  units for each keyed identity. A batch pays one unit for each identity and one for each merge
  of its full fixed array. Its growth pays one unit and one for each 64 rows that it copies, and
  its settle one for each 64 rows. So a batch never pays more for the same identities.
  A count keeps 64 identities in a fixed array. When the array is full, the count sorts it and
  keeps one entry of each equal run, and it grows only when more than 48 entries stay distinct.
  So a count whose identities repeat allocates nothing. In the 11 byte-kernel cleanup suites,
  320528 counts of 38 or 39 identities grew to heap storage without the merge, and none now.
  With sorted insertion, 64 and 128 more allocations cost 51025 and 93402 W, a square term; the
  batch pays 26841 and 33597 W. A batch that paid two units for each append refused a join of
  one allocation at W 6, where sorted insertion fits: it paid one settle unit more.
  Representation partitions use the full source key and a canonical half-open storage interval.
  Snapshot partitions use their ledger's snapshot key and a canonical half-open snapshot interval.
  These two namespaces remain distinct. Duplicate identities count once within one namespace.
  Empty intervals add no partition. A missing key, a key of another kind, or ordinal zero proves
  no equality, so each such row counts once.
  Unknown fragments add no borrowed-source fact. Empty-owner rows add no region by themselves.
  Counting equality proves no storage, value, permission, or ownership correspondence.
  Prospective edits count survivors and new facts before fragment storage grows.
  A clone requests the same absolute R amount. Repeated equal writes do not accumulate R.
  Each refused edit keeps caller input. These rules implement FIR 14.1 without changing its bounds.
  Original layout leaves and residual descendants control erased writes and shallow release.
  The byte suites establish supplied ordered effects and transactional storage cleanup.
  These suites do not establish producer integration.
  `src/fort/ownership_raw_join.ft` joins or widens two paired states into an owned result.
  The core facts use `ownership_join.join` or `ownership_join.widen` with the mapped event route.
  A ledger window keeps a byte only when both inputs prove one original value, type, and byte
  position at it. Equal fragment counts prove nothing. A different split of equal bytes keeps
  its correspondence. Distinct complete versions can stay as two guarded alternative states.
  Their G content is the guarded units of both cores plus `alternative_units(2)`. G is a
  precision bound (FIR 14.1): content within G keeps the alternatives and charges that content
  before either copy; content above G merges the inputs and records a lost fact of category G for
  the window. A refusal remains only when the meter bound is below the request. A widen always
  merges. The core join covers the guarded units of its result: above G, each core row becomes
  residual and unknown.
  When an input proved the correspondence of a window and the join loses it, each core row that
  depends on a stored window place or an origin place of either input becomes unknown. A
  dependent row lies at, below, or above that place in one storage; a parent past a dereference
  is other storage. Canonical empty rows stay empty, as in byte publication. A window that no
  input proved changes no row, so a join of two equal states changes no row.
  A window of one input keeps no fragment. Two windows of one interval with another stored type,
  place, alignment, or correspondence flag are both dropped. A lost correspondence and a dropped
  pair each add a lost fact of category R, with reason `unknown_effect`, for the window source.
  The joined context keeps an origin with its layout only when both inputs hold an equal origin of
  that source. One input proves nothing about the window or count of the other path, so raw
  access to any other source needs a new capture.
  A captured bound, a relation, or a canonical empty fact stays only when both inputs hold it.
  A snapshot with one key and one size on both inputs merges into contiguous parts, and each
  disagreeing part becomes unknown. Other snapshots are dropped.
  The join pays the copy work of both inputs before it allocates. A later refusal frees each
  tentative record. The meter charge and the mapped event stay.
  A bottom input gives a clone of the other input. An incomplete input keeps its own proof.
  The join suites establish supplied paired states. They do not establish producer integration.
  These rules change no layout, ABI, emitted instruction, arithmetic operator, or runtime check.
- **An emitter that copies a value and then clears its source needs an intermediate.** `move(lv)`
  writes into a destination the emitter cannot prove distinct from the operand (`s = move(s)`,
  `*p = move(*q)`, `v[i] = move(v[j])`), so it reads into a register or a `%tmpK` slot first and
  zeroes the operand only after. The first version copied straight to the destination and zeroed
  after, which destroyed the value in three of six build-mode/shape cells and was invisible to
  every language test, because the two aliasing forms it did not cover are not statically
  comparable. Fix the general case rather than banning the syntax that exposes it.
- **The IR emitter** (`bootstrap0/src/gen*.c`, toolchain.md 6): it runs inside that window, before
  the caller frees the analysis, because every annotation it reads points into the checker
  (`check.h`); an emitter called after the analysis is released walks freed memory. It builds its
  operand and type texts in one shared `g->scratch`, so a function that has begun writing there
  must compute a nested text first and only then compose -- `nominal_type_name` and `enum_name`
  each call `gen_symbol`,
  which clears the same buffer, and both got `%struct.` and `@.enum.` silently dropped before the
  name was built first. A construct the emitter does not lower yet reports
  `cannot generate code yet for <what>` through `gen_todo` and fails the compilation: an unfinished
  path is a diagnostic, never wrong code, and the ticket that implements it deletes its `gen_todo`.
  One unit test asserts that an unlowered construct is refused
  (`gen_module_test.c`, `a_construct_the_emitter_cannot_lower_yet_is_a_diagnostic`), so the
  ticket that lowers the construct that test names re-points it at one still unlowered rather
  than deleting it. `break` and `continue` are two targets and not one: a loop sets both, a
  `switch` only the break target, because a `break` inside a switch inside a loop exits the
  switch while a `continue` there still runs the loop's step (D7.6), so a construct that catches
  an exit (`defer`) reads both. Each target is a label and a flag saying whether it is set, and
  the flag is deliberately not a depth: nothing asks how many constructs an exit crosses, and a
  counter whose balance no test can see is a bug waiting for the pass that starts reading it.
  **Deferred statements are expanded off a scope stack, not off a counter** (`gen_stmt.c`, D7.8):
  `gen_block_scoped` pushes one entry per block being emitted, with its kind (`GEN_SCOPE_FN`,
  `GEN_SCOPE_LOOP`, `GEN_SCOPE_CASE`, `GEN_SCOPE_BLOCK`) and the index where its own `defer`
  nodes begin, and every exit walks that stack outward, copying each scope's statements into
  itself in reverse order and stopping at the scope it leaves. How many scopes an exit leaves is
  therefore read off the tree being walked. A new construct whose body is a block must open it
  with `gen_block_scoped` and the kind its exits stop at, or the deferred statements of that body
  run at the wrong place; and because the expansion duplicates code at every exit, an emitter
  test that adds one asserts `verified()`, since a missed `g->terminated` check there writes an
  instruction after a terminator that `opt -passes=verify` alone accepts.
  **`break` and `continue` inside deferred code bind to a loop or a `switch` written inside that
  deferred code**, and the same scope stack expresses that (D7.8, narrowed by T-075). The checker
  hides `ck->loops` and `ck->switches` while it checks a deferred statement and restores them
  after, so the counters see only what stands inside the deferred block; the emitter needs no
  matching state, because `run_scope_defers` already raises `defer_floor` to the current depth and
  a loop inside the deferred code pushes its scope above that floor. So the walk of an exit
  written there stops at that loop and branches to its label: for
  `while (true) { defer { while (true) { note(2); break; } note(3); } break; }` the inner `break`
  branches to the inner loop's exit block, which then calls `note(3)` and branches to the outer
  loop's exit. **The floor alone holds the case the checker refuses**, and T-075 measured it: with
  the checker's rule reverted so that `for (...) { defer { break; } }` type-checks, `gen_unwind`
  starts at a depth equal to the floor, takes no step and reports an internal error, so no branch
  to the enclosing loop's label is ever written. A second guard in `run_scope_defers` that cleared
  `has_break` and `has_continue` was written and then removed for that reason: it changed the
  message and nothing else.
  **A runtime entry point is described in two places, and one test holds them together**: the
  fort signature in `std/rt.ft`, which is what defines it, and one row per entry point in
  `bootstrap0/src/runtime_sig.c` (`RT_SIG`, indexed by the `rt_entry_t` of `runtime_sig.h`), which
  carries the mangled name of D9.7, the result form, the `noreturn` mark and the parameter forms;
  `toolchain.md` 5.1 states the same signatures in prose. The emitter writes its call sites from
  the row's name, result form and `noreturn` mark, and declares nothing, since the module holds
  the definition too (item 8). **The argument list of a call is not in the table**: each site
  builds its own operands in `gen_expr.c` and `gen_stmt.c`, so a wrong argument type or a wrong
  order is held by the per-site text assertions in `bootstrap0/test/gen*_test.c` and by nothing else
  -- every entry point a program can reach has one today, by count and not by construction, so a
  ticket that adds an argument to a call writes the assertion with it. The witness for the row is
  `bootstrap0/test/runtime_sig_test.c`'s `every_row_is_the_fort_signature_of_std_rt`, which loads
  `std/rt.ft` with the compiler's own front end (CMake passes `FORT_STD_SOURCE_DIR`) and holds each
  row against the declaration of the same name: the result form, the arity, each parameter form and
  the `noreturn` mark, plus the text the emitter writes for that definition's result, which is
  what a call site must state for the two to agree. Nothing below the compiler holds them
  together, so there is no second oracle: before a witness existed, giving `fort_rt_print_f64` an
  `i64` parameter or `fort_rt_fail_div_zero` a 64-bit line number left the whole gate green. The
  fort table (`src/fort/runtime_sig.ft`) cannot read that file -- a `test/fort` program runs in a
  temporary directory holding only itself -- so `test/fort/runtime_sig_test.ft` holds every row
  against the text of 5.1 transcribed into it. A narrow result carries its extension attribute
  on the definition *and* at the call site (`define dso_local zeroext i1 @"std.rt.str_eq"(...)`,
  `%t = call zeroext i1 @...`), which is why a form in `RT_SIG` is a type text with its attribute
  and not a type. `opt` accepts a call site whose attributes differ from the callee's and LLVM
  falls back to the callee's, so *dropping* one at a call site cannot change the assumption while
  *adding* one the definition lacks can: T-021's review dropped the `zeroext` from the
  `fort_rt_str_eq` call site and the entire language corpus stayed green, only the emitted-text
  assertion failing. The attribute is not decorative -- on a return it licenses eliding the
  `movzbl` -- and it stops being invisible the moment a lowering compares or widens the narrow
  result instead of truncating it straight to `i1`.
  **The emitter reads lvalue-ness from the checker.** `check_expr` in `src/fort/check.ft` sets
  `ANN_LVALUE` on each node whose `expr.lvalue` is true (D6.7). `fir_lower.lower_del` reads that
  bit to decide whether `del` empties its operand (D17.9); until T-255, `gen_del` of the direct
  path's `src/fort/gen_expr.ft` read it. The checker also sets `ANN_MOVE` on a call of the builtin
  `move`, and the lowering reads that bit and not the name of the callee. A new place form changes
  the checker's lvalue rule and the place forms of the lowering (`fir_lower.designates` and
  `lower_place`). The bit and the kind of the node differ for `e[i]` on an
  rvalue array `e`, for `e.f` on an rvalue `e` (D6.7) and for a function name. The emitted IR
  stays the same, because the checker refuses `del` of each of the three forms. Before T-193,
  `is_place_expr` derived the answer from the kind of the node. bootstrap-0 keeps that rule in
  `gen_expr.c`, because the freeze (section 8) leaves the C unchanged.
  Two C declarations of one name are one ELF symbol, so anything the module emits once -- an
  `extern` declaration above all -- deduplicates by the C name and never by `sym_t*`: two modules
  declaring the same function are two symbols, and a second `declare` is a redefinition `opt`
  rejects. The suites (`bootstrap0/test/gen*_test.c` over `bootstrap0/test/common/gen_helpers.h`)
  emit into a sandbox they `chdir` into, so `@.file.N` holds a bare file name and the text does not
  depend on the build directory; CMake gives every `bootstrap0/test/gen*_test.c` `FORT_OPT` and
  `FORT_IR_DIR`, and the ctest `lang` passes `--verify-ir`, so every module either suite produces is
  checked by `opt -passes=verify`. `opt` is a default, not a configure-time requirement, so each
  suite must report a missing one as a broken environment (`gen_no_verifier`, exit
  `TEST_RESULT_ERR`): a spawn that succeeds and a child that
  exits 127 otherwise reads as "the verifier rejected this IR", which blames the wrong thing.
  A duplicate the emitter stops writing is only a fix if something else refuses the program: `opt`
  rejected a `declare` beside a `define` of the entry function the compiler emitted, and dropping
  the declaration to satisfy it turned a hard compile error into a call through the declared type
  -- a SIGSEGV in the test that declared a wrong signature (T-018's review). When a tool's
  rejection is the only thing standing between a legal-looking program and wrong code, the front
  end takes the rejection over before the emitter stops producing it.
  **`opt -passes=verify` does not reject a call whose argument types disagree with its callee's
  `declare`.** Opaque pointers make a call site's type independent of its callee's, so
  `call void @"std.rt.fail_div_zero"(ptr @.file.0, i32 4, i32 14)` against a definition taking
  `(ptr, i64, i32)` verifies, links and then reads a register the caller never set (T-072's
  review). Together with the attribute fact above and the terminator fact below, this is why the
  emitter suites are a weak oracle for a *call*: they check the text they assert and nothing
  more, so a call site no suite spells is unjudged. The runtime's signatures are pinned against
  `std/rt.ft`, which defines them, and not against the IR a tool accepts.
  **`opt -passes=verify` accepts a store of a value narrower than the slot it goes into.**
  `store i1 %t2, ptr %n.1, align 1` into an `i8` local verifies and runs. A width the emitter
  computes wrongly therefore reaches the module and shows only in its text. T-078 made
  `gen_int_bits` answer 8 bits for a `bool`, which turned `cast(b, u8)` into the identity. No unit
  test and no program of the corpus saw it. The assertion that sees it now is
  `a_bool_widens_to_a_byte_because_its_value_is_one_bit` in `bootstrap0/test/gen_cast_test.c`.
  **`opt -passes=verify` does not reject an instruction after a terminator.** It splits the block,
  invents an unnamed successor which it prints as `0: ; No predecessors!`, and exits 0 -- so it
  quietly manufactures the implicit numbering D19.5 forbids rather than reporting the module that
  caused it. An emitter whose block structure is wrong therefore passes the whole gate: `gen_for`
  emitted its back edge after a `noreturn` call in a `for` header for a whole ticket while the unit
  tests, the language corpus and `--verify-ir` all stayed green. Block
  structure is asserted by the suites themselves: `verified()` in
  `bootstrap0/test/common/gen_helpers.h` runs `gen_block_terminators` before it calls `opt`, so
  every call site in every emitter suite checks contract item 10 and a new suite inherits it. Treat
  `opt` as a floor that catches type and dominance errors, never as the proof of a structural
  contract item -- and when a contract item names a tool as its check, confirm the tool actually
  rejects a violation before believing it. **No internal-ABI mutation is catchable by a language run
  test.** Flipping a `zeroext` to `signext`, dropping either extension attribute, passing an
  aggregate `byval` and dropping the `sret` of a definition each leave the whole of `test/lang`
  green (measured on all 280 tests, T-017's review): a fort program is one LLVM module, caller and
  callee are compiled together, the `-O1` of the `--cc` line inlines the mismatch away, and a
  convention both sides get wrong agrees with itself. The corpus can only see what the *program* can
  observe -- evaluation order, a callee writing to its parameter, a trap that must fire. Everything
  else is pinned by the text: the assertions and goldens in `bootstrap0/test/gen*_test.c`. A ticket
  that touches the calling convention therefore asserts the emitted text and proves the assertion by
  mutation -- change the emitter, watch that one test fail, change it back -- rather than trusting
  that a run test would have caught it. The same held for the `llvm.trap` of D19.7: the review broke
  it and all 55 suites and 280 language tests stayed green. **A C mirror is the exception, and only
  when the mirror is optimised.** T-025 re-measured the `zeroext`/`signext` swap with
  `test/lang/run/ffi/007` sending `i8`, `u8`, `i16`, `u16`, `char` and `bool` into separately
  compiled helpers: with the helper at `-O0` the whole corpus stayed green, with the helper at `-O1`
  it printed `4295032812` for `-20` and failed. An unoptimised callee spills its narrow parameter to
  a stack slot and re-narrows it from there, which repairs the caller's mistake; at `-O1` the callee
  keeps the argument under the `AssertSext`/`AssertZext` its parameter attribute states, folds the
  re-narrowing away, and the wrong extension reaches the arithmetic. That is why `link_command` in
  `test/lang/run_tests.py` passes `-O1`, matching the `-O1` the driver gives the fort side: a mirror
  built at `-O0` silently answers a weaker question than the one it was written to ask. So the
  emitted text in `bootstrap0/test/gen*_test.c` is where a convention rule is pinned *first*, and a
  `run/ffi` mirror is the second, independent witness -- not a blind one. **Under opaque pointers a
  field's type in a named struct type is observable through the offsets it moves and through one
  other window, a module-level constant initializer.** A substitution that leaves every later offset
  unchanged is invisible to every run test and to C interop, when it also keeps the struct's size
  and alignment. Two shapes do that. One is a same-size swap: `i32` for an enum, `i64` for a `ptr`.
  The other is a widening that fits in padding the field already had, such as `u16` written as `i32`
  or as `i64` in `{ u8; i32; u16; i64 }`. It is **not** invisible to `opt` when the module
  holds a constant or a global of that struct type. LLVM writes such an initializer with the named
  type. It then checks each element against the type's element. T-078 printed a `ptr` field
  as `i64`. `opt-18` answered `element 1 of struct initializer doesn't match struct element type` on
  `@"main.N" = dso_local constant %struct.main.node { i32 7, ptr @"main.N" }`. Two tests of
  `gen_global_test.c` went red through `verified()`, at `:201` and at `:313`. That window opens
  only for a struct that D7.10 gives a module-level declaration. A struct that appears in no such
  initializer keeps the weaker guarantee. The fort emitter behaves the same. A mutant of
  `gen_fir.gen_struct_type` wrote a `ptr` field as `i64`. At 49d15a5c all 97 `gen_` and `fir_`
  suites of `test/fort` stayed green, and so did 800 of the 801 language tests. clang refused
  `run/globals/008_self_reference.ft` with the same message. Its `node N = node{7, &N}` is the
  one module-level constant of the corpus whose struct type holds a pointer.
  `the_named_types_are_the_two_fixed_ones_then_every_struct` in `test/fort/gen_function_test.ft`
  now asserts a `ptr` field. So one assertion pins a field's IR type in every case:
  the string comparison on `%struct.<name> = type { ... }`. A struct that appears neither in one
  nor in a module-level constant has its field types unchecked. The bootstrap cannot *produce* that
  bug -- `gen_mem_type` is the single fort-type-to-memory-type map and a wrong mapping is wrong in
  the stores too, where it is observable -- but a second map (a packed path, an ABI-classification
  table) would break that argument, so the assertion stays and grows a field per type family.
  **A `test/lang/run/ffi/*` test with a cross-compiled C mirror is the only shape in this
  repository that can see a fort-vs-LLVM-vs-C layout disagreement.** `//! link: ffi/<file>.c`
  compiles that C file for the target with the same clang, so a helper reading a struct through
  C's `offsetof` while fort reads it through its own GEPs makes the boundary observable;
  `run/ffi/006_struct_layout.ft` is the worked example, and it closes the third edge by comparing
  the stride between two array elements -- LLVM's own size for the struct -- against C's `sizeof`.
  Struct pointers are extern-legal (D9.8), structs by value are not.
  **Pick the size classes deliberately.** Every `sret` and aggregate `memcpy` assertion used a
  struct of 8 or 16 bytes until T-019, so a mutation that dropped `sret(%T)` or shortened a
  `memcpy` only for a struct wider than two words passed the entire gate. An assertion about an
  aggregate convention covers one size unless a second size is written down.
- **A check record carries the source column of the name or token it guards, so widening a type
  spelling changes the emitted IR.** The emitter writes the position into the `fail_*` call it
  builds: `gen_var_decl` reports the overwrite check at the declaration's `name_loc`, and a
  bounds or span record carries the column of the expression it guards. A declaration retyped
  from `void*` to `void mut*` therefore moves its record four columns right, with no other
  change anywhere in the module. T-086 met it on a `fail_span` record and T-130 on a
  `fail_overwrite` one, and both worked around it in a test by putting the declared name on a
  line of its own. Measured on T-130's tree: with the comments held equal, retyping
  `std/rt.ft:629` alone makes one line of a 2629-line `.ll` differ,
  `fail_overwrite(ptr @.file.0, i32 629, i32 15)` against `(... i32 629, i32 19)`. Two
  consequences. A ticket that claims "no IR change" for a type-system edit must say "no
  instruction, no signature and no size" and then count the column constants it moved, because
  the unqualified claim is false. And a doc comment moves the **line** constant of every record
  below it in the file: six comment lines added above `alloc` moved 9 records in `std.rt`.
- **A FIR read is a load, and `fir.read.is_use` says whether it is a use** (T-233): the test of
  `p` in `check(overwrite: p)` is the one read with `is_use` false (`spec/fir.md` 14), so an
  ownership analysis such as T-182 skips it, and V12 and V13 test every read. The pointers that
  the address of `p` loads come before it as uses (T-259): skip the test and not those loads.
- **The heap proof retains multiplicity and structural separation** (T-274, D17.16).
  `memory-model.md` 2.7 defines singleton representatives, summary groups, and inductive shapes.
  One allocation-site key cannot replace several concurrent obligations with one obligation.
  Releasing a selected member cannot clear the remaining group.
  Widening retains residual obligations.
  Borrow alternatives and simultaneous allocation multiplicity are separate facts.
  `containers.pool_free` extracts `block->next`, releases `block->bytes`, releases the block, and
  advances. Its chain predicate must retain each block's separate payload allocation.
  Complete release discharges the whole proved region, including payloads and detached obligations.
  Cleanup that transfers a region preserves its obligations and source relations at new owning
  paths.
  A transferred child keeps its identity and obligation outside the old container's cleanup region.
  Retained pool views preserve their payload sources through pool growth and lose validity at
  payload release. A refilled pool cannot revive an earlier view.
  These are analysis invariants. The current compiler does not implement this complete heap proof.
  The ten H01-H10 specification traces have read evidence, not compiler-probe evidence.
  T-286 implements the heap analysis. Keep `del` shallow and add no runtime identity data.
- **Finite ownership proof preserves obligations when precision ends** (T-276, D17.18).
  `spec/fir.md` 14.1 defines the finite domain, canonical solving, and counted budgets.
  Publish numeric limits and reproduction commands before qualification. This rule ticket chooses
  no numeric production defaults. Bound symbolic keys rather than concrete loop or recursion depth.
  A residual region keeps sources, owned descendants, release history, and transferred obligations.
  Provisional recursive summaries remain private. Missing returns or effects supply no safety fact.
  Export complete sound summaries or unknown. Keep unknown caller continuations and cleanup facts.
  Function return and process termination remain separate under the global-boundary rules.
  A noreturn type proves no caller return. It proves neither abort nor completed process cleanup.
  `spec/toolchain.md` 4.2 separates validated invalidity witnesses from abstract possibility.
  D checks absolute projection depth and stores the greatest permitted depth.
  Borrow, history, alias, and relocation paths use their maximum depth.
  E requests retained cardinality. Graph rebuilds keep E unchanged and consume cumulative W.
  State history mutators count retained history occurrences before they add incoming history.
  `ownership_state.require_counts` treats its E input as incoming history.
  It then forms the full request.
  Source validity changes retain no ordered history row. They consume W without E.
  Join and widening count canonical merged histories with their own W work kinds.
  An E refusal preserves histories and obligations, then marks the result incomplete.
  W stays cumulative within one computation. Each W request item has amount one.
  A computation is the graph build, the liveness of one body, or the summary solver of one
  recursive component. A body in a recursive component keeps its own liveness computation.
  `ownership_limits.computation` gives its W bound from its FIR size (D17.18).
  A counted multi-unit mutation pays earlier W units before its final mixed request.
  The final mixed request checks all items before any counter changes.
  A refusal keeps earlier paid work. It changes no input state.
  `ownership_state.require_content` pays W, measures content, then sends one precision request.
  The caller commits at once after it. `test/fort/ownership_content_limits_test.ft` holds this.
  `ownership_state.guard_units` and `shape_units` count one prospective state for insertion, join,
  and widening. The meter keeps the greatest D, R, P, G, H, T, and E amounts.
  A count pays one W unit for each 64 rows it reads and sorts by place or key, so it compares
  O(n log n) pairs. A unit per compared pair cost 348650 W to fill a state with 100 scalars;
  now 5386.
  The H cover drops templates, which hold only proofs, and merges edges into one residual edge
  when edges alone pass H. The G cover blurs contents. Both leave content within the bound.
  The call-graph build keeps one fact chain for each body. Its whole-closure scans took 52 s on
  `src/fort/main.ft` once W scaled, and the chains take 1.1 s for the same W units.
  Keep proof events independent of rendered diagnostic limits. Suppression cannot change a verdict.
  The T-317 event store owns its meter because teardown releases caller state before event storage.
  The seven A01-A07 traces have read evidence. They do not measure current compiler acceptance.
- **Ownership flow retains FIR path alternatives** (D17.14, D17.18).
  `ownership_flow.solve` consumes verified FIR and complete supplied effects.
  It follows reachable edges and keeps null facts by value version until a write invalidates them.
  A known call invalidates its supplied value versions. An unknown fort call removes all null facts.
  The worklist keeps path alternatives up to G, then widens while it retains ownership obligations.
  At P, the pass records a lost predicate and keeps the successor.
  It reports an error only if a later operation needs the lost fact.
  Collection and call effects charge W before they change facts.
  A repeated allocation site keeps a bounded record of released allocations and their histories.
  A retained borrow prevents reuse of that site's current identity.
  A range loan starts at loan_begin and ends at loan_end.
  Direct and summarized writes to captured storage use one overlap test.
  At normal return, the pass checks complete returned-borrow relations after expanded effects.
  A proved empty reference needs no borrow. Scalar result fields add no source obligation.
  It also checks parameter obligations.
  A widened owner with no known root keeps its parameter obligation.
  A possible parameter owner gives incomplete proof unless a path witness proves the loss.
  It then ends parameter stack sources. Abort and trap paths do not require normal cleanup.
  Unknown call outcomes keep a possible return. A guarded return needs a condition proof.
  Direct and delegated W failures store the exhaustion record used by diagnostic rendering.
  Source positions keep filename presence apart from numeric module and file IDs.
  A named zero pair can coexist with an unnamed source without a false filename.
  The renderer does not call a filename resolver when the source has no filename.
  This pass does not construct source effects.
- **Local ownership effects come from verified FIR** (D17.14, D17.18, D19.8).
  `ownership_source_local` reads each verified body and feeds `ownership_flow`.
  A tracked local slot (TL) is a reference local whose storage no `addr` or array `slice`
  names. One pass over the statements marks those locals. Only direct FIR writes change a TL,
  so a TL owner keeps its allocation across any effect outside local proof. Scalars, structs
  and arrays are not TLs.
  The producer gives allocations, moves, copies, releases, reads through a TL, and the
  storage end of TL owners at dead markers and at each return.
  A fort call and a release through a pointer are `outside` effects: only TL owners, static
  storage and trusted foreign storage keep their facts. A move of a TL into other storage is
  an `escape`; its detached obligation leaves local proof. An integer cast to a reference
  names no proved source (D17.17); only `null` and `zero` constants empty a reference.
  A violation counts only when each path state that reached the operation failed it, no path
  stopped or passed an unknown fort call before it, and a witness path reaches it
  (`spec/toolchain.md` 4.2). Otherwise it is incomplete proof. The witness walk starts at the
  entry and crosses no back edge and no fort call; an extern call returns (D17.13). It knows
  constants, exact integer arithmetic, comparisons, value-keeping casts, allocations, span
  headers, slices, and the null left by a move or a `del`. A condition on a bool or integer
  parameter that no earlier condition fixed takes a value: the constant of the arm, or the
  constant of a relation or an integer next to it. The walk makes one choice at each
  condition, the target nearest to the operation, and never backtracks. A check that traps
  ends the path, except at the store of an overwrite and the access through an empty
  reference: there the trap is the event. Each block that the walk searches and each
  statement that it evaluates charges one W unit of the `local` ledger.
  `test/fort/ownership_source_local_walk_test.ft` and `_witness_test.ft` hold these rules.
  A second run without outside effects classifies the failures of a body that has no
  validated violation and no refusal. When it ends within its budget with no failure, each
  failure is call scope and local correspondence is incomplete (`call_dependent`). Any
  failure or refusal of it keeps the body in scope: a refusal explains no failure. Its first
  pass joins the paths of each block (G = 1). A joined state covers each path, so a joined
  pass with no failure shows that no path fails. Only a failed joined pass runs the exact
  pass. Both passes charge one `local_classification` ledger, and a refusal there is a budget
  error. 978 of 3312 unique bodies of the source audit run it, and none refuses. With the
  exact pass alone, 4 refused W: `copy_contents` and `scratch` in `ownership_borrows.ft`,
  `copy_state` in `ownership_ffi.ft` and `copy_view` in `ownership_globals.ft` need 107376 to
  641461 W, 1.4 to 6.3 times their bounds. Their joined passes need 5083 to 16510 W.
  Own parameters, own extern results, loads of references from other storage, owning non-TL
  locals, struct results with reference leaves and range loans also leave it incomplete.
  A W, E or V refusal of a local ledger is a budget error (fir.md 14.1).
  Scoped CI rejects each local violation and each declared body without complete local proof.
  `tools/ownership_audit.py enforce --scope local` reports the declared bodies of each root and
  each problem. A declared body has complete local correspondence.
  These legal shapes give incomplete proof in the declared scope, so scoped CI rejects them:
  a null test that the flow does not refine; a loop whose allocation site runs again while
  its earlier allocation is live (D17.16); flags that test one condition twice; a loop that
  exhausts the transfer history E (256); a path that needs a back edge, a short-circuit
  condition or a second choice of one parameter. The flow also stops a path at its first
  failure, so a later independent obligation on that path is not examined: the corpus file
  `fail/ownership/054` expects line 12, and local proof reports line 11 only.
  `bind_fresh_source` in `ownership_calls.ft` had such a shape: it allocated a borrows array
  in a loop and stored views of each. Its exact second pass failed 32 times with "old borrow
  keeps a released site identity" (D17.16). It now allocates one array outside every loop and
  gives each row a slice, so its failures are call scope.
  `test/fort/ownership_source_local_review_test.ft` pins the probes of each shape.
- **Dynamic element proof keeps guarded update states** (D17.15).
  `ownership_regions.apply` takes resolved storage paths and exhaustive supplied choices.
  Captured index versions stay separate from local slot names.
  Choice-local predicates constrain only their copied state. Guard facts own their positive keys.
  Index membership inputs use half-open ranges.
  Equality, inequality, and membership facts compare element slots through substituted storage.
  Index inequality supplies no source or allocation separation.
  A strong write changes supplied aliases and known equivalent content paths.
  A whole write removes covered child facts. Possible overlap keeps unknown child facts.
  Aggregate transfer requires a resolved type and complete reference-leaf and owner correspondence.
  It restores covered facts by decreasing projection depth. Input order does not change the result.
  Each parent follows its children. A move empties the complete selected source value.
  An owning aggregate type requires move. A false destination classification cannot permit its copy.
  Copies and moves preserve original borrow sources and byte ranges, including inline sources.
  A summary write retains old sources and guarded new child alternatives.
  Unknown child facts prevent stale singleton read proof after a possible whole write.
  Each release or move checks the selected source and destination before it changes a cloned state.
  The result keeps one state per choice up to G. Further choices use existing residual widening.
  Widening keeps possible sources, release history, and residual allocation obligations.
  Complete range cleanup needs coverage and empty nested leaves. It preserves borrowed siblings.
  Backing replacement moves element facts and owner paths while it keeps original borrow sources.
  It releases the old backing source and preserves the separate container slot source.
  First backing allocation accepts a proved empty header and creates no release of an absent owner.
  Proved unchanged backing keeps the input facts unchanged, including an empty header.
  Element partition byte windows require a proved element stride and checked multiplication.
  The source producer supplies storage, choice, and full-range correspondence.
  This module adds no source producer, LLVM instruction, or runtime field.
- **Semantic liveness separates current values from retained sources** (D17.14, D17.15).
  `ownership_liveness.solve` reads verified FIR before build-mode changes.
  Its result owns copied local bindings, ordered effect records, and point bit sets.
  Destination indices precede operand reads. Destination pointers follow operand and call effects.
  A move reads its value before emptiness. A whole proved replacement kills only its prior value.
  Projected and unproved writes preserve possible use of the containing prior value.
  Complete supplied correspondence maps indirect reads and ordered substituted call effects.
  Missing correspondence prevents a successful query.
  A supplied success establishes no source coverage.
  The backward solver unions reachable successor uses and preserves loop back edges and zero
  iterations.
  Live and dead delimit local instances. Neither marker supplies a semantic read or replacement
  write.
  Deferred reads remain before dead markers. Ending an inner instance preserves outer local uses.
  Instance boundaries remove no retained relation, referenced source obligation, or range loan.
  Validation, alias resolution, buffer growth, bit-set work, transfers, and joins charge W before
  changes.
  Array initialization, FIR read walks, and unreachable-block scans also charge their work.
  Failed results stay safe to free. Partial results provide no successful absence query.
- **Retained sources remain separate from local last use** (D17.14, D17.15).
  `ownership_borrows.solve` copies current contents, source facts, and complete supplied bindings.
  A binding separates current destination storage from original relation evidence.
  Local liveness removes no field, element, result, global, caller relation, or range loan.
  The graph follows retained storage through intermediate references with a finite visited set.
  Owning references supply storage reachability without becoming borrowed local-use roots.
  Exact updates and proved destination ends remove only their selected relations.
  Weak updates, residual partitions, and unknown retained contents cannot prove absent retention.
  Symbolic caller sources remain caller requirements. Ending retained local storage gives escape.
  W and E charges precede copies and traversal. Failed results provide no successful absence query.
  Separate allocator probes measure successful and failed result teardown.
  The producer still proves storage, reference-leaf, and source-closure correspondence.
  The module adds no driver selection, lifetime syntax, LLVM instruction, ABI field, or runtime
  field.
- **Global entry proof uses supplied canonical bindings** (D17.19).
  `ownership_globals.solve` keeps global obligations across ordinary library and callback returns.
  Selected entries require complete global leaves and exact valid caller-source maps.
  Foreign own inputs carry symbolic allocations.
  Their transfers preserve allocation and source keys.
  Local owner storage ends separately from the allocation that its value owns.
  Borrowed caller inputs keep caller-source requirements. Callback-local source ends reject escapes.
  Explicit callable cleanup empties owning globals.
  Supplied host and normal-process boundaries check final emptiness.
  The proof requires supplied cleanup order before those boundaries.
  Normal exit also requires complete closure and heap correspondence and discharged caller owners.
  A runtime name or empty global state proves no cleanup coverage.
  Copied startup and terminal records preserve supplied classifications and cleanup order.
  Generated boundaries require explicit sequence coverage.
  Abort permits live owners only after earlier operations pass.
  Trusted unresolved foreign termination proves no final boundary.
  Wrapper caller requirements identify the call, callee exit, and substituted live owner.
  Copied call summaries preserve effect order and exact source keys.
  Ordinary selected reads check source validity before a function return.
  Retained global borrows require live sources at that return.
  Current heap reference edges retain field storage through owned and borrowed paths.
  Borrowed reachability creates no owner obligation. Empty current fields stop reachability.
  Missing or residual reachable storage facts give incomplete proof.
  Source ends and ordinary returns check current retained heap storage.
  Call entry does not read fields that the body can replace.
  Ordered reads still reject released sources before a later field replacement.
  Scalar global reads require exact checked type, canonical layout, contents, and effect facts.
  A contents tag or nonowning binding alone proves no scalar read.
  Startup accepts only read, write, transfer, release, and storage-end effects with event keys.
  An owner move empties its old owned graph edge in place (`spec/memory-model.md` 2.7).
  The emptied edge keeps its key, parent, place, range, and old target.
  Empty contents make it a potential edge, so a later access still finds the parent.
  A move into a potential edge retargets it only after current contents and allocation facts
  prove the new target. Neither the move out nor the move back adds a graph edge.
  A supplied state without the emptied edge makes the field a plain place.
  An owner moved back there has no graph ancestor, so an entry return refuses it.
  Field reads, writes, transfers, and releases check containing storage before any shortcut.
  The check reads source storage first, then distinct destination storage.
  A proved released parent gives invalid use only for an unconditional effect.
  A conditional effect gives incomplete proof with abstract possibility evidence.
  Owned graph ancestors can keep descendant allocations at a persistent global owner.
  Both ancestry and shallow-release walks require current contents and canonical owner
  correspondence.
  Empty current fields stop potential owned edges. A potential edge alone proves no current owner.
  Moves check empty destination correspondence before changing the allocation owner.
  A potential destination target can retain an unrelated ownership obligation.
  Supplied graph coverage and canonical global roots precede an empty-edge traversal result.
  A proved persistent global owner permits an unrelated local source end.
  That source end still checks current retained heap borrows and rejects an escaped stack source.
  Residual or missing ancestry proves no persistence.
  Separate alternative states start after startup and retain their own fact storage.
  Startup-only releases reserve node traversal storage before an effect runs.
  State equality reserves the complete all-pairs and nested-loop work bound before comparison.
  The cost calculation also consumes counted work. An unrepresentable price gives work refusal.
  Different alternative states need a proved path join before one persistent result represents them.
  Staged symbolic bindings and callback records survive input release.
  They support prior-result reuse.
  Failed copies, charges, and effects preserve input and the prior result.
  Null consumer context, services, or request pointers give incomplete proof.
  A null request has zero operation and source keys. It records no event.
  Missing callbacks or event bindings establish no boundary and preserve retained storage.
  The API wrappers still require valid services and request pointers.
  Legacy unselected copies establish no entry or final boundary.
  This consumer proves no actual source producer, external host call order, or generated cleanup.
  It changes no program ABI, runtime field, or driver selection.
- **The FIR lowering takes each type from the checker and derives none** (T-236):
  `fir_lower.lower_function` gives a temporary the type that the checker gave its node. So a `?:`
  whose arms lend takes the lent type and a copy fills it, and an owning `?:` or `cast` is read by
  `move _t`. A construct that the lowering does not lower is a `fir_lower.report`, never a
  panic, and `fort --fir` prints it as a comment. Each test of the lowering runs the verifier on
  its result (`test/fort/support/lower_env.ft`), and `fort --fir` runs `fir_verify.verify` before
  it prints. A new construct needs both: a text in a `test/fort/fir_lower_*_test.ft` and a clean
  `fort --fir` over the run corpus, which T-236 measured on 450 programs.
  **An aggregate operand is a place** (T-239): `fir_lower.lower_place` puts an aggregate rvalue in a
  temporary first, and a call argument of an aggregate type is never a constant (rule V7). The
  materialization rule of `spec/fir.md` 9.4 holds each `copy p` and `move p` before a later writer,
  except fixed storage whose base is a temporary: the lowering takes the address of no temporary's
  own storage, so no writer reaches it. A held copy takes `fir_lower.lent_type`, which drops `own`
  at the top level and in the elements of a fixed array; a held copy that keeps `own` breaks rule
  V6. A place is also read late: `P->arr[swap()]` read `P` after `swap()` until review round 1 of
  T-239 found it, and the direct path read it before. So an index or a span expression whose index
  or bound holds a writer holds `_a = addr(base)` first when the base reads memory to find its
  storage. The target of `lv = e` is held under a wider condition: any target that is no fixed
  storage, a global included (`spec/fir.md` 9.3), while an index or a span holds only a base that
  reads memory (9.4). The header of a span and the element are still read where they are used, as
  the direct path read them. A check stands where the direct path reported it, because the runtime
  prints that location: the `check(user)` of `assert` and the `fail(panic)` of `panic` stand at the
  name of the builtin and not at its `(`. T-239 moved `fort --fir` over the 450 run programs from
  1795 lowered functions to 30161, with no panic.
  **An exit finds its target on a stack of scopes, and never on a counter** (T-242):
  `fir_lower.lower_scope` pushes one scope for each block, of kind `fn`, `loop`, `case` or `block`,
  and `lower_exit` walks the stack from the innermost scope. `break` stops at the first `loop` or
  `case` scope, and `continue` at the first `loop` scope, so a `continue` in a clause reaches the
  loop around the `switch` (D7.6). A `loop` or `case` scope that stays open after its block turns
  a later `break` into a jump to the wrong block. The verifier does not see it, because that block
  is a block of the function. A text test sees it: `a_clause_is_a_case_scope_inside_a_loop` holds
  a `break` after a `switch`, and a mutant that never closes a scope fails it. A range `for` over
  an owning collection whose place reads memory (`h->items`) held `_a = addr(p)` before the
  loop in T-242. T-263 instead holds a lent span or string header before the loop (D7.5, D17.10).
  An owning fixed array still holds its address when its place reads memory. T-242 moved
  `fort --fir` over the run programs from 30161 lowered functions to 37947, with no panic. The
  first program that rule V9 refused was T-210's: a `break` left a `switch` that D8.4 then
  counted as terminating. Since T-210 the checker refuses that program with `missing return`,
  and `fir_lower_switch_test.ft` holds both the refusal and a clause whose `break` targets an
  inner loop, which lowers with no V9 violation.
  **Each exit and each fall-off unwinds the same stack** (T-244). A scope records where its
  deferred statements and its named locals begin in two vectors of the lowerer, and its pop
  truncates both, so no exit expands a `defer` of a closed scope or ends a local of one.
  `fir_lower.unwind` lowers the deferred statements of each scope it leaves, the last one first,
  then writes a `dead` for each local of that scope. A local joins its scope at its `live`, so
  an exit ends no local declared after it. An expansion raises the floor to the depth of the
  stack; checked programs never meet the floor, so `fir_lower_expand_panic_test.ft` wraps a
  checked `break` in a `defer` node of its own to hold it. The local of the init of a `for`
  belongs to a scope of kind `block` around the loop, and the variable of a range `for` to the
  scope of its body. The store of a scalar `lv = e` stands at the `=`, as spec/fir.md 13 stores
  `*p = 7`. T-244 moved `fort --fir` over the run programs from 37947 lowered functions to
  41051 and refused none, and `src/fort/main.ft` from 1626 and 144 refused to 1790 and none. A
  function is refused only when it needs a runtime entry that its closure lacks, as the sandbox
  of a test does: a print or a string equality.
- **The build-mode pass changes only what `spec/fir.md` 11 lists** (T-247).
  `fir_passes.build_mode` replaces each check that the mode removes by `goto`, writes the mask of
  a shift check at the end of its block, and replaces each read of the count that the mask
  reaches. A forward dataflow over the masks, joined by intersection, finds those reads. The pass
  then removes the pure assignments to temporaries that nothing uses until nothing changes, folds
  the store after a removed overwrite check, and merges each continuation into its check block.
  A use is a read of `fir.stmt_read` or `fir.term_read`, or an `addr` or a `slice` of the storage
  of the local. An `addr` is no read of its place, so a count of the reads alone removes a
  temporary that a pointer reads later (`test/fir/build_mode/020_addr_keeps.fir`). The width of
  the mask comes from the type name that the shift check reports, and the pass panics for a name
  that is no integer type. The pass keeps the `let` of each local, and spec/fir.md 12.1 gives a
  temporary that nothing names no storage: under `--release`, 1251 such temporaries stand in 548
  functions of `main.ft`, and none after the lowering (T-247 review, round 1). The pass looks up
  the masks of a local in a table, because a scan of every mask for each statement took 1.98 s
  for 8000 shifts. The driver runs the verifier before and after the pass. T-247 ran
  `--fir-after=build-mode` over the 41051 functions of the run corpus, the 2560 of
  `test/lang/programs`, the 1820 of `src/fort/main.ft` and the 306 of `src/lsp/main.ft` in the
  four modes, and the verifier accepted each result. On `main.ft`, `--release` removes 1197
  checks and `--no-bounds-check` 730, and the default mode changes no function.
- **The translator gives each local its storage in one scan, before it writes text** (T-249).
  `fir_llvm.classify` reads the blocks in number order. A scalar temporary is a register when
  one statement assigns it whole and every read has the statement index of that statement. Two
  more conditions keep a register sound, and the lowering never breaks them: no read comes
  before the assignment in the order of translation, and no `addr` takes its storage. Every
  other temporary that an item names is a `%tmp` slot, and a temporary that nothing names has
  no storage. A slot is memory, so it is correct where a register has no value yet. The
  translator reads every operand before it computes the address of the destination, so a place
  operand is loaded once for each rvalue that names it (spec/fir.md 12.5). A `move` or a `del`
  of fixed storage of a temporary zeroes nothing, and an aggregate `move` of any other place
  goes through a new `%tmp` intermediate. `fir_llvm.cast_value` and `fir_llvm.overflow_call`
  write the cast and the overflow intrinsic; until T-255 they were `gen_expr.gen_cast_value` and
  `gen_overflow_call`, which both paths called. T-249 translated the 1822
  functions of `src/fort/main.ft` and 43793 functions of `test/lang/run` and
  `test/lang/programs` in the four modes, with a stand-in terminator for each block, and
  `opt-18 -passes=verify` accepted each module. The stand-ins read only the operand of a
  `switch` and the condition of a `check`, and a call is `poison`, so that check does not hold
  the dominance of the reads of a call argument, of a reported operand of a `check` or a `fail`,
  or of the place of `check(overwrite)`. The review of T-249 probed those reads apart and found
  none out of dominance. Under `--release`, 1251 temporaries of `main.ft` have no storage, which
  is the count of T-247.
- **One signature map writes every definition, extern declaration and call** (T-251).
  `gen.signature_of` maps a fort function type to the result type and its extension attribute,
  the `sret` type, each parameter with its attribute, and the variadic flag of an `extern fn`.
  `gen.gen_define`, `gen_data.emit_extern`, `gen.gen_main` and `fir_llvm.call_of` all read it
  (and `gen_expr.gen_call` of the direct path did, until T-255 deleted it), and
  `gen.gen_call_sig` writes each `call`. A fixed argument takes the attribute of its parameter,
  and an argument of the variadic tail none. The move did
  not change the direct path's text: `tools/ir_snapshot.sh` of `main` and of the branch gave an
  empty `diff -r` over 427 run tests in three modes. The calls that the translator makes itself,
  `std.rt.alloc`, `std.rt.free` and the failure entries, still come from the rows of
  `runtime_sig`, as the direct path's did. `gen.function_begin`, `gen.function_end` and
  `gen.gen_main` stand in `gen`, because `fir_llvm` must not import the module that writes a
  program: `gen_fir` calls the translator, and a circular import is an error (D9.5).
  **The translator numbers the failure blocks before it writes a block** (T-251).
  `fir_llvm.open` gives the `k`-th block that ends in a `check` or holds only `fail`, in block
  order, the label `%L<B + k>`, and a branch to a block that holds only `fail` names that label.
  `bb0` is never such a block, because LLVM needs an entry block, so a `fail` there stands in
  place. A `check` reads its condition and its reported operands in its own block, before the
  `br`, so each dominates the failure block; a block that holds only `fail` reads them inside the
  failure block, which is the one case where a failure block holds more than its call. An
  aggregate argument passes the storage of a temporary as it is, and copies any other place into
  a new `%tmp` slot, the caller-made copy, which a `move` then zeroes. The `sret` pointer is the
  address of the destination, which the translator computes after the arguments. T-251 built a
  compiler whose `gen_function` emits through FIR (`.tickets/evidence/T251/probe_patch.py`). It
  reached a fixpoint: the probe built by the direct path, the probe built by itself and the probe
  built by that one write one module for the compiler source. That module is not the direct
  path's: `diff` of the two gives 261291 lines on Linux. The probe passed the 702 language tests
  with `--verify-ir` and the 255 module tests of `test/fort`.
- **The compiler writes each function through FIR** (T-253, T-255, `spec/fir.md` 3, 9.8).
  `gen_fir.emit_function` counts the definition in `g->functions`, lowers the function, runs the
  verifier, the build-mode pass and the verifier again (`gen_fir.check_fir`), and translates a FIR
  module of that one function, whose extern list gives its `declare` lines; it counts
  `g->lowered`, which `--fir-stats` prints. Four cases write no text, and each is a compile
  error (`gen.gen_error`) that fails the compilation (spec/fir.md 9.8). The first is a
  construct that the lowering does not lower, at that node: `cannot lower `<f>` to FIR: the
  lowering does not support the node `<kind>``; no checked program meets it, so
  `gen_fir_test.ft` makes a statement a struct declaration by hand. The second is a print or a
  string equality whose `std.rt` function the closure lacks (`fir_lower.report.entry`, T-244), at
  that node: `the runtime `std.rt` has no entry `<name>`, which `<f>` needs`. The third is a
  print or a string equality whose `std.rt` function the closure declares with another
  signature, at that node: `the runtime `std.rt` has no entry `<name>` with the parameters that
  `<f>` needs`. `fir_lower.print_fits` and `str_eq_fits` hold the signature of `spec/toolchain.md`
  5.1, and they also refuse a `mut` or `own` mark on a pointer parameter and a `noreturn`
  function, which V4, V6 or V8 would refuse with a panic; `fir_lower.report.signature` tells the
  third case from the second. The fourth is a
  `check` or `fail` that the build-mode pass of the mode keeps and whose entry the closure lacks
  or declares with other parameters (`fir_verify.runtime_holds`, `entry_fits`), at the name of
  the function. `fir_verify.excuse_mode` marks the kinds that the mode removes, so that V7 and
  `runtime_holds` accept a missing entry of such a kind on a `check`, never on a `fail`: under
  `--release` an overflow check needs no `fail_overflow`, because the pass removes it before the
  translator runs. The verifier after the pass reads `fir_verify.unexcused`, so a check that the
  pass keeps needs its entry (review round 2 of T-255: a pass that kept shift checks under
  `--release` passed the verifier and called a missing `fail_shift`). Only a test with a stub
  `std.rt`, or an old `--std-dir`, meets the second, the third or the fourth. The compiler tests
  no other `std.rt` function: `alloc`, `free`, `args_init`, `args` and `flush_all` are assumed, so
  `del(p)` under `--release` with an empty `std.rt` writes a call to an undefined `std.rt.free`.
  Until T-255 the direct path (`gen_stmt.ft`, `gen_expr.ft`) wrote both. On T-253's tree and on
  414d8b7c, `--fir-stats` gave `lowered N of N` over `test/lang`, `src/fort`, `src/lsp` and `std`
  in the three modes, so no program of the corpus took the direct path, and T-255's
  `tools/ir_snapshot.sh` of 414d8b7c and of its branch gave an empty `diff -r` over 431 run tests
  in three modes. So a test whose sandbox has an empty `std.rt` and whose program checks or prints
  needs a stub runtime: `gen_env.open_body`, `open_src` and `open_module_rt` write
  `lower_env.RUNTIME`, `lifetime.write_check_runtime` does for the emitter probe, and the stub of
  `comptime_test.ft` holds the two print functions. A violation of a rule of the verifier ends
  the compiler with a panic (`gen_fir.emit_fir` runs `check_fir` before the translator), so
  T-210's program was an internal error of `fort -S` until T-210 made the checker refuse it. No
  known source program then breaks V9, so `gen_fir_v9_panic_test.ft` gives a FIR function built by
  hand to `emit_fir`, and `driver_test.ft` gives it to `driver.report_function`, the path of
  `--fir-verify-report`. A designated literal is `aggregate zeroed`, which the translator writes
  as the direct path did: a memset of the whole value, the padding included, then the named fields
  (`run/structs/012_designated_padding`, review round 2 of T-253). A member of a literal that the
  lowering builds in a `%tmp` slot of a type with padding, and the operand of a cast, start as
  `const zero` (`fir_lower.built_operand`), so that the copy of the slot writes zero padding into
  a designated literal. A first fix zeroed them in a designated literal alone, and `deep()` of
  `run/structs/013_designated_member_padding` printed stale padding at `-O0`: a callee's positional
  literal copied a stale `%tmp` slot into the zeroed field that the caller passed as its `sret`
  pointer (review round 3 of T-253). The `declare` lines follow the first call of each extern in
  the text of the definitions, because the FIR module of a function lists its externs in block
  order (`spec/fir.md` 12.5). The FIR path costs time: in three runs in the VM on T-253's tree,
  `fort -S src/fort/main.ft` took 3.2 s to 3.4 s through FIR and 1.4 s to 1.6 s through the
  direct path, and `fort --fir` alone took 2.1 s to 2.2 s, so the lowering and the verifier take
  most of the difference.
- **Loan statements emit no LLVM** (D19.8, `spec/fir.md` 12).
  The translator ignores both statement kinds before it changes pending source notes.
  The LLVM equality test removes the statements and compares actual emitted text in four modes.
  Build-mode passes keep statement order and verify V14 again.
- **Direct-call model storage keeps ordered records.**
  `ownership_summary_model.clone` copies temporary nested metadata.
  Checked types and source filenames stay alive through its last observer.
  `ownership_calls.bind` binds caller sources, allocations, captured values, globals, and results.
  It keeps by-value slots separate from caller storage.
  It preserves known caller facts and compares substituted storage, not formal identity.
  It evaluates no body and applies no effect.
  A bound frame keeps an unknown caller outcome and requires cleanup.
  Each owner charges E with the count of the relations that it retains now, not with an
  increment, so a clone of the same records adds no unit (`spec/fir.md` 14.1).
  Each record kind keeps strictly increasing keys, so a lookup is a binary search. A case check
  costs its length and the order count, not their product.
  `ownership_summary_body.extract` derives linear effects from checked and verified FIR.
  It separates by-value storage from the parameter's designated source.
  A local whose address the body takes keeps no scalar fact, so a store through that address
  leaves no stale index. A cast keeps an exact value only when its type holds the value (D17.17).
  It keeps unsupported calls and control flow incomplete.
  Owning aggregate values and aggregate ownership returns also stay incomplete.
  Each reference load has one internal source, allocation, and value-version token.
  Model validation requires its outer extent check and ordered definition before use.
  A parameter requirement retains its call-entry value.
  It checks current source and allocation validity.
  The consumer loads the current complete reference from canonical caller storage at that operation.
  It keeps the inner source, allocation, value version, byte window, and alignment.
  Outer storage liveness cannot prove inner source liveness.
  A load from storage of the body, and a nested read through a loaded reference, stay
  unsupported.
  A prior replacement changes a later load. A prior load keeps its old source.
  Loaded bindings use reserved frame capacity. They consume no argument index.
  Shallow parent release requires each inline owned child empty at that operation.
  A bounded checked-type walk does not follow a child pointer.
  Residual child obligations, graph edges, or unproved shapes keep the result incomplete.
  The consumer copies borrowed graph answers before the next callback.
  A selected address keeps its designated source and checked byte window.
  The producer retains a generated address temporary's field path.
  Each selected owner leaf has one private direct flow slot.
  The exported effect keeps its canonical field or fixed-index path.
  The existing projected-place consumer applies that effect to the caller state.
  An indirect parameter address does not designate the parameter slot.
  An output requirement checks the selected old owner before replacement.
  A prior release supplies an ordered valid-owner requirement instead of an empty-entry requirement.
  Distinct selected leaves retain distinct obligations.
  A selected address cannot widen its original argument window.
  Binding can refuse a residual alias before application copies caller state.
  That refusal preserves the borrowed input. It does not supply a populated result state.
  `ownership_summary.infer` copies normal exits after defers and parameter boundary checks.
  It publishes one proved return with ordered effects and caller requirements.
  An intrinsic parameter escape remains a callee failure when a definite normal-return suffix
  retains it.
  A later projected clear keeps the current incomplete summary and supplies no invalid-use witness.
  A conditional prefix supplies no unconditional witness unless its condition has a proved true
  value.
  Typed local reads require the original source window and actual type alignment.
  Reference inspection does not prove a designated-storage read.
  Inferred extent requirements guard private symbolic caller reads.
  Their exported typed effect still checks the actual caller representation.
  `ownership_calls.apply` applies the ordered case to a separate caller-state owner.
  An extent requirement keeps its original formal source and substituted caller value separate.
  Two values that share an allocation keep their separate access windows.
  Foreign extent trust does not override known negative bounds or known allocation release.
  Each fresh call allocation has a separate obligation. A zero length preserves that obligation.
  Failure preserves the borrowed caller input and a possible return with cleanup.
  The client supplies one meter for each computation: the summary solver of one body, or the
  flow of one caller with its call applications. Its W bound is `ownership_limits.computation`
  of that body's FIR size. The function E count continues from the body through each exit.
  These modules do not select a new compiler driver. (D17.18)
  Measured on a body of N scalar declarations and one read: at N = 49 the summary completes
  under the production bounds and uses 29461 of its 75072 W units. At N = 50 the E bound of
  256 refuses first (`test/fort/ownership_calls_limits_test.ft`).
- **A body applies the summary of each direct call at the gap of the call** (D17.18).
  Extraction records each call site, its event key, and fresh keys for the callee allocation
  templates. A call depth creates no key.
  The flow `direct_call` step runs `ownership_summary.call_step`. It binds the callee roots to the
  current path facts and runs `ownership_calls.apply`.
  An output root binds to a whole caller root only. An argument such as `&t->b` designates a
  field, so its type differs from the root type, and the call stays unknown.
  A proved abort ends the path and records an abort exit. A missing binding, an incomplete callee
  summary, or an incomplete application keeps an unknown continuation. Flow keeps the obligations,
  the call result becomes unknown, and the published summary stays incomplete.
  A refused charge in an application stops the computation with that refusal. The inference sees
  each charge through a watch, so no later step changes the reason.
  The summary exports the callee requirements on its own caller sources, with one read at the
  call, and a reference result that designates its own caller sources. Any other caller-visible
  callee effect, and an owning argument move, keep the summary incomplete.
  So does a callee requirement on a source that the body loaded from caller storage, because no
  formal of the body names that source.
  Only the effects of the applied case run. A requirement after an abort cannot fail the call.
  The handler checks no collection loan, so a direct call inside an open loan fails before it.
  A callee summary without a proof keeps the call unknown. Its body reported its own error, so
  the call does not fail the caller for it. A direct fort call without a summary, such as a
  call of a missing body, is an unresolved step with the same continuation. An extern
  call keeps the body unsupported with the reason `missing_ffi`. An indirect call without graph
  targets keeps it unsupported with `unknown_effect`.
- **A call of a function value applies the summary of each fort target** (D17.13, D17.18).
  `ownership_recursive.solve` gives each graph call one `call_targets` record and binds each
  explicit fort target to its published summary. Extraction finds the record of a call by a
  binary search, and a direct call without a record finds its callee by symbol. One explicit
  target and no residual target is the call of that body, with definite caller errors.
  With several targets, each target applies its possible cases. A failure of one target is
  incomplete proof with abstract-possibility evidence, because a target set entry is no witness
  (`spec/fir.md` 14.1, A04). Such a call exports no requirement and no caller-visible effect.
  It keeps a continuation only when each target can return, and its path ends when no target
  can return. Otherwise it stays unresolved.
  A residual fort target keeps an unresolved step. A foreign target or a residual foreign target
  keeps the body unsupported with `missing_ffi`. A residual fort target at the same call keeps
  `unknown_effect`, so foreign trust never covers an unknown fort target.
  Each possible abort case gives one abort exit at the call, and two abort exits with one guard
  give one case. The solver solves the components callee first, one computation each.
- **A recursive component publishes its summaries at a covered fixed point** (D17.18).
  Each member starts at bottom: a complete summary with no case and no residual effect. A
  member call applies the possible cases of the provisional summary. A provisional target
  without a possible case ends its path in that round and records no exit. Rounds infer the
  members in key order with the latest summaries and record no event. A round that changes no
  summary, compared record by record (`ownership_summary_model.equal`), is a fixed point,
  because inference is deterministic. One publication round then infers each member with the
  real event service.
  In that round each member call needs possible cases that cover each captured symbolic bool
  value (`ownership_calls.covered`, at most 6 values). A vacuous or uncovered member call, or a
  publication that differs from the fixed point, withdraws the proof of each member that has
  one. So no complete summary misses an input, and each prefix of a divergent recursion is a
  prefix of a covered case. All rounds of one component are one computation with the W bound
  of the component FIR size. A refused W charge stops it for each member. The stop records one
  `work_failure` event when no event of the computation names the W exhaustion, as in a round
  that records no event (`spec/fir.md` 14.1). An event store keeps one event for each key, so
  the stop event names the first member and the ordinal U32_MAX; a body key stops before that
  ordinal (`ownership_summary_body.next`). Each inference gets the services of its round
  through a copy of its input, so no input names the services of an ended computation.
  A withdrawal records no event. A divergent prefix that reads a released view (`PREFIX` in
  `test/fort/ownership_recursive_test.ft`) gets no error of its own: the published summary
  names the uncovered call with `unknown_effect`, and a client that reads it must render it.
  A guard with two atoms that need two values of one caller value fails at the call. Two abort
  exits with one guard at one call are one case, so the cases at one position stop growing.
  A call of a body to itself applies a copy of its summary whose keys name the function
  instance `FRAME_INSTANCE` (`ownership_summary_model.rename`). Its formals, locals, values,
  and relations then stay apart from the caller keys, as for a call of another body. Without
  the copy, a second lookup of a substituted key found a formal of the same function: a call
  `sw(q, p, true)` mapped the argument `q` back to `p`. Operation positions keep the callee
  function, so diagnostics name the callee body.
  Measured: a recursive pair of 9 FIR units uses 17224 W in 3 rounds and one publication
  round. Six captured values over 14 cases use 84116 W in 3 rounds and one publication round,
  above their bound of 65536 + 64 S (`test/fort/ownership_recursive_limits_test.ft`). Each
  round infers each member again, and the bound does not grow with the rounds. A self call
  `walk` after k scalar declarations uses 15990, 30519 and 48070 W for k = 1, 2 and 3, in 3
  rounds and one publication round. That is 4.7 to 5.7 times the 3384, 5708 and 8486 W of
  the same body that calls another body. At k = 4 the self call reaches its bound of 67520
  and publishes `work_limit`; the plain body uses 11683.
- **A branch on a captured formal gives guarded cases** (D17.18).
  Extraction walks each reached block of a forward tree and keeps the origins and scalars at each
  block entry. A back edge or a second parent keeps the body unsupported, so an `if` whose
  branches meet again before an exit stays incomplete.
  A bool switch on a scalar formal value adds a flow value edge. Flow records an equality atom on
  that path and does not reach a path that contradicts an atom.
  Each exit gives one case. Its guard holds the formal value atoms of its path. It keeps only the
  effects and requirements of its path, in FIR order.
  At a call, an exact argument or a caller atom decides a case guard. A symbolic formal argument
  keeps the case possible and adds its atom to the continuation. An atom with no capture keeps
  the call unresolved. Each guard is decided before any case runs. When each guard fails, the
  call also stays unresolved. A formal whose address the body takes has no symbol, so its branch
  adds no value edge and its argument gives no capture.
  `ownership_calls.contradicts` is the one refutation rule: an exact fact of the formal of an
  equality atom refutes the atom when its value differs. Extraction applies it to the call
  operand (`ownership_calls.refuted`) and removes each refuted case from the origin of a
  reference call result (`ownership_summary_body.call_origin`). `bind_call` copies the same
  operand into the capture of the formal through `scalar_formal` and `formal_value`, and
  `case_answer` applies the rule to that capture in its atom loop. So extraction never removes
  a case that flow applies, and flow reads no guard twice. The normal cases that stay must
  give one argument source over each target. A caller atom, a negative atom, or a guard
  without a record removes no case there, so two sources keep the origin unknown and a later
  read incomplete. When the operands refute each normal case, flow applies no normal case at
  the call: it ends the path at an abort case, it ends the path for the round at a
  provisional target, or it keeps a published call unresolved with an unknown result, whose
  read is incomplete proof. The origin then reads each normal case, so the read does not stop
  extraction with `missing_path`. Before, each case stayed, so `second(r, a, false)` with two
  different views left its caller with `missing_path`. `ownership_calls_guards_test.ft`
  (`guarded_origins`, `abort_origins`, `refuted_origin`, `unread_guard`),
  `ownership_recursive_test.ft` (`refuted_round`), `ownership_indirect_test.ft` (R06) and
  `ownership_calls_coverage_test.ft` (`refuted_operands`) hold these rules.
  An abort case records an abort exit. Normal cases join into one continuation. When the normal
  cases of a target cover each captured value (`ownership_calls.covered`), their case atoms leave
  the continuations before the join, so the join loses no atom. Result rows that differ only in
  their keys and value versions then become one row, and the summary exports that one value.
  Normal cases that leave a value to an abort keep their atoms, and the join of two different
  atoms keeps the summary incomplete. Two result values also keep it incomplete.
  A store that a call exports attaches to each exit after the call, so it cannot name one case.
  The store list of each applied case must be a prefix of one list, and each normal case must
  export all of it; otherwise the summary stays incomplete. A store in one normal case only, or
  in an abort case only, is the refused form; a store before or after a trap is not.
  `ownership_calls_guards_test.ft` (`joined_stores`) holds each form.
  A projected effect on the caller storage of a symbolic caller stays incomplete in this unit.
- **A borrow stored into caller storage is retained past the return** (D17.14).
  A copy of a reference local, or a null store, into caller storage is a store, at a field or at
  the root. Flow copies it into a private slot, which checks the stored value. A root written
  through is an output root, so a call binds the storage that it designates.
  The summary exports a callee copy of one of its parameters into caller storage as a copy from
  the argument local. Any other callee store into caller storage keeps the summary incomplete.
  At each normal return, the last store into each caller storage path is a retained relation.
  A later store at that path, such as a null store, ends it. A retained source that was
  released or ended fails the body at its return with an invalid use.
- **An aggregate result binds to the actual caller destination** (D17.15).
  A body may build a flat struct literal, move a whole struct out of caller storage, or move a
  holding local into `_0`. Each leaf effect goes into the summary. A literal written into `_0`
  requires each owning destination leaf to be empty, and each caller checks that after the
  operand effects. Flow models the owner and borrow leaves of a literal in private slots.
  A holding local must reach `_0` on each returning path. A nested struct, an owning aggregate
  parameter, and a returned aggregate parameter stay unsupported.
  FIR can write a call result through a pointer local, as in `(*_k) = call`. Then the callee
  result `_0` binds to the storage that `_k` designates. Callers replay aggregate effects with
  `ownership_places`, which refuses a write over a live owner. A deferred refill of the
  destination therefore fails the final result write (`spec/memory-model.md` P17).
  At each normal return a borrowed result leaf must still be live, after deferred effects.
  The extractor does not lower global places, so a test binds a result to caller storage with
  a static source to model a global destination.
- **A view loaded from an owner field ends when the same body releases that field** (D17.14).
  Flow models a caller owner field as a private empty slot, so its release does not end the
  loaded source. `ownership_summary.publish` checks the exit sequence instead. A later
  requirement on that view is a use after release. A returned borrow of it, for example after
  a deferred release, outlives its source. The body fails at that operation with an invalid
  use. A read after an aborting call is not on the exit path, so it is not checked.

## 7. The runtime and the standard library

- **Extern declarations keep allocator failure calls** (T-149).
  Apple clang `-O1` removes a `calloc` and matching `free` when the caller reads
  no storage, even with `#3 nobuiltin` at the call site. The emitter adds
  `nobuiltin` to each extern declaration on both targets.
  Check the optimized entry function for `call ptr @calloc` in the OOM fixtures.

- **Mac std.net uses Darwin's 16-byte IPv4 address** (T-145).
  `test/lang/ffi/sockaddr.c` holds the length, family, port and address offsets at 0, 1, 2 and 4.
  Set the length byte to 16 before bind and connect.
  Use Darwin SOL_SOCKET 65535 and SO_REUSEADDR 4.
  Mac `std.libc` declares seven socket calls with a 4-byte `socklen_t`.
  Mac `std.net` uses `libc.errno_slot()` for its two errno writes.
  Linux `std/linux/net.ft` and `std/linux/libc.ft` keep their Linux layout and calls.

- **A system call added to a print path must give errno back.** `sys.errno()` hands a program the
  errno of its own last library call (`stdlib.md` 2.4), and the print family runs between the two:
  the `isatty` of D11.5 fails with `ENOTTY` on every pipe, so the first `print` after a failed
  `open` replaced the program's `ENOENT` with it and `run/stdlib/053_io_open_errors.ft` printed
  `-1 false`. That test found it because the arguments of one `println` are evaluated left to
  right (D11.7), which puts the buffer's creation before the `sys.errno()` beside it. The runtime
  saves and restores errno around the call; anything else it grows on that path does the same.
- **The runtime is `std.rt` and every closure holds it** (D9.10, D13.1, T-091). Three consequences
  a ticket meets before it meets anything else. A module set with a standard library directory
  loads `<std-dir>/rt.ft` as a root **before** the entry file, so `std.libc` rides in behind it
  and D9.8 holds a program's own `extern` declaration of a libc symbol against the library's,
  `own` included: `run/ownership/015` and `019` gained the `own` `std.libc` carries. **So a
  declaration added to `std.libc` binds every program in the repository**, and the cost of a wrong
  signature is paid by all of them at once: T-045 added `qsort` there, and a program that
  redeclares it must now write the same parameter types, `fn (void*, void*) i32` included.
  `grep -rn 'extern fn void qsort' .` finds one in each `std/<target>/libc.ft` and one in
  `spec/module-system.md` 8.5, which spells it out as its callback example. **Retyping one costs
  the same**, and the bill is the programs that redeclare it, not the ones that call it: T-086
  gave `read`, `write` and the four `<string.h>` byte functions `u8*` and `u8 mut*` and made
  `malloc` return `void mut* own`, and 9 corpus tests had to redeclare their `extern` while 7
  only dropped a cast. `spec/decisions.md` D9.8 carried a `void* buf` example of `write` that a
  program could then no longer copy, so a signature in the specification is part of that bill.
  A set with no
  such directory loads no runtime, which is what every in-process unit suite is, so a suite that
  drives the whole driver writes an **empty** `std/rt.ft` in its sandbox
  (`bootstrap0/test/common/driver_helpers.h`, `bootstrap0/test/common/modules_helpers.h`,
  `test/fort/support/modules_env.ft` and the three `test/fort/driver*` suites) rather than the real
  one: the driver needs a file that parses and the assertions stay short. And the `"files"` of D20.2
  and the `"symbols"` of D20.3 now hold the library's records too, whose file names are the
  `--std-dir` the run was given, so `run_tests.py`'s golden index compares the records of the test's
  own directory alone (`index_of_the_test`) -- a byte-for-byte golden of the whole index would name
  a build directory and could not be checked out on another machine.
- **Writing a library module against C** (`stdlib.md` 1.4, D13.4): a span is not a pointer, so
  `cast(u8 mut@ own, void* own)` is rejected (D3.14 lists pointer-to-pointer, not span-to-pointer).
  The `libc.free(cast(move(p), void* own))` idiom of `stdlib.md` 2.2 therefore applies to a
  `T mut* own` from `new(T)` or `libc.malloc`; a `T mut@ own` from `new(T, n)` is released with
  `del`, and what crosses to C is its `.ptr`, a view. A byte buffer crosses with no cast at all,
  since T-086 gave the six byte functions `u8*` and `u8 mut*`: write
  `libc.read(fd, buf.ptr, buf.len)` for a `u8 mut@ buf`, and keep the cast only where the types
  really differ, as a `string`'s `char*` does.

**The standard library holds a float, and every program pays for it** (T-132). `std/rt.ft` holds
the two float printers of D18.1 and the search of D18.2. They stood in `std/rt_float.ft` until
T-132, which the compiler loaded into a closure that held a float and into no other; the split
existed because the C bootstrap refuses a float and compiled `std/rt.ft` into every closure it
read. T-131 ended that reason and T-132 made the two modules one. `std/rt.ft` is in every import
closure (D9.10), so every program now emits the float printers, the `std.strbuf` they import for
`append_f64`, and the `std.mem` behind it.

**Deleting `std/rt_float.ft` took a pin, and that is the second reason a pin moves** (T-132).
Pin 0's loader reads `<std-dir>/rt_float.ft` into any closure holding a float, and the fold put
floats into `std/rt.ft`, which stands in every closure, so pin 0 asked for that name on every
build: with the file deleted the build gave `std/rt_float.ft:1:1: error: cannot read` and
stopped, measured on 2026-09-14 at 6c56459. Nothing about the language was in the way -- every
pin implements floats. So T-132 took `bootstrap-1`, the commit of the fold itself, whose
`src/fort` names `rt_float` nowhere. The next commit deleted the file. Invariant 3 below lists
both reasons now.

**What the fold costs, measured on 2026-09-14 (T-132), against the split T-096 measured on the
same day.** A program that prints no float paid nothing for the split. It pays 105,914 bytes of
emitted IR and 8,549 bytes of `.text` after the fold: 84,294 bytes of IR and 10,120 of `.text`
before, 190,208 and 18,669 after, and its closure grows from 3 files to 5. A program that prints
one pays nothing new; it is 36 bytes of `.text` smaller than before, because the fold dropped a
duplicate constant and one module of the closure. **Name the unit**: the emitted IR of such a
module is about twelve times the `.text` it becomes, so an IR figure offered as the cost of a
binary overstates that cost by that factor. The commands, run at the top of the worktree in the VM
after `tools/vm build debug fort`:

```sh
printf 'fn main() i32 {\n    println(1);\n    return 0;\n}\n' > /tmp/int.ft
printf 'fn main() i32 {\n    println(1.5);\n    return 0;\n}\n' > /tmp/flt.ft
for p in int flt; do
    build/Linux/debug/fort -S --std-dir build/Linux/debug/std -o /tmp/$p.ll /tmp/$p.ft
    echo "$p $(wc -c < /tmp/$p.ll) $(grep -c '^define' /tmp/$p.ll)"
    build/Linux/debug/fort --index --std-dir build/Linux/debug/std /tmp/$p.ft |
        python3 -c 'import json, sys; print(len(json.load(sys.stdin)["files"]))'
    build/Linux/debug/fort --std-dir build/Linux/debug/std --cc "$(command -v clang)" \
        -o /tmp/$p /tmp/$p.ft
    size /tmp/$p | tail -1
    for i in $(seq 7); do
        t=$(date +%s%N)
        build/Linux/debug/fort -S --std-dir build/Linux/debug/std -o /tmp/$p.ll /tmp/$p.ft
        echo $(( ($(date +%s%N) - t) / 1000000 ))
    done | sort -n | sed -n 4p
done
awk '/^define /{c=""; if (match($0, /@"[^"]+"/)) {m=substr($0, RSTART+2, RLENGTH-3);
     sub(/\.[^.]*$/, "", m); c=m}} c!=""{b[c]+=length($0)+1} /^}$/{c=""}
     END {for (m in b) print b[m], m}' /tmp/flt.ll | sort -rn
```

After the fold both programs read the same: 5 files in the closure, 91 definitions, 190,208 and
190,200 bytes of module, 18,669 and 18,649 bytes of `.text`. The two differ by the program's own
`main` and by nothing else.

Before the fold the integer program's closure held 3 files, `std/rt.ft` and `std/libc.ft` beside
the program, and its module was 84,395 bytes and 51 definitions, 48 of them `std.rt`'s, for
10,136 bytes of `.text` (T-096; the same run on 2026-09-14 at 6c56459 read 84,294 and 10,120).
The float program's closure held 6 files, the three above plus `std/rt_float.ft`, `std/strbuf.ft`
and `std/mem.ft`; its module was 191,160 bytes and 91 definitions, the 40 new ones being
`std.rt_float` (76,889 bytes), `std.strbuf` (24,179) and `std.mem` (2,732), for 18,701 bytes of
`.text`. The integer program's module held no byte of `std.rt_float`, because `load_float_runtime`
in `src/fort/modules.ft` loaded that module into a closure that held a float and into no other.
The emit took 55 ms against 69 ms, each the median of seven runs of the loop above on an idle VM,
and 72 ms against 94 ms in a later run that shared the machine with a second build: read the pair,
never one number of it.

**What makes the fold cost anything is that a module emits every definition of every module in
the closure**, called or not, and external linkage carries each one past the linker. 14 of the 48
`std.rt` definitions in the pre-fold integer program's module are named by no call in it. Each of
the 14 stands on its own `define` line and appears nowhere else in the module, and `nm` finds all
14 in the binary:

**Product library scope.** Product tests use the current compiler and current standard library.
The C compiler does not run this corpus. Only two C contract suites read the bootstrap-1 standard
library (T-160).

## 8. The port to fort

- **`src/fort` is written re-entrant** (D20.5), because a language server is a planned consumer of
  the compiler's modules and retrofitting that later would touch every pass. Four rules, set by the
  skeleton (`src/fort/containers.ft`, `diag.ft`, `session.ft`) and followed by every module ported
  after it. No module-level mutable state that outlives one analysis: the diagnostic sink, the
  counters and the caches are fields of `session.session`, which the driver creates, passes down
  and frees, so two analyses in one process share nothing (`bootstrap0/src/diag.c` keeps one
  file-scope `sink` holding every counter, which is the habit not to transliterate). Every
  allocation of an analysis comes from that session's pool or from a container the session frees,
  so a document analysed a thousand times leaves the heap where it found it. Nothing in a library
  module ends the process: an impossible input is a
  `panic` at the boundary that broke the precondition (D13.3), never an exit deep in a leaf (the C
  bootstrap ends the process at 73 sites across 17 modules:
  `grep -rn 'fatal_internal(\|fatal_oom(\|\bexit(' bootstrap0/src/*.c | grep -v fail.c | wc -l`;
  and its arenas are never freed). A panic buys a documented boundary and a stated precondition,
  not in-process recovery: `std.rt.panic` aborts like every other failure (D11.4), so a server that
  must survive a malformed document runs the analysis where it can observe that abort (D20.5).
  And every read of a source file goes through `session.read_source`, which answers from the
  overlay a `session.set_source` installed before it opens anything, so a server points the
  compiler at an editor buffer without touching a pass. Each module's header comment also records
  whether it uses a function-pointer dispatch table (D3.10) or a switch.
- **A port uses direct unit tests.** `diag.ft` first enforced the
  twenty-diagnostics-per-file cap of D14.2 inside `report` and charged a note to the budget, which
  reads like the decision and is not what the compiler does: `bootstrap0/src/diag.c` counts a file's
  errors only (`diag_note` touches no counter) and the cap is checked by `lexer.c` and `parser.c`
  before they report, so `check.c` and `modules.c` are uncapped. The divergence is stage-visible --
  25 type errors would have printed 20 in the fort compiler and 25 in the C compiler. Direct
  unit tests now hold the required behavior of each implementation. The project does not maintain
  C-to-fort parity (T-160).
- **The completed translation from the bootstrap into fort.** During Phase B, the C compiler
  built the fort compiler. Compiler sources used only the bootstrap subset: no floats (`f32`,
  `f64`, float literals), no second array or span level in one written type (`i32[3][4]`,
  `i32[4]@`, `u8@@`, `node@[4]`, T-043), no `do { } while`, no `?:` and no `$cfg` (T-156).
  The source compiler alone supports configuration expressions. Function pointers are inside
  the subset (D3.10), and a dispatch table wraps them in a struct, because an unparenthesized
  function type carries no suffix of its own (T-136: `fn (i32) i32[2]` returns an `i32[2]`, so an
  array of function pointers is `struct slot { fn (i32) i32 f; }` and `slot[2]`). Both current
  parsers accept the group `(fn (i32) i32)[2]` (D3.6), but `src/fort` and `std` must not use a
  parenthesized type yet. `tools/bootstrap.ref` pins bootstrap-1 at `0fe521c`, which predates
  groups and builds working-tree HEAD: it reports `expected a type, found '('` at a group. Use
  the struct wrapper there until a re-pin moves the chain past the group change. T-153 puts an
  explicit C variable tail inside the subset when its types use no other unsupported form. These
  are the constructs a C file may hold that have no fort spelling, with what replaces each; the
  rules the bootstrap
  already follows so that it stays portable are the first four.
  **The target bootstrap chain and its invariants** (T-155, D14.7). CMake detects Linux or Darwin
  from the host operating system. The C compiler builds only `bootstrap-1`. Each source compiler
  builds the next listed revision. The last listed compiler builds working-tree HEAD. Five
  invariants hold the chain:
  1. **One list serves both hosts.** `tools/bootstrap.ref` contains the same full commit SHAs for
     Linux and Darwin. Each listed tree contains the compiler and standard sources for both hosts.
  2. **The host operating system selects the target.** Linux selects `x86_64-linux-gnu`. Darwin
     selects the fixed `arm64-apple-macosx11.0.0`. The build rejects cross compilation and other
     systems. No stage crosses between targets.
  3. **CMake owns the new graph.** CMake reads and validates the gap-free list. It creates each
     extraction and compiler edge. The graph does not call a shell script to read the list or select
     a predecessor.
  4. **A source pin moves only when the previous source stage cannot build a required later tree.**
     A new pin must build with its predecessor and build its successor. Both builds must pass on
     both supported hosts. Each pin is an ancestor of HEAD. A shallow clone that lacks one pin
     cannot bootstrap.
  5. **The C compiler has one production job.** It builds `bootstrap-1`.
     The pinned compilers still emit `fort_entry` and reserve its name (T-212), so a source that
     the chain builds (`src/fort`, `std/`) must not declare `extern fn fort_entry`.

  **The C compiler stays frozen against general language work** (T-046, T-160). Change it only to
  fix a specified C defect or to keep the native C-to-bootstrap-1 edge working on a supported host.
  It does not track the fort compiler's language behavior. The one exception is its IR form: the
  user had it emit the fort compiler's IR on both targets, with fixed externs, `nobuiltin`
  declarations, `sret` call sites, `inline-asm` stack probes and no PIE link flag (D9.8, D9.9,
  D10.8, D14.3).

  Set `FORT_STAGE1_COMPILER` to build HEAD with an external compiler and no listed revision.
  `FORT_ENABLE_BOOTSTRAP0=OFF` requires this setting.
  `FORT_ENABLE_BOOTSTRAP0=ON` rejects it. External-stage1 mode reads no pin and registers no
  bootstrap tests. A successful native build proves the bootstrap edge.

  - No unions, no bitfields, no anonymous struct or union members: a fat tagged struct with a
    kind enum and every field in the open, which is what `ast.h`, `types.h` and `sym.h` already
    are.
  - No macro beyond a constant, and no token pasting or stringizing: a module constant
    (`u64 WORD_BITS = 64;`) or an ordinary function. A C `enum { NAME = value }` becomes a module
    constant or a fort `enum`, whose members are qualified (`kind.num`, D3.9).
  - No compiler builtin fort lacks. `__builtin_add_overflow` and `__builtin_mul_overflow` are
    the exception the exact constant folder needs, and the port replaces their three sites in
    `bootstrap0/src/consts.c` by pre-checks: `am > UINT64_MAX - bm` in `add_raw`,
    `a.mag != 0 && b.mag > UINT64_MAX / a.mag` in `cv_mul` and `a.mag == UINT64_MAX` in `cv_not`.
    No new builtin may be added without the same note.
  - No `goto`, no `switch` fallthrough, and an `enum` switch must name every member or carry a
    `default` (D7.6). The bootstrap uses none of the three today; keep it that way.
  - **No pointer arithmetic at all** (D10.4): `p + 1`, `p++` and `p[i]` are errors, and
    `bootstrap0/src/str.c` is the file that uses them (`p->cur + p->used`, `b->data + b->len`).
    The fort form is a span and an index; the only way from a raw pointer to a span is the
    two-bound `p[lo..hi]` (D6.9).
  - **No implicit conversion but dropping `mut` and `own`** (D5.4, D17.4) and **no integer
    promotion, not even for `u8`/`i8`** (D6.2). Every mixed-width or mixed-signedness expression
    that C writes silently needs an explicit `cast`, and that is the single largest mechanical
    difference in the port. `.len` is a `u64`, so an `i64` loop counter over a span is a type
    error rather than a warning.
  - `char` is a distinct one-byte type with no arithmetic and no bitwise operators (D3.2):
    `c - '0'` becomes `cast(c, i64) - cast('0', i64)`.
  - `sizeof` takes a type, never an expression (D3.15): `sizeof(x)` becomes `sizeof(T)`.
  - A C string is a NUL-terminated `const char*`; a fort `string` is a pointer and a length and
    may hold an embedded NUL (D3.7). A literal carries a trailing NUL that `len` does not count,
    so `s.ptr` is a C string for a literal and for nothing else: a sub-string is not
    NUL-terminated, and `str.to_cstr` is the conversion at the C boundary, `str.from_cstr` the
    one coming back. `strcmp` of two names becomes `==` on two `string`s, which compares `len`
    and then the bytes.
  - A fixed array is a value: `T[N]` copies on assignment, on argument passing and on return
    (D3.4), where C decays it to a pointer. A parameter that means "the caller's array" is a span
    `T@`, and `T[N]` has no `.ptr`.
  - A fort definition and a function type have no variable tail (D9.8). Stage2 lets an
    `extern fn` declare `...` after at least one fixed parameter (D19.1). The checker accepts
    only `i32`, `u32`, `i64`, `u64`, `f64`, pointers, and function pointers in the tail (D19.2).
    It lends an owning lvalue and rejects an owning rvalue. A fixed extern declaration uses the
    fixed LLVM form and a marked extern the variadic LLVM form, on both targets. Mac puts its C
    tail arguments in stack slots.
    The print family is a builtin (D11.7). The compiler uses `print`, `println`,
    `eprintln`, or an explicit string buffer instead of C print calls.
  - No function-scope `static`: a module-level `mut` global replaces it, and it is visible to the
    whole module rather than to one function.
  - No forward declaration: top-level declarations are order-independent within a module (D7.10),
    so every C prototype the file carried for ordering disappears. **Two types that point at each
    other must therefore live in one module**, since circular imports are a compile error (D9.5)
    and no fort module can name a type of a module that names one of its own. C gets away with it
    through the incomplete type a header may declare (`struct ast_node` in `sym.h`), so the C
    file boundary is not a guide: `ast.h`'s node and `sym.h`'s record are one module in fort
    (`src/fort/ast.ft`), and `scope.ft`'s `void* module` is the other way out where one of the
    two may be opaque. **The same cut applies to two C files that call each other**: `check.c`
    and `check_stmt.c` do, so `src/fort/check.ft` stops where the single reverse edge is --
    `check_module_decls` resolves the declarations, `check_module_finish` ends the pass, and the
    body loop between them, with `check_module` and `check_program` themselves, is
    `src/fort/check_stmt.ft`'s -- and the split is a ticket boundary rather than a copy of the
    C's. **The half that is cut may not keep the whole function's name**: `check.check_module`
    returning a bodies-unchecked result was the shape T-035 left and T-036 deleted, since a later
    caller gets the wrong answer from a function whose name promises the right one. Name the
    halves for what they do and let the module that closes the cycle own the complete function.
  - **A value must not store a pointer into storage that returning it copies**: itself, or a
    field beside the pointer. A `return` of a local aggregate copies the whole value to the
    caller, so a pointer inside it that named the local -- or a sibling field of the local --
    dangles the moment it lands. Storing the *caller's* address is fine, which is why
    `driver.ft`'s `analysis_create(&s)` is correct: `s` is the caller's session and does not
    move. What is not fine is the shape T-035's test environment had, `fn open() env` building an
    `env` in a local and calling `check.check_init(&e.ck, &e.m.s)` on a field of that local before
    returning it: every diagnostic then went to the dead local's session and the suite saw a check
    that reported *nothing at all*, which reads as a pass. The fix is to fill the caller's value
    (`fn open(env mut* e) void`). `modules.ft` sidesteps the question entirely by taking the
    session as a parameter of every call instead of holding it.
  - `_Static_assert` has no fort spelling. The invariant becomes a unit test, or a runtime
    `assert` at the one place that depends on it; `prim.h`'s assertion on the order of
    `prim_kind_t` is the site.
  - `malloc`/`free` become `new`/`del` with ownership (D17), and **there is no `realloc`**: growth
    is allocate, copy, `del`, as `std/vec.ft` and `bootstrap0/src/str.c` already write it.
  - Checked arithmetic traps where C wrapped (D11.1). Every place that means to wrap -- a hash, a
    checksum, a fingerprint -- must be written `+% -% *%` (D11.2), or the checked build aborts on
    input the C compiler handled.
  - `const` is a reserved word (D2.4) and immutability is the default, so a C `const` disappears
    and a C non-`const` gains `mut` in the position D5.3 gives it. A field never carries the
    outermost `mut` (D5.5), so `int count;` in a struct the code writes through is just
    `i32 count;` and the mutability comes from the access path.
  - **Adjacent string literals do not concatenate** (D2.9), which C uses to wrap a long text
    across lines, and a `.ft` line is 100 columns: build the text with a `std.strbuf` or split
    the statement into several. T-031 found it in a test that lexed the forty-one keywords of
    D2.4 as one line.
  - **A `case` label is a constant expression**, so a C `switch` over byte values held in an
    `int` -- the lexer's `switch (c0)` over operator characters, where -1 means the end of the
    file -- becomes a chain of `if`/`else if` comparisons against `cast('+', i64)`. The chain is
    the transliteration; do not reorder it, since the first match wins in both.
  - **An enum member may not take a keyword's name.** `char` and `string` are keywords (D2.4),
    so C's `TOK_CHAR` and `TOK_STRING` become `char_lit` and `string_lit`, and the whole family
    takes the suffix rather than two of its six members.
  - A C `T*` parameter that may be null to mean "do not compute this" (`lex_digits`'s `value`)
    is better replaced by always computing it and letting the caller ignore the result, when the
    caller that passed NULL ignores the result anyway: same behaviour, no null to reason about.
  - Nesting deeper than 256 is a compile error (D2.11), parentheses, blocks, brackets and type
    suffixes together.
  Each ported module has direct unit tests. The project does not maintain C-to-fort parity
  (T-160).
  Eight more facts the first ports paid for: five from T-032 (`prim.ft`, `consts.ft`,
  `types.ft`) -- keywords, `new(T, n)`, `==`, enum ordering, the forked tests -- and three from
  T-033 (`ast.ft`, `parser.ft`, `test/fort/support/parse_env.ft`) -- joining strings, the
  NULL-for-no-message parameter, and the fixture's token vector. A fourth, that two mutually
  recursive structs had to be declared in one particular order, was a bootstrap-0 bug and is gone
  (T-082).
  - **Keywords take the names first.** `type`, `const`, `match` and the rest of D2.4's reserved
    list, and every type keyword, are not identifiers, so `type_t` cannot be `type`, a field
    cannot be `mut`, `own` or `noreturn`, and an enum member cannot be `i8`, `bool` or `null`.
    The port keeps the C function names verbatim (`types.type_ptr`, `consts.cv_add`,
    `prim.prim_is_integer`), stutter and all, because the module answers to its C original name
    by name; it renames a field to `is_mut`/`is_own`/`is_noreturn` and gives an enum member the
    C constant's prefix (`cv_int` for `CV_INT`), or a short one where the C name is already a
    function's (`k_ptr` for `TYPE_PTR`, beside the constructor `type_ptr`).
    **A module drops the C prefix only when every name it holds stays legal.** `diag.ft`,
    `modules.ft` and `index.ft` drop it (`diag.error` for `diag_error`, `index.build` for
    `index_build`), since the module name already says which module it is. `json.ft` keeps it on
    every function (`json.json_bool`, `json.json_null`), because `bool` and `null` are keywords
    (D2.4) and a module with two spellings in it is worse than one stutter. Pick one rule per
    module and say which in the header comment.
    **A universe function's name is taken too, but only where the module needs the builtin.**
    `del`, `move`, `panic`, `assert` and the print family are universe-scope names that a
    module-level declaration shadows inside that module (D12.2, D7.9), so a module that declares
    one loses the builtin for its own body. The question to ask is therefore not "is this name
    reserved" but "does this module call the builtin it would shadow": `std.rt` may define
    `panic`, which it implements and never calls, and may not define `del`, which it calls on its
    own buffers -- so the entry points behind `new` and `del` are `alloc` and `free` while the one
    behind `panic` is `panic` (`toolchain.md` 5.1, T-088). A keyword, by contrast, is never
    available: `new` is one (D2.4), so no declaration of that name exists at all.
  - **`new(T, n)` marks the storage it allocates and no position below it** (D5.8, D10.2), so
    the element type of the result is the element type written: `new(node mut*, n)` is a
    `node mut* mut@ own` and `new(node*, n)` a `node* mut@ own`. Write the `mut` the field wants.
    Until T-135 `new` marked every position and no `mut` parsed inside `new(...)`, so a field of
    borrowed pointers took a `cast` at the allocation; `src/fort/gen.ft` held two of those and
    `src/fort/types.ft` one, and all three are gone. The conversion between the two spellings is
    still refused in both directions: adding a `mut` needs a `cast` (D3.14), and dropping the
    pointee's `mut` behind a mutable span is D5.4's `T** -> const T**` hole, for which a `cast` is
    the sanctioned escape and the only one. `test/lang/run/pointers/013` and
    `test/lang/fail/mutability/012` are the pair that hold it.
  - **`==` does not drop `mut`.** Operands lend `own` (D17.4) and nothing else, so comparing a
    `node mut*` with a `node*` is a type error: give the test a `node*` binding rather than
    casting.
  - **Neither adjacent string literals nor `+` join two strings** (D2.9, D3.7), so a C message or
    expected text wrapped across two literals becomes a module constant whose text stands on a
    line of its own (`MUT_BEFORE_ARRAY_ERROR` in `parser.ft`), a second `msg_str` call, or, in a
    test, a `join2`/`join3`/`join4` helper over a buffer of the fixture
    (`test/fort/support/parse_env.ft`). A test fixture that hands out views of one buffer needs
    one buffer per role -- the source being built, the dump being compared, the text being
    joined -- or an assertion compares a string with itself.
  - **A `const char*` parameter that is NULL for "no message" becomes the empty string**, since
    no message is empty (`parse_markers`'s `own_error`); state the sentinel in the comment.
  - **A fixture that reuses one token vector must truncate it before each parse.** `lex_file`
    appends and `parse_module` reads from the first token, so a second parse into the same vector
    silently re-parses the first source and the second assertion passes for the wrong reason
    (T-033); the same fixture resets the diagnostic sink, so that an error count answers for the
    source it was just given.
  - **An enum has no ordering operators** (D3.9), so a C range test over a kind enum
    (`k <= PRIM_U64`) becomes a comparison of `cast(k, i32)`, and the `_Static_assert` that
    pinned the order becomes a test (`test/fort/prim_test.ft`).
  - **The C's forked internal-error tests become one file each.** `fatal_internal` is a `panic`
    in `src/fort` (D13.3), a panic ends the program (D11.4), and a `test/fort` test cannot fork,
    so each broken precondition is a `<module>_<case>_panic_test.ft` that prints one line, calls
    the site and carries the message in a `//! stderr:` directive.
  - **`fort -S` run by bootstrap-0 was the oracle for the emitter port** (T-037). The IR of a
    program is a function of the program alone (D19.5), so the expected text of a fort emitter
    test was read off bootstrap-0's own output over a program whose body the test writes, and
    never transcribed from the fort under test. One mechanical step made the two comparable: a
    suite that drove one expression at a time renumbered `%tN` and `%LN` from zero, which is what
    the emitter produced when the expression was a function's first. It earned its keep: the
    port and the reading of D10.2 disagreed about whether `new(T, 3)` checks its literal count,
    and the oracle said the emitter was right. Since T-255 deleted the direct path, the
    expression suites hold the FIR translator's text of the whole `main` (`gen_env.main_text`),
    numbered as it stands there; the line and column of each failure call are still the ones
    bootstrap-0 wrote for the same operator.
  - **The C emitter's four files are one dependency cycle, so the fort port is layered and not
    cut where the C is.** `gen.c` calls `gen_data.c` (`gen_file_ref`, `gen_call_rt`,
    `gen_append_name`) and `gen_stmt.c` (`gen_block_scoped`), and both call back, which no set of
    fort modules can express (D9.5). `src/fort/gen.ft` is therefore gen.c's primitives together
    with gen_data.c's private data, name spelling and runtime calls -- what the checks of D19.6
    need -- and `gen_data.ft` holds the data and the assembly of the sections. The port of
    `gen_expr.c` and `gen_stmt.c`, the direct path, went in T-255: `fir_lower.ft` and
    `fir_llvm.ft` do its work, and `gen_fir.ft` holds gen.c's `gen_module` and `gen_program`
    over them, while `gen_main` stands in `gen.ft`. The functions that remain keep their C names,
    so the primitives of the two emitters are still read side by side name by name. The one
    exception since T-212 is gen.c's `gen_fort_entry`, which the fort emitter no longer has: the
    emitted `main` calls the program's `main` directly.

**The fort compiler can implement language forms that the C compiler does not implement.**
Product tests hold these forms directly. The project does not maintain C-to-fort parity (T-160).

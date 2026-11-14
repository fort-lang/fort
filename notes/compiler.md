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
- **Source is UTF-8 text** (D2.1): the lexer of `src/fort/lexer.ft` reads a byte above 0x7f in
  five places: a comment, a string literal, a char literal, the byte after the backslash of an
  escape, and the start of a token. Each place asks `utf8_len` first. `utf8_len` gives 0 for
  bytes that are no well-formed sequence (The Unicode Standard, table 3-7), and the place then
  reports `invalid UTF-8 sequence at byte 0xHH` at the first byte of the sequence. A comment and
  a string literal step over each byte of a sequence, so a continuation byte never starts a
  character. A new place that reads a byte above 0x7f must ask `utf8_len` too. The error costs
  its line as each lexical error does, so a file gives one such error for each line at most. A
  place behind an earlier error of the line gives that error: `'a` then the byte 0xE9 then `'`
  gives "char literal holds exactly one character". An escape is ASCII text in the file, so
  `"\xE9"` holds the byte 0xE9 (D3.7). Bootstrap-0 is frozen and does not have the rule. The
  first compiler with it is the one that the chain builds from the tree with the rule, and
  `tools/fixpoint.sh` then applies it to `src/fort` and `std`.
  `tools/mutations/lexer_fort.json` holds the bounds and the five places. It holds the skip of
  the byte order mark and the limit of 20 diagnostics (D14.2) too. Its rows
  `D14.2-resync-lone-cr` and `D14.2-line-base-lone-cr` hold that a lone CR ends no line for
  a resync (D2.1). Bootstrap-0 is frozen, so it still resyncs at a lone CR.

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
  only forbids unannotated lines also passes with recovery switched off. It asserts no file count,
  so nothing notices a file that the walk skips. The `test/lang` runner judges each file of
  `test/lang/fail` with the product compiler, not with the parser of bootstrap0. A ticket that adds
  a suite of its own to `bootstrap0/test/` runs `tools/vm configure` before `build`, since the
  executables are globbed at configure time.

## 4. The checker

- **Every type node that is not nominal must enter the intern index.** `types.intern` finds an
  equal node through the hash index of the table (`src/fort/types.ft`), not through a walk over
  every node. `intern` adds each node that it makes. `type_void`, `type_null`, `type_error` and
  `type_prim` make their node without `intern`, so each of them calls `index_add`. A new
  constructor of a node that is not nominal must call `index_add` too. Otherwise `intern` does
  not find that node and makes a second equal node, and two equal types then compare unequal by
  address. `index_add` keeps the first equal node, so a lookup finds the node that a walk in
  creation order finds. `test/fort/types_intern_index_test.ft` holds the index against that walk.
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
- **A range loop holds a span or string header once** (T-263, D7.5, D17.10).
  `fir_lower.range_collection` copies the header into a lent temporary before the loop head.
  An owning fixed array still lends its original storage. `check_stmt.check_range_writes` checks
  the resolved body for assignment, `del`, `move`, and implicit return moves of collection storage.
  The check includes containing paths and local reference initializers. Scalar element writes
  remain legal. This AST check has no alias dataflow or call-effect analysis. It conservatively
  refuses fort calls and other ownership writes during a range loan. An owning element can own
  the object that contains the collection. Its release can therefore free the collection storage.
  Trusted extern calls retain their D17.13 contracts. The user cancelled the planned loan proof
  on 2026-10-22, so this refusal stays the rule. The T-263 ticket records the probes and the cost.
  Do not treat this AST check as exact loan proof.
  `del(s, d)` is a `del` of `s` there and a call of the fort function `d`.
  The walk reports the span operand `s` as it reports each other operand.
  It reports `d` only when the walk of `s` gave no report, as for `del(new(...), d)`.
  So `del(s, d)`, `del(move(s), d)` and `del(make(), d)` give one report each.
  The check read only the one-operand forms before.
  Then `--check` accepted `for (e : x) { del(x, d); }` and `--fir-run` gave `use after release`.
  `test/fort/check_range_test.ft` holds each storage form and the report counts.
  The fail tests 100 to 107 of `test/lang/fail/ownership/` hold the goldens.
- **`src/fort` carries the `mut` in the declaration, where the C casts a `const` away.** The
  bootstrap holds a tree of `const ast_node_t*` and a record of `const sym_t*` and casts the
  qualifier off at each of the ten places the resolution writes through one. T-085 made a
  mut-adding cast an error (D3.14), so the fort compiler cannot copy that. Four fields carry the
  mark instead -- `ast.node.sym`, `ast.sym.node`, `ast.sym.owner` and `scope.binding.node` --
  and `ast.ast_child_mut` answers a child of a node the same way. The shallow
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
- **A size behind a reference waits for its layout** (D3.4, D3.8, D7.10). `check_size_fits` walks
  the type behind each `*`, `@` and function type and asks the size of each array there. The
  rule above keeps a struct behind a reference unresolved, so such an array can have no layout
  yet. The checker counts the active `resolve_sym` calls in `resolve_depth`. While
  `resolve_depth > 0`, no size check forces a layout: `referenced_too_large` puts the array and
  its position on the list `sized`. The outermost `resolve_sym` drains that list when it returns
  (`sized_drain`), and only then lays the struct out. A forced layout at depth > 0 reports an
  infinite size for `struct outer { inner[2]* p; } struct inner { outer o; }`, and a skip makes
  the two declaration orders of one pair two programs. `check_limits_test.c` and
  `check_limits_test.ft` hold both orders, the self-reference and that cycle.
- **The `$if` selector sizes no type** (D21.2). The selector (`comptime.ft`) runs before module
  closure and name collection, so it cannot use the checker's types or layouts. Until 2026-10-08
  a condition could use `sizeof(T)`, and the selector kept its own copy of the checker's type and
  size rules for it. Three differences between the copies were defects: an array too large behind
  a pointer, an array of a struct, and a `void` part of a function type. Now `refuse_sizeof`
  reports the first `sizeof` of each condition of a chain before the chain evaluates, and of each
  reached initializer and `.len` array length. Do not add a rule to the selector that needs a type
  or a layout: give the facts a `$cfg` key (D21.1) instead. `fail/control/013` holds the forms.
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
- **A `mut` cast keeps the pointer places of its first `mut` level** (D3.14).
  `check_pointer_places` in `src/fort/check.ft` follows the two chains of references through the
  read-only levels of the result, and through fixed arrays on both sides, to the first level that
  the result marks `mut`. There each pointer place -- a reference, `own` or not, or a function
  pointer -- must stand on both sides with an identical type: `first_hidden` runs in each
  direction, so a hidden place is named before an added one. The type of a pointer includes what it
  reaches, so the walk stops at that level. `types.type_pointer_count` answers whether a type
  holds a place, from the count that `types.type_layout_struct` keeps beside the owner count.
  `first_hidden` reads the type and the byte offset of each field from the layout
  (`types.field_at`), so both storages need a layout: a struct in error, or a layout that still
  waits in a module-level initializer, ends the walk. A run of copies goes to the smallest part
  of the other side that holds it, with its byte in that part, and over an array with its byte
  modulo the element size;
  `place_memo` keeps each run, part and byte whose places are all held. **The work is bounded, not
  linear:** two arrays whose elements meet at many offsets cost one step for each offset, so the
  walk stops after `POINTER_WORK_LIMIT` (65536) steps and the cast is an error with a note. The
  casts of the five trees take 10 steps or fewer; `fail/casts/027` passes the limit on purpose,
  and a check that stops there takes 0.2 s and 18 MB. A cast that is an argument of an `extern fn`
  call meets each rule of a cast: the checker has no exemption and no mark of such an argument
  (`fail/casts/033`, `run/casts/014` and `018`).
- **A cast makes no pointer from an integer and no larger pointee** (D3.14). `check_cast` asks
  `types.type_cast_makes_pointer` before the matrix, so an integer source of each width reports
  the rule before an added `mut` or `own`. A `char`, a `bool`, an enum and a float keep the report
  of a pair that no row allows. `types.type_cast_allowed` keeps its row from `u64` to a pointer and
  to a `void*`: FIR verification (V4) reads the matrix alone, and the FIR text of
  `test/fort/fir/shadow_test.ft` (30 casts) and `interpreter_test.ft` (2) forges a pointer through
  it on purpose. Those suites run the FIR in `fir.interpreter` and never reach `fir_llvm`.
  `fir_llvm.cast_value` writes the row as `inttoptr`, which the FIR text of
  `fir_llvm_rvalue_test.ft` holds. No fort source reaches the row.
  `check_pointee_growth` compares `sizeof` and then the alignment of the level-1 pointees of a
  typed pointer cast, after the rules of `mut`, `own` and `move` and before `check_pointer_places`,
  with no exemption for an `extern` argument.
- **A cast gives no type to a `void*`** (D3.14). `check_cast` asks `types.type_cast_gives_type`
  after the rule of a pointer from an integer and before the matrix, so a `void*` source with any
  marks reports the rule for each typed pointer target, before an added `mut` or `own` and with no
  exemption for an `extern` argument. A function pointer target keeps the report of
  `report_function_pointer`, and a span target the report of the matrix. The matrix refuses the
  pair too, and a `void*` to a function pointer type (`types.type_cast_makes_function`), so FIR
  verification (V4) refuses a `cast<T*>` and a `cast<fn ...>` of a `void*` in FIR text
  (`test/fir/verify/v04_cast_void_*`), and `fir.md` 4 names no row for them. FIR text that forges a
  pointer goes through the row from `u64`, or reads a slot through a pointer to another type. A
  typed extern signature or a typed handle replaces each such cast: `std.sort` sorts typed spans
  in fort, a vector of pointers is a typed copy of `int_vec`, and `libc.readdir` gives a
  `dirent*`.
- **A cast keeps each pointer place, in any view** (D3.14). After the walk of the `mut` levels,
  `check_view_places` runs `first_hidden` once more over level 1 of the target with
  `place_memo.view` set. A place of the same type at its byte in the source holds a place of the
  target. A place of the source of the same shape (`types.type_same_shape`) holds it only when
  `carries_mark` answers false for the place of the target, so the walk of the marks runs only
  for such a place, and a cast whose places are all identical walks no struct. Each other place
  of the target is added. `check_cast` refuses a `void*` source with a typed target before this
  walk, so no source of it is a `void*`. A function pointer carries a mark always
  (`mark_step` answers MARK_SOME). The walk reads none of its parameters and not its result, so
  no cycle passes through a function type. A cast to a function pointer type from another type
  (`check_cast` lets only a `void*` past the matrix) is the error of `report_function_pointer`, with
  no byte. The test stands at the top of `check_pointer_places`. C calls a function as the type of
  its parameter says (`fail/casts/036` to `038` and `040`, `run/casts/017`). An argument of an
  `extern fn` meets the same walks (`fail/casts/041` to `043`). `walk_marks` is Tarjan's algorithm
  over the structs, with explicit stacks (`place_memo.frames` and `entered`), so a chain of 65600
  structs needs no deep recursion. A struct keeps its answer in `types.layout.mark` for the life of
  the checker: MARK_SOME on the path to a mark, MARK_NONE for each struct of a set of structs that
  reach each other once the walk closes the set with no mark, and no answer after a layout that
  waits or the work limit. So the walk enters each struct once, and each entry is a step of
  POINTER_WORK_LIMIT (`check_mark_limit_test.ft`). Before the cache, one legal cast of a 15248-line
  program took 15.65 s, because each pointer place walked its chain again. **No compiler module
  casts a `void*` back to a typed pointer**, because the checker refuses each such cast. A type
  keeps a `u64` handle of its declaration, `k + 1` for `ck->syms[k]` (`check.nominal_sym`), and the
  layout of a struct keeps the type, the offset and the name of each field, so the FIR, the
  interpreter and `owner/place` read the fields with no declaration. `scope.binding.module` is
  `k + 1` for module `k` of the set (`check.module_of`, `modules.read_at`), and the source lookup
  of `diag` takes the `diag.sources` that the session embeds.
- **A `.ptr` stands as each argument of an `extern fn`, and no rule reads what C gives back**
  (D3.5, D17.13). `check_call` sets `checker.ptr_ok` to the `.ptr` or `->ptr` node (`ptr_node`) of
  each argument of a call whose callee is an `extern fn`, in the variable tail too, and
  `pseudo_field` reports each other `.ptr` of a span or a string. The `.ptr` must be the argument
  itself: a cast or a `?:` around it is no such position. `check_call` reads the extern from the
  symbol of the callee node (`callee->sym`), as `fir_lower.call_rvalue` does, and gives the result
  that symbol: `f()` names `f`, which `check_no_value` quotes, and `get()(x)` names no function, so
  it has no variable tail and takes no `.ptr` (`fail/ffi/013` and `015`). `&` reads its operand
  node, because `&f()` takes no function. A field of a struct named `ptr` is no `.ptr`, because
  `pseudo_field` reads spans and strings only. Only a trusted module declares or calls an extern
  (D9.8), and the rows of D9.8 state what C gives back: the declaration row gives the `mut` of each
  pointer that C gives and where a pointer that C gives back lies, and the call row gives the bound
  of each `.ptr`. In a module that is not trusted, each call is the error of D9.8, and its `.ptr`
  arguments add none (`fail/ffi/013` and `014`, `check_extern_test.ft`). Two compile rules read the
  types of a declaration before D9.8 replaced them: the `mut` rule of D17.13 (`first_given_mut`) and
  the walk of D3.5 (`give_walk`, with its work limit and the field `types.layout.give`). Each one
  checked a part of A7, and each review round found one more route past it. `types.layout` is 80
  bytes without that field (`types_layout_test.ft`).
- **Only a trusted module meets C, and the trust is one bit of the module node** (D9.8).
  `ast.FLAG_TRUSTED` on the `k_module` node is the bit. The parser sets it for a first item
  `import extern;` (`parse_trust_decl`), which makes no node, and reports each other
  `import extern;` as `'import extern;' stands only as the first item of a module`, then reads it
  to its `;`, so no recovery skips the item after it. The loader sets it for a module whose path
  starts with `std.` (`from_trusted_root`), because only the standard library directory and the
  root of `fort --lib std <root>` give such a path, and for each section of the used library
  `std` (`load_section`). The path of an entry module is the base name of its file and holds no
  `.`, so an entry file `std.ft` is trusted by its own form alone. An entry file that the closure
  of `std.rt` read before it from the standard library directory (rt, libc, mem, str and strbuf)
  stays the module of that read, with its path and its trust (`entry_already_read`); each other
  file of that directory named on the command line is the entry module of its base name and is
  not trusted, so `fort --check <std>/io.ft` reports the errors of D9.8. `modules.trusted` reads
  the bit, and the checker reads it for the module that it checks (`checking_trusted`) and for
  the owner of a symbol (`declared_trusted`). Three checks
  hold the rule: `resolve_declaration` reports an `extern fn` of a module that is not trusted,
  `value_of_sym` reports an extern callee and a name of a boundary function there, and
  `check_symbol_imports` reports a symbol import of a boundary function there, and a symbol
  import of `std.rt.args_init` in each module. A name of `std.rt.args_init` takes the error of
  D6.9 alone, because `value_of_sym` returns before it asks `is_boundary`. None of the three
  poisons a type, so each name is one error and the arguments of the call are checked as before.
  `is_boundary` walks the parameter types with an explicit stack (`reach_walk`): the pointee of a
  pointer, the element of a span or a fixed array, each field of a struct, and the parameters and
  the result of a function type. A struct keeps the number of the last walk that entered it in
  `types.layout.reach`, so a walk enters it once; after a walk that found no pointer to `char`,
  `u8` or `void`, each struct that it entered keeps `check.REACH_NONE` for the life of the type
  table, and a later walk enters it no more. A walk that found one keeps no such mark. A struct
  in error adds nothing; each other struct has a layout, because the module of a boundary function
  is checked before each module that names it, so the walk lays out nothing itself. The
  interface writer starts the section of a trusted module of a library other than `std` with
  `import extern;` (`interface.writes_trust`). `params_reach_byte_pointer` takes the function type
  as a parameter: with the walk in `is_boundary`, a read of `s->ty->params` after a call that
  takes `&w` was the error `use after move or del: 'w.todo' can be vacated here` (D17.21).

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

- **A library build loads its modules from a list, and a used library from its sections**
  (D9.11, module-system.md 10.2). `library/tree.ft` walks the root once and lists the modules in
  byte order of the path; `modules.load_library` resolves a path of the library only in that list,
  so the loader opens no other file for it, and it refuses a path of no library at the import.
  `modules.use_library` lexes an interface file once and keeps the token range of each section. A
  section becomes a module (`module.interface`) only when an import reaches it, and its positions
  are those of the whole file. A section has no file of its own, so it takes no place in the
  identity map. `library/interface.ft` reads the checked trees and writes nothing into them, so it
  can run on a module that an interface file declared: the writer gives that section its own text
  back, which `test/library/checker.ft` and `interface_values_test` hold.
  **`--lib` without `--emit-interface` builds the artifact** (toolchain.md 2.1).
  `driver.library_entry` checks, lowers and runs the emptiness pass, writes the interface text,
  emits the IR module of the unit, and runs the compile line and the archive line through
  `driver.run_tool`. `library.artifact.write` then writes the directory. The archive is read
  once, and those bytes are hashed and copied. The
  writer removes the manifest first and writes it last, each file through a temporary name and
  rename(2), so a failure at any step leaves no manifest (`test/fort/library/artifact_test.ft`).
  It empties `summaries/` before it writes a file, so a file of an older build does not stay, and
  it writes `summaries/ownership`, the text of `owner.summary.write`, after `interface.ft` and
  before `lib.a`. The `interface` line of the manifest is `artifact.append_interface_hash` of the
  two files, each with its name and its byte count (toolchain.md 2.2). It
  opens `summaries/` with O_NOFOLLOW and removes each entry with unlinkat(2) on that descriptor,
  so a link at `summaries` fails the write (ELOOP) and no entry outside the artifact goes. A
  file or a link at a temporary name, which a run with the same pid can leave, is removed with
  unlink(2) first, and the temporary file is opened with O_EXCL, so no write goes through a link.
  When unlinkat(2) refuses an entry and its AT_REMOVEDIR form gives ENOTDIR, the error is the one
  of the first call: darwin gives ENOTDIR there and Linux gives EACCES for both (probed with a
  directory of mode 0500).
- **A library build looks its key up before it checks, and a hit replaces the build** (D9.11,
  toolchain.md 2.1 and 2.3). `library.cache` holds the layout `<cache>/1/<key>/`, the lookup, the
  store and the `--cache-stats` line; `driver.cached_unit` holds the fields of the key. `--lib`
  reads each module file of the walk through `session.read_source` before the parse
  (`driver.read_listed`), and `artifact.hash_texts` hashes those bytes; the parse then reads the
  same bytes from the session. A build with `--use` hashes after the load, from `module.source`, and
  before the check (`driver.front_end`). The key is `artifact.append_key`, the hash of the manifest
  lines that `write_manifest` writes before the key line, so the key of a lookup and the key line of
  the stored manifest are one function. A hit of a library writes the checked bytes of the entry to
  the artifact; a hit of the entry unit writes the checked `object.o` to the object path and links
  it. The compile line of the entry unit always writes `<tmp>/<entry>.o`. The build reads that file
  once and stores the bytes. `-c` writes its output from the same bytes, after a hit and after a
  miss. So `-c -o /dev/null` stores the object, and no read waits on `-c -o /dev/stdout`.
  `artifact.write_output` classifies the `-c` output with stat(2), which follows a link, as clang
  does. A regular or absent output gets a temporary file and a rename with mode 0666, so the umask
  applies and a link to a regular file is replaced. A device, a FIFO, and a path whose temporary
  file cannot be made are written in place. A miss stores the entry by the write protocol of the
  artifact, and a store that fails is no error. A lookup refuses a link or a file at
  `<cache>/1/<key>` (corrupt), and the store refuses a link at `1` or at the entry, so it writes
  through no link that it did not make. The cache directory itself may be a link. Only a build that
  writes the artifact, or that writes an object or an executable with `--use`, uses the cache;
  `--fir-stats` does not, because a hit runs no emitter. The archiver runs with `ZERO_AR_DATE=1` in
  its environment, which `driver.run_tool` gives to `sys.run` and the child sets after fork(2), so
  the archive of macOS holds no time stamp (`test/fort/driver_cache_test.ft`,
  `test/library/cache_test.py`).
- **`--use` reads each artifact once and keeps the bytes that it checked** (D9.11, toolchain.md 2.1
  and 2.2). `driver.read_libraries` runs before the entry file or the root is opened.
  `artifact.read` reads `manifest`, `interface.ft`, `summaries/ownership` and `lib.a` once each. It
  holds the two text files against the interface hash, reads the summary file into
  `contents.ownership` with `summary.parse`, and holds each `fn` line against the set of the lines
  `fn <name>(` of the sections of the interface text, which one scan reads
  (`artifact.put_declared`); each failure is corrupt. A scan for each line took 1.53 s for a
  `--check` with `--use` of a `fort.flib` with 2,297 lines, and the set takes 0.19 s against 0.18 s
  with no line (VM slot 3). The summary file names its library, so a forged `library` line of a
  manifest alone now reads as corrupt and no longer as `is named`: the validation of toolchain.md
  2.1 step 1 comes before the rule of the name (`test/fort/driver_use_test.ft`).
  `artifact.parse_manifest` accepts a text only when `artifact.write_manifest` writes it again byte
  for byte from the parsed fields, so the order of the lines, the value form, the byte order of the
  lists and the key of rules 1 to 3 are one comparison. A hash gets a check of its own, because the
  writer writes any value of a hash field as it is. An absent file, and an entry that is no regular
  file after the link that it can be, are corrupt: the open takes O_NONBLOCK and fstat(2) reads the
  type, so a FIFO does not stop the build. Each other failure of a read is `cannot read library`.
  The checked interface text is what `modules.use_library` lexes, and the link line names a copy of
  the checked `lib.a` bytes in the temporary directory, `<tmp>/<name>.a`. `library.graph` holds the
  uses between the libraries, the cycle rule and the link order. No build writes a cycle: a build of
  `a` against `b` must name `a` with `--use` when `b` uses `a`, and a `--use` of the `--lib` name is
  refused. So only a forged manifest reaches that message.
- **The modules of a used library that the closure does not hold enter no order of a later pass**
  (module-system.md 10.4). After a clean check of the closure, `modules.load_outside` loads each
  other section, the libraries in the reverse of the link order. Such a module goes to
  `outside_order`, an import before its importer, and to `outside_listed`, in byte order of the
  path, never to `order`, and `pass_at` skips it. So the emitter, the lowering, the interface
  writer, the source hash and the index never read it. The emptiness pass runs over the closure
  before this check, in a build with `--use` as under `--lib` (toolchain.md 2.1, step 4), so an
  overwrite error stops the build first, as it does in a whole program.
  `check_stmt.check_outside` checks these modules with `checker.defer_externs` set, so
  `resolve_fn` holds back each extern, and then
  `check.check_externs` compares the externs in the listed order. The deferral keeps the order of
  the errors when a section imports a later section, which the checker reads first
  (`modules_outside_test.ft`). Under `--check --json` the `"files"` of D20.2 list the interface
  file of each used library first, in command-line order, because step 1 reads them, and then each
  other file once.
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
  checks and `--no-bounds-check` 730, and the default mode changes no function. Since the
  overwrite error of D17.11 became static, every mode removes each `check(overwrite: p)` that
  the proof of the emptiness pass proves, and folds its store. `build_mode` takes that proof and
  keeps each unproved check. Only `--fir-after=build-mode` after its error,
  `--fir-verify-report` and a FIR test bring one to it, and the translator panics on one. So
  the default mode changes each function that stores into an `own` reference: the checked IR of
  the compiler lost 180 calls of `std.rt.fail_overwrite`.
- **The emptiness pass keeps one fact for each place that a check or a store names** (`spec/fir.md`
  11). `owner.prover.prove` reads one verified function before the build-mode pass; `owner.place`
  holds the paths, the keys, the memory locals and the stale and fresh sets, and `owner.report` the
  errors. No gen rule and no kill rule of one fact reads another fact, so the pass tracks only the
  places of the `check(overwrite: p)` terminators and of the stores that need a proof
  (`find_stores`), and a function with neither and with no owner local of the leak rule costs one
  scan of its statements. Four dataflows of `fir_flow` run with gen and kill sets for the overwrite
  proof, and the leak rule adds a fifth (the next entry): the stale index temporaries (a union from
  a full entry
  set), the fresh pointer locals (an intersection from an empty entry set), the scratch locals (an
  intersection from a full entry set) and the facts (an intersection from the facts of the storage
  that is empty at the entry). A walk of each block applies each statement to an empty probe and to
  a full probe, so the first gives the gen set and the second the kill set; the walk of the facts
  reads the stale and the fresh set before each statement. Before any dataflow the pass measures the
  work: statements and terminators times the checks and the stores that no check stands right before
  and that write no whole scratch local, plus blocks times locals, plus the steps of the stale
  chains. Past `owner.prover.WORK_LIMIT` (2^20) the function gets no proof; the compiler, the server
  and `std` measure 25734 or less, and the language tests 25216 or less (`proof.work`, a probe build
  that writes the work of each function from `report.write_leaks` under `--check`, over the
  corpora, 2026-11-06). The memory marks take one scan and one walk: a held address that is
  memory ends once. Two paths are separate only while neither leaves its object, by a deref or a
  span index after its first step: `p->next->g` can be `p->g`. A held address `(*_a)` reads as the
  place of its `addr mut` when one check and one store read `_a` and nothing else does, `_a` is no
  memory, and the place is no fixed storage. An operand or a `del` through `(*_a)` is no read of
  `_a` as a whole, so it does not end the held address. A held address of fixed storage gave three
  false accepts in `test/fort/owner/oracle_test.ft` (FIR cases `held_value`, `held_fresh` and
  `held_copy`): the store through it wrote a local that the counts of the local, its freshness and
  the value of its one store did not see. So an `addr mut` of fixed storage is no held address and
  makes its local memory. The lowering of an assignment holds no address of fixed storage
  (`fir_lower.target_place`, `hold_base`), but `*&x = e` lowers to the same shape, `_t = addr
  mut(_x)` and a check of `(*_t)`: since an `addr mut` of fixed storage is no held address, such a
  store is an error, as D17.11 says, though `x` is empty (`test/lang/fail/ownership/061`). Two more
  false accepts (`stale_base`, `stale_key`) read a held address as its place after a store into the
  base or an index local of the place: such a store makes the held address stale in the stale
  dataflow (`first_reader`, `apply_stale`). A stale held address gives no tracked place
  (`stale_alias`), and the store through it keeps no fact through a fresh pointer local
  (`stale_fresh`). Five more false accepts held the address of an element of a span whose header is
  in memory: a call (`span_flip`, `span_local`), a store through a pointer (`span_alias`), or a
  store straight into a memory local or a global (`span_straight`, `span_global`) changed the header
  with no store into a local. So each such statement makes each held address stale (`writes_memory`,
  `apply_stale`); the earlier cases and the three families keep each count, and no source of the
  tree changes its verdict. The two fort shapes are errors now (`test/lang/fail/ownership/062`).
  Seven FIR cases were false accepts too, because the first statement after `check(overwrite: p)`
  changed `p` before the store: a call into `p` (`call_store`, and `call_value` through a function
  value), a call with a `void` result (`call_void`), a call into the local of a field `p`
  (`store_whole`), a call into the key of `p` (`call_key`), a store through a second pointer to the
  object of `p` (`alias_store`) and a store straight into the global of `p` (`global_alias`). A rule
  for the first statement closed them, and three more FIR cases stayed open: a statement or a `goto`
  between a check and its store (`mid_store`, `goto_store`), and a store with no check
  (`lone_store`). So the pass proves each store that needs a proof, at the test of D17.11 after the
  kill of its call and before its own kill rules (`judge_stores`, `apply_facts`), and a check takes
  the verdict of each store into its place that a path from it reaches before another store into
  that place (`guards_proved`); a guarded store that needs no proof leaves the check unproved. A
  store into a temporary or into `_0` needs a proof, and each tracked place in fixed storage of a
  temporary, or of an `_0` that is no aggregate, has its fact at the entry
  (`place.is_empty_at_entry`, `run_facts`), the state `S = {null}` of design 4.4. A store into fixed
  storage of an aggregate `_0` needs none unless a check stands right before it, and FIR text that
  stores over a live aggregate `_0` is the first of two shapes outside the guarantee: an entry fact
  for it rejected fort source, because an `own` span or `string` result is memory and each call
  before `return` kills its fact. The entry zeroing needs none while no statement of `bb0` before it
  writes its local (`mark_written`). A temporary or `_0` of an `own` reference type that is no
  memory is a scratch local (`place.is_scratch`): a store into the whole local takes one bit of the
  scratch dataflow (`run_scratch`) and no fact, adds nothing to the first count of the work, and
  past the limit is no error. So FIR text past the limit that stores over a live scratch local is
  the second shape outside the guarantee: past the limit, the store in a loop of ne19
  (`test/fort/owner/prover_test.ft`) is proved, and its run writes over an allocation. The lowered
  FIR past the limit stays sound by D17.8, and a scratch dataflow past the limit would not help: its
  sets are the second count, which alone puts 060 `big` past the limit. When the first count took
  each store into a temporary, a function of 800 calls `take(new(u8))` passed the limit with 800
  errors that named temporaries `_N`; 5000 such calls now measure 10001 and compile (`write_scratch`
  in `test/fort/owner/prover_test.ft`). The lowering puts the right side before the check and the
  store first, so each store of the lowering has the verdict of its check. The place text of a
  report comes from the source: the pass finds the assignment at the position of the `=` in the tree
  of the function and takes the text from its first token to the `=`, with a `(` before it when the
  text needs one. A FIR text with no tree gives the FIR place. The emitter (`gen_fir.check_fir`),
  `--check` and `--lib` (`driver.overwrite_entry`), and `--fir` and `--fir-after`
  (`driver.fir_entry`) run the pass and report each check and each store that it does not prove with
  `owner.report.write`, once for each location; a function with such an error reaches no later pass,
  and the run exits 1 with no module, no interface file and no FIR. Past the work limit the first
  error of a function takes the note `owner.report.LIMIT_NOTE` (`fail/ownership/060` passes the
  limit with 1000 checked additions). `--check` skips a function whose check names a runtime entry
  that `std.rt` lacks, because the verifier would panic and a build reports the function. A
  `check(overwrite: p)` names no runtime entry, so the empty runtime of a test needs none for it.
  `std/rt.ft` keeps `fail_overwrite` with no row in `runtime_sig.ft`. bootstrap-1 (`0fe521c`) emits
  a call of it for each store into an `own` reference. It builds bootstrap-2 with the `std` of
  bootstrap-2, which keeps the function. The later pins of `tools/bootstrap.ref` emit no such call,
  and the last pin builds HEAD with HEAD's `std`. So the build no longer needs the function in HEAD's
  `std`, and `run/stdlib/082` and `run/modes/022` still call it.
- **The emptiness pass proves part 1 of the leak rule, and each exit that it cannot prove is an
  error** (D17.20, `spec/fir.md` 11, Exits). `owner.prover.prove` lists in `proof.exits` each `dead`
  of a named owner local outside an abort block, and at each `return` each parameter and temporary
  of an `own` reference type (`find_exits`), and proves each with the fact of its whole local, or
  the scratch bit of a scratch local (`judge_exits`). `proof.leaks` counts the exits that it does
  not prove, apart from `proof.unproved`, and `prover.has_error` holds when either count is not 0.
  The emitter (`gen_fir.check_fir`) and the driver (`run_emptiness`) stop such a function in every
  mode, under `--check` and under `--fir`: `report.write` writes the errors of D17.11 of the
  function, and then `report.write_leaks` writes the error of the leak rule and its note at each
  exit, once for each location and owner local. The bootstrap fixed point compiles the compiler with
  itself, so a leak in `src/fort` or `std` stops the build of stage3. To measure a change of the
  pass, run `fort --check` over each entry of the corpora with main and with the change, and compare
  the status, the stderr and the stdout. A fifth dataflow (`run_ended`) gives a named local an exit
  at a `return` where its scope can be open; it runs only in a function with a named owner local.
  The lowering ends each named local before each `return`, so only FIR text has such an exit
  unproved, and without it the oracle test finds a false accept in FIR text that returns with a live
  named local and no `dead` (row D17.20-return-named of `tools/mutations/oracle.json`). The leak
  work adds the statements and terminators times the owner locals that are no scratch local
  (`proof.leak_work`), and past 2^20 no exit is proved but that of a scratch local; the first error
  of the leak rule then takes `owner.report.LEAK_LIMIT_NOTE`, which names no limit of D17.11.
  `proof.work` keeps the count of D17.11, so a function past the leak limit alone keeps its
  overwrite verdicts. **A change to `owner.prover` can move the work of a compiler function past the
  maximum that D17.11 states.** With the dataflows inline, the work of `prove_within` passed it, so
  `run_dataflows` holds them. After such a change, build a compiler that prints `proof.work` and
  `proof.leak_work` of each function, run `--check` with it over the entries of the corpora, and
  compare the largest of each corpus with D17.11. The error names the declared name of a named local
  or a parameter and the source text of the expression of a temporary (`report.write_owner`). The
  temporary option `--leak-report` implies `--check` and lists on stdout only the leak sites that
  are no error yet, the sites of part 2 (the next entry); it refuses `--json` and `--index`, which
  write to stdout too. A leak at `continue` always comes with the
  error of D17.11 at the declaration of the next round, because the back edge carries the live local
  there. The pass follows the edge of `while (true)` that no run takes, so the `dead` before the
  `return` of `spin` in `test/fort/owner/prover_test.ft` is unproved; D17.20 keeps that obligation,
  and `test/lang/fail/ownership/072_leak_at_an_exit_that_no_run_reaches.ft` holds the error at the
  closing brace. A `case` clause ends at its last token, so its `dead` markers and their errors
  stand at the `;` of its last statement or at the closing brace of a block that ends it
  (`fail/ownership/069`). Time a change of the pass with
  five interleaved `fort --check src/fort/main.ft` runs of main and of the change
  (`notes/environment.md` 1); the limit is +0.10 s (`spec/ownership-design.md` 9).
  A memory owner is unknown after each call, so its `del` must come after the last call of the
  function. Register the `defer del` of a memory owner before every other `defer`, so that it runs
  last, after the last call (D17.20; `test/lang/run/stdlib/053_io_open_errors.ft`).
- **The emptiness pass proves part 2 of the leak rule, and only reports it** (D17.20,
  `spec/fir.md` 11, Inner owners). `owner.prover.find_inners` lists in `proof.inners` each `del` of
  a `T* own` and each `cast` that carries `own` from one to a pointer to another type, where `T`
  holds an inner owner (`place.count_owners`), and `judge_inners` tests the fact of the path of each
  inner owner (`place.inner_path`) right before the statement. `proof.inner_leaks` counts the sites
  that it does not prove, and `prover.has_error` does not read it, so no build and no `--check`
  changes its verdict; `driver.overwrite_entry` writes the lines of `report.write_inners` to stdout
  under `--leak-report` alone. The inner owners take facts of their own, so a function with such a
  site and with no check, no store and no owner local gets a proof too. The inner work has its own
  limit over the leak work, so a function whose inner owners pass 2^20 keeps the verdicts of
  D17.11 and of part 1, and its sites of part 2 are unproved. Each inner owner of each site takes a
  fact of its own with no search (`collect_inner_facts`), and one walk of the type gives each path
  (`place.put_inner_paths`): a search for an earlier fact of each owner took the square of the
  owners, which the inner work does not count (`the_proof_of_part_2_is_linear_in_the_inner_owners`
  in `test/fort/owner/prover_test.ft`). Each path copies its steps, so the inner work counts the
  steps of the paths (`count_inner_steps`) and not the owners alone: the owners at the end of a
  long chain of structs take the owners times the depth in memory. The layout of each struct keeps
  its count of inner owners and their depth (`types.type_layout_struct`, `types.type_owner_count`,
  `types.type_owner_depth`): a walk of each field of each struct took 2^n steps for a chain of n
  structs that each hold two fields of the struct below
  (`the_inner_owners_of_a_doubling_chain_take_one_walk`). **A `del` of a place that is no
  whole pointer local is always a site that the pass cannot prove** (`del(n->left)`,
  `del(v->items[k])`, `del(*pp)`, `del(s.next)`), because D17.11 tracks no place through it, and a
  call before the `del` kills the facts of the inner owners, which are reached through memory: move
  each inner owner into a local and `del` it first. A deferred `del` expands at each exit with one
  location, so the report gives one line for each location, at the first inner owner that the pass
  does not prove. The place text comes from the tree: `report.find_release` finds the call of `del`
  whose name stands at the location of the FIR `del`, or the `cast` that starts there. To count the
  sites, run `--leak-report` over each entry of the corpora and group the lines by the directory of
  their file; the counts stand in the ticket of the change.
- **`del(s, d)` drains a span of owners, and `fir.release` decides each drop** (D17.9, `spec/fir.md`
  9.4). `check.check_del` takes one or two operands. A `del(s, d)` whose operands hold joins
  `check.drain_count`, and a one-operand `del` of a span of owners (`check.is_span_of_owners`)
  joins `check.span_del_count`, which `--span-del-report` writes (`driver.write_span_dels`).
  `fir_lower.drained` writes the drain as ordinary FIR, so the verifier, the emptiness pass, the
  interpreter and the translator need no case for it. When the closure holds a drain,
  `gen_fir.emit_program` and `driver.overwrite_entry` scan the FIR of each lowered function into a
  `release.table`, solve it after the last function and report each drop that is not
  release-only; a closure with no drain scans nothing. The bit reads FIR and not the tree, so the
  print family breaks it through `std.rt` and `write`, and the `null` and `{}` of an `own`
  reference count as constants through the one temporary that holds each (`release.fills_of`).
  The table holds the functions of the unit only, so a drop that calls a function of a used
  library is not release-only (D9.11). `src/fort/fir/release.ft` stands outside
  `src/fort/owner/`, whose cap is 8,000 lines (`spec/ownership-design.md` 9). A drain through a
  pointer, `del(p->items, d); del(p);`, leaves part 2 unproved at `del(p)`: the calls of the drain
  kill the fact of `p->items`, which is reached through memory. Move the span into a local first,
  as for each other inner owner. For the same reason a drain of a field of a memory local leaves
  the field unproved at the exit of the local: keep such a value in an allocation that a
  `drop_made` ends, as for each other memory owner (`run/ownership/076`, `holder_drop`).
- **The emptiness pass proves part 1 for the locals of an owning aggregate type, and each exit
  that it does not prove is an error** (D17.20; `spec/fir.md` 11, Aggregates). `owner.prover` lists
  their exits in `proof.aggregates` with the scan of the owner locals (`scan_exits` with
  `aggregate`), gives each owner below each such local with an exit a fact of its own
  (`collect_aggregate_facts`, `place.put_local_owners`), and `judge_aggregates` tests them;
  `proof.aggregate_leaks` counts the exits that it does not prove, and `prover.has_error` reads
  it. `report.write_leaks` writes the error of each such exit after the errors of the owner
  locals, once for each location and local (`report.write_aggregates`), and names the first owner
  that the pass does not prove (`write_aggregate_owner`). A function whose only owners stand in
  such locals takes the full proof and not the light survey of rule (b) alone. The aggregate work
  has its own limit over the inner work, so a function past it keeps every other verdict, and its
  first aggregate error takes `report.AGGREGATE_LIMIT_NOTE`. Before the error, the option
  `--aggregate-report` listed the exits with a cause each, which two more runs of the facts
  dataflow gave (`prover.prove_causes`); the option and the causes went with the error, and the
  measure of the report stands in `spec/ownership-design.md` 6.3. Most sites were memory locals
  after a call: a call kills each fact of a memory local, so a deferred `free(&b)` that a later
  deferred call follows leaves `b` unproved (`main.c14` and `main.c26` of
  `test/fort/owner/prover_aggregate_test.ft`).
  The time that the change added to `fort --check src/fort/main.ft` is that of the functions that
  the full proof takes on: a variant that proved the aggregates only in a function with other work
  of the pass, `has_owner` without `is_aggregate_local`, added none. Measure such a variant with a
  copy of `src` in which `sed` makes the change, a compiler built from the copy with
  `build/Linux/debug/fort --std-dir build/Linux/debug/std -I <copy>/src <copy>/src/fort/main.ft -o
  <binary>`, and nine interleaved runs of `<binary> --check --std-dir <std> -I src
  src/fort/main.ft` beside the compiler of `origin/main`. Such a function now also gets the leak
  work and the inner work of a full proof, the sets of its dataflows, where it got 0 before. The
  inner work is the work that `driver.overwrite_entry` gives `summary.add`, which charges it to the
  component of the function against `summary.WORK_LIMIT`; V1 gives no line, so the summaries of the
  libraries `std` and `fort` do not change (measured with `--emit-summaries`), but an analysis that
  gives lines reads the larger charge. No refinement gives a line since 2026-11-11: R1 is dropped,
  and R2, R3 and R4 are retired (`spec/ownership-design.md` 8).
- **The emptiness pass proves the stores of a whole value of an owning aggregate, and only reports
  them** (D17.11, `spec/fir.md` 11, Whole stores). `owner.prover.find_wholes` lists in
  `proof.wholes` each assignment whose place has an owning aggregate type, but a store into fixed
  storage of an aggregate `_0`. `collect_whole_facts` gives each owner below each destination a fact
  of its own (`place.put_owners_below`), and `judge_wholes` tests them where the test of a store
  into an `own` reference stands: `apply_facts` is three steps (`apply_reads`, `apply_call`,
  `apply_store`), so the walk tests the owners after the reads and again after the kill of the
  `call`, which gives cause 3. `proof.whole_leaks` counts the stores that the pass does not prove,
  and `prover.has_error` does not read it, so no verdict changes; `driver.overwrite_entry` writes
  the lines of `report.write_wholes` under `--whole-store-report` alone. The lowering writes a whole
  store through `(*_a)` of `_a = addr mut(p)` when the right side has a writer, with no check, so
  `place.is_whole_address` marks such an `_a`, also when the two arms of a conditional store through
  it, and only the test reads it as `p` (`place.whole_source`); the stale dataflow keeps its bit
  (`mark_whole_addresses` lists it with the held addresses). Without that reading `p = new(box); *p
  = move(b);` was unproved, because no gen rule makes a place through `_a` empty. A declaration is a
  store into a named local whose bit in the ended dataflow is set (`mark_ended_wholes`), so the
  facts, which give a named local no entry fact, do not decide it. A call keeps each fact through a
  fresh pointer local (kill rule 5 of D17.11, `prover.survives_call`), and the `addr` of the address
  of a whole store ends no freshness: the store through it ends the freshness of the local of its
  place, after its test and before its kill rules (`place.apply_fresh_end`). So `*pr =
  make_probes(count)` after `pr = new(probes)` is proved, and the marks of these addresses run in
  each function, also past the whole-store work, so that no other verdict depends on that limit
  (`place.mark_whole_addresses`). The grow loop of a vector, `bigger[i] = move(v->items[i])`, has no
  fact, because no gen rule makes an element of a fresh span empty. `place.mark_values` marks once
  each temporary that one store writes and one `move` reads, so a nested zero literal, whose inner
  literals the lowering puts into temporaries, is a zero value (`place.is_zero_rvalue`), and a store
  of a literal makes each owner empty that it leaves out or gives a zero value, at any depth (gen
  rule 7 of D17.11, `place.is_zero_member`, `prover.gen_members`): so after `dataflow{.count = n}`
  the owners that the literal leaves zero have their facts. `test/fort/owner/whole_oracle_test.ft`
  holds each verdict against a run of `fir.interpreter`: a store that the pass proves and whose run
  gives an `overwrite` event at its location fails it. `fir.shadow` gives an `overwrite` the exit
  location of its statement (` exit <line>:<col>`), so each expansion of a deferred store is a site
  of its own, and no event of one expansion marks another (`mixed` in that test). The ticket of the
  change holds the count of the sites by directory and by cause, and the command that measures it.
- **The emptiness pass proves rule (b) of D17.21, and each use that it does not prove is an error**
  (`spec/fir.md` 11, Uses). `owner.prover.prove_uses` runs after the facts, in the same context, so
  it reuses the survey of the places, the stale dataflow, the tests against null and the map of
  `owner.alias`, and it changes no fact: `prove_limits` keeps each line of the emptiness proof.
  `has_error` reads `proof.use_unproved`, and `report.write_uses` writes the errors after those of
  D17.11 and D17.20, in `gen_fir.check_proved` for a build and in `driver.run_emptiness` for
  `--check`, `--fir` and a library build. A function with a local owner and no work of D17.11 or
  D17.20 takes a light survey (`place.count_stores`, which counts no read and marks no held
  address, and none reads an owner), and no stale dataflow when no index temporary takes its key
  from a local. Each block turns into acts once (`collect_acts`), and the fixpoint and the judge
  read the acts and trace no place again. An act traces a place only when its local holds an owner
  in its type, or for a place through a pointer, also through a global, when a memory local holds
  one (`can_name_owner`). A place through a global that holds `&x` can be `x` (`alias.can_be`
  gives the exposure of `x`), and a refusal of each global place let `P = &x; del(x); **P` pass with
  no line. A walk that traced each place of each function took 0.05 s more of
  `fort --check src/fort/main.ft`. The state holds ranks, not bits: the rank of an operation orders
  the `move`, `del` and call acts by their source positions (`rank_ops`), so the least rank of an
  owner names the operation of the note, and a tie at one position cannot name an operation that
  reaches no use. A position alone gave each act of FIR text with no location one value, and the
  note of `b7` in `test/fort/owner/prover_use_test.ft` named a call that a store before the read had
  ended. The values of the state are the owners that `make_owners` makes, each with one more for
  each local key, which the charge of D17.21 counts; a bound that counted a read `b[k]` twice let
  the first pass stop before the limit. The map of `owner.alias` is made only when an act goes
  through a pointer; a map that rule (b) made before the facts and that the facts made again cost
  0.05 s. The error names the expression of the read in the tree of the item (`report.find_read`),
  which matches a `*`, a `.`, a `->` or an index to each projection of the FIR place and skips the
  operand of `&`, `move`, `del`, a comparison with `null`, the target of `=` and the operand of a
  field or of an index into a fixed array, which load no pointer of it. A read that the lowering
  made, such as the element of a `for` over a fixed array, has no expression; the use then names its
  owner in the type of its local (`use.local_owner`), and FIR text gives the FIR place. The result
  `_0` of an aggregate type, which a read through a pointer can reach before its store, is named as
  the call of the function, `make()`. The note of a `move` or a `del` names the place that it
  vacates, the source text of its operand (`report.write_op_place`), and so
  `del(kids[1]); *kids[0]` names `kids[1]` in the note and `kids[0]` in the error. A `return x` of
  an aggregate type whose function runs deferred code moves `x` into a temporary at the first token
  of `x`, not into `_0` (`fir_lower.lower_return`), so `report.returned_by` finds the implicit
  `move` by that position; before, the note of such a `return s` stood at `s` and named the owner
  of the error. The text of an operand or of a read takes each `(` that it needs
  (`report.open_start`): the parser gives no node for a parenthesis, so `del((s).a)` once gave the
  note `'s).a' is deleted here`.
- **A test against null makes the facts of its place hold on one edge** (D17.11 gen rule 6,
  `spec/fir.md` 11, Tests). `owner.place.find_test` reads a block that ends in `switch(copy _c)` on
  a flag, a `bool` temporary that is no memory, and goes back through the last store into the
  flag: an `eq` or an `ne` of a pointer place with a zero constant, or a `copy` or a `not` of
  another flag. A named local is no flag (`bool b = p == null; if (b)` gives no fact, `k13` and
  `fill_flag` in `test/fort/owner/prover_test.ft`): gen rule 6 names a branch on the test. The edge
  is the target of the value that the flag holds when the place is null, and the other value must
  go to another block. `owner.prover.find_tests` finds the test of each block once, and the walk
  that gives the gen and kill sets of the block fills a third set of its probes, the edge set:
  `add_test_facts` puts the facts of the place there right before the test statement, and each
  later statement kills and gens in it as in the probes, so a call, a store through a pointer, a
  store into the place or its pointer local and a store into the flag kill as they kill any fact;
  `run_scratch` does the same for the bit of a tested scratch local.
  `fir_flow.dataflow_forward_edges` joins the out-set of a block with its edge set on that one edge
  (`test/fort/fir_flow_sets_test.ft` holds it against a new block on the edge). A test makes no
  new fact, so the counts of the work do not change. Part 2 also takes the fact of the released
  place itself (`collect_site_facts`, `releases_nothing`), read with the stale set before its
  statement (`b5`): a `del` or an owning `cast` of an empty place frees nothing, so `del(p)` after
  `move(p)`, after `del(p)` or in the arm where a test finds `p` null is proved, and that fact
  counts in the inner work. The `switch` that reads the value of `&&` or `||` stands alone in the
  block where its operands join, so no store into its flag stands there and its edges hold no
  fact; only the right operand gets the fact of the left one (`n14`, `n15`).
- **The kill rules read the borrows and the exposure of `owner.alias`** (D17.11, Borrows;
  `spec/fir.md` 11, Alias; refinement R6 of `spec/ownership-design.md` 7). Kill rule 3 keeps a fact
  of a memory local that the destination of a store through a pointer cannot be, and kill rule 4
  keeps a fact through a pointer that cannot be a place of the memory local of a straight store.
  `alias.make` ranks the locals that are not memory, the borrow locals, and the memory locals whose
  frames are origins (an aggregate `_0` has none); `alias.run` is a worklist fixpoint of its own,
  in the order of `fir_flow`, because its transfer copies the origins of one local into another,
  which no gen and kill set holds. `prover.run_aliases` runs it after the facts are collected and
  before the facts dataflow, and each walk of `apply_facts` moves the state with
  `alias.apply_reads` before the kill rules and `alias.apply_store` after them. A path keeps what
  its last load reads (`place.via_kind`, set by `place.trace` and `put_inner_paths`), and
  `alias.can_be` answers from it. Every rvalue carries the origins of its operands, a comparison
  too, because D10.7 derives through each operation; only an `own` pointer type, a call, `alloc`
  and a constant cut them. Past `alias.WORK_LIMIT` (2^24 of charge, `proof.alias_work`) the map is
  off and each verdict is that of the rules without R6, so the limit turns no verdict into an
  error; the five trees charge 3,037,320 or less (`test/fort/owner/alias_test.ft` holds the
  boundary, 2^24 and 2^24 + 2048, a loop that passes it on a later turn, and a first transfer that
  passes it, which allocates no state). A function with no memory local whose frame is an origin,
  or with no fact, makes no map.
  The time of `fort --check src/fort/main.ft` went from 1.31 to 1.34 s to 1.32 to 1.34 s (five
  interleaved runs). A change that moves a value of an address must keep lemma (vi) of
  `spec/fir.md` 11: a forged pointer is outside it only by D10.7, and an `own` pointer only by
  D17.1.
- **Each dispatch over a FIR kind in `owner` is a `switch` with no `default:`, and each predicate
  has one definition** (D7.7, D17.11, D17.20, D17.21). The 17 dispatches of `owner.place`,
  `owner.prover`, `owner.alias` and `owner.report` over a statement kind, an rvalue kind, a
  terminator kind, an operand kind or a place step kind list each member, so a new kind is a
  compile error at each of them. An arm that does nothing names its kinds and gives the reason in
  one comment line. A loop that acts on each projection of a place and a test of the callee of a
  call are dispatches too: `alias.add_indexes`, `alias.add_address`, `place.mark_readers`,
  `prover.pointer_acts`, `prover.statement_acts` and `summary.add` each take a `switch` over the
  projection kind or the callee kind. One other test of one set of kinds with no `else` is a guard
  and stays an `if`.
  `place.is_call` is the one trigger of the call effect, which has two results: the kill of facts
  (`prover.apply_call`, kill rule 5 of D17.11) and the vacate (`prover.vacate_memory`, rule (b) of
  D17.21). `place.is_zero_store` is the zero value, with a null survey for the entry zeroing.
  `place.compared_place` is the null test, `place.is_aggregate_result` the `_0` predicate and
  `place.is_moved_once` the moved temporary. `place.is_leaving` is the scan for the step at which
  a path leaves its object, from a first step that the caller gives. `place.find_operand` gives
  the operands of a terminator to the fresh reads and to the acts of rule (b), and
  `place.step_type` gives the type behind a step. A new dispatch takes a `switch`. A new user of
  one of these predicates calls it and writes no copy: before this rule the zero value had 6
  definitions and the trigger of the call effect 4 tests.
- **Free an object with owners in a form that part 2 of the leak rule proves** (D17.20,
  `test/fort/owner/release_test.ft`). A test against `null` proves a place empty on its edge (the
  entry above), so a `del` of a local, a parameter or a field that a loop or an `if` finds null
  frees nothing and is proved (`local_walk`, `chain_free`); a call between the test and the `del`
  kills the fact of a field, which is reached through memory (`walk_then_call`). Two forms of a free
  that has owners to free pass, and `release_test.ft` holds both. First, a loop that frees a chain
  tests the place in memory (`while (h->last != null)`), moves the link and its tail into locals,
  frees the link and then stores the tail back (`containers.pool_drop`). Second, an object whose
  owners a call frees moves out whole into a local that is not memory (`module kept = move(*m);
  del(m);`), and a second local takes it for the call (`modules.free`). Since refinement R6 the
  second local is not needed: a store into a memory local keeps each fact through the `own` pointer
  `m`, which holds an allocation (`memory_free` in `release_test.ft`). A vector of objects with
  owners keeps a chain of owners (`made_before`, `last`) beside a span of views (`ast.arena`,
  `types.table`, `modules.owned_modules`, `lsp.json.doc`), and the first form frees it: a slot of a
  new span of owners is null, but no rule proves it, and refinement R8 (`del(s, d)` of D17.9) adds
  no fact, so its `del` before a store is a site. Link a new object into the chain before any
  call, because a call kills the facts through the new pointer (D17.11). A chain frees its objects
  from the newest. With the chains of the AST arena, the type table and the module vectors
  together, a heap probe of repeated runs reaches its steady size later: take its early sample
  after that (`driver_lifetime_emitter_test.ft`, `notes/testing.md`).
- **Write a read after a call in a form that rule (b) proves** (D17.21,
  `test/fort/owner/prover_use_source_test.ft`, the pairs `*_before` and `*_after`). The proof
  takes each call as a change of each memory owner, and of each owner of an aggregate `_0` before
  its store, so a read of such an owner after a call is a use that it does not prove. Ten forms
  remove such a use, five in the compiler and five more in the tests, and none adds a
  `move`/store pair. Split: the function keeps the owner local
  and its cleanup, and a function that takes the local as a pointer does the work after the first
  call (`check_stmt.check_range_in`), where the owner is no local owner (X2 of
  `spec/ownership-design.md` 6.2). Fill: a function that returns an aggregate with an owner
  allocates it with no load through a pointer after a call, and a function that takes its spans
  fills it (`driver.cc_command_fill`); a fill through a pointer into a new `own` field fails the
  overwrite check of D17.11. Narrow: the owner local and its calls go into a function that returns
  a plain value (`summary.intern_symbol`). Read early: a load through a pointer comes before the
  first call or before the owner exists (`driver.run_entry`). Method: a direct read of an `own`
  field of a memory local goes through a function that takes the struct (`prover.start_walk`).
  Accessor: a one-line function takes a pointer and returns one read (`node_at(&g, 0).call_count`
  for `g.nodes.items[0].call_count` in `test/fort/gen_fir_summary_test.ft`,
  `fir.module_function_at`). Parameter: a loop or a check over a value that a call gave, which can
  borrow `exposed`, goes into a function that takes the value (`spot_check(strbuf.view(&buf))` in
  `test/lang/run/stdlib/054`, `expect_param` in `test/fort/fir_local_test.ft`), or into a function
  of `std` (`str.index_of`). Part: the rest of a test whose owners lend their addresses goes into a
  function that takes them by pointer; the `run` of a support module (`check_env.run`,
  `modules_env.run`) makes the value, passes its address to the test and frees it, and a panic
  test passes it to `trigger`. Owned string: a builder returns a `string own` whose address no call
  takes, in place of a buffer that the test lends. One initializer: a fixed array of owners takes
  each element in its initializer, so no `del` of one element comes before a read of another
  (`test/lang/run/ownership/016`). Read early and one initializer are no forms of X2; the
  coordinator ratified both as simpler forms that remove the use.
- **End an owning aggregate local in a form that part 1 of the leak rule proves** (D17.20,
  `spec/fir.md` 11, Aggregates). The pass reads no callee, a call kills each fact of a memory
  local (D17.11 kill rule 5), and the `dead` markers of a scope come after each of its deferred
  statements (`spec/fir.md` 9.5), also after the move of a `return` operand (9.6). So each owner
  below the local needs a `del`, a `move` or a test on null after the last call of the exit. A
  call that takes the local by `move`, as `drop(move(x))`, kills the facts of `x` too when the
  function takes the address of `x` anywhere. The end form is the drop form (the user, 2026-11-02,
  "Drop form everywhere"): each owning type ends through a drop that takes its value by `move`
  and holds each release of the type once, and the free by pointer moves its value into the drop.
  No end frees a value and then dels an owner that the free releases, and no end loops on a chain
  and calls its free in the body: that `del` or that loop hides a release that a mutant deletes
  from the free. Five forms remove a site.
  Del: a free that only dels the owners of its parameter becomes the `del` of each owner, when no
  free of the local runs before the end (`defer del(buf.data)` for `strbuf.free(&buf)` in
  `std/io.ft`).
  Drop: a value whose address no call takes ends with the drop of its type (`fir.func_drop`,
  `scope.drop`). A drop moves each field whose type has a value drop into that drop
  (`strbuf.drop(move(g.out))`, `fir_flow.bits_drop(move(c.origins))`), dels each other owner of its
  value, and drains each chain in a loop, in the order of the free that it replaces, so each release
  of a type stands once, in the drop of the type. No function of src/fort, src/lsp, std, test/fort,
  test/lsp or test/library, a drop or another, dels the storage below a field or an element of a
  span whose type has a value drop, through a parameter or a local: a local whose type holds such a
  field and that the function lends to calls lives in an allocation and ends through the drop_made
  of its type (`gen.gen_args_make` in `gen.gen_main`, `prover.probes_drop_made`). A fixed array of
  such values that a test lends to calls does the same (`fir_env.sets` in test/fort/support): a call
  kills the fact of a `move` out of a memory local, so a drop of each element there leaves a site.
  An owned span whose element type has owners (`new(fir_flow.bits, n)`) gives no site when the
  drops of its elements go, so use an allocation with a drop; the aggregate leak error will decide
  this case. test/lang stays outside the scan: a `del` below a field is valid fort, the corpus shows
  the language, and a change there moves the oracle events of fir-run. ctest `drop-fields`
  (`test/drop_fields_test.py`) holds that count at 0 by a scan of the text, with a control tree. The
  scan reads each function at the top of a file and each function in each branch of a module `$if`,
  with its parameters, its locals and each `del`; it reads the code of a line without its comment
  and with each literal empty. It follows a `del` through fields, `->` and elements of spans. Its
  docstring names what the scan cannot see. The parameter of a drop is a local whose address no call
  takes, so no call kills its facts; a drop that takes the address of a field makes its parameter a
  memory local, and its end a site. The free by pointer moves its value into the drop
  (`gen.gen_free`, `fir_parse.parser_free`), or keeps the fields that its callers read after it
  (`diag.free` keeps the text mode for `diag.reset`, `lsp.json.free` the limit), or frees each field
  through the free of its type, which reaches the drop of that type (`session.free`).
  Allocation: a local that the function lends to calls lives in an allocation of `make`, whose
  `own` pointer no call changes, and `drop_made` moves the value out, dels the allocation and
  drops the value (`scope.make` and `scope.drop_made` in `check_stmt.check_block`,
  `containers.pool_make` and `pool_drop_made` in `driver.run_cc`, `driver.compile_entry`). A `make`
  that fills the value stores into its fresh `new` (`prover.context_make`, `fir_parse.parser_make`):
  the pass knows the owners of the allocation empty after a `new`, and a store after the call of
  a `make` is the overwrite error of D17.11, because the pass reads no callee.
  Order: the deferred statements run from the last, so write the `del`s first and the drops last
  (`driver.run_cc`). A memory local that a function declares after an allocation ends in a block
  of its own, so that its `del` comes before the call of the drop (`driver.tokens_entry`).
  Split: a function that returns a local holds no deferred call after the move of its result, and
  a helper holds the other locals (`fir_parse.parse_into`, `prover.prove_into`).
  The old end form, a free by pointer and then a `del` of each owner or a loop on `!= null` that
  called the free again, hid each release that a mutant deleted from the free. A drop that called
  its free and then took a `del` of each owner again hid them the same way: the lifetime tests
  caught none of 5 such deletions, and the mutation rows D13.5 to D13.5c of
  `tools/mutations/emitter_fort.json` survived (PR 206, review round 1). A drop that called the free
  in a loop hid a free that releases one block of a chain (`containers.pool_drop`, review round 2),
  so each drop drains its chain itself. The drop form replaced the old end form at the 32 ends that
  `src/fort` and `src/lsp` held at c04d6836. Before it, 44 of 78 deletions of a release of the frees
  and drops of those ends turned a test red; after it, each of 103 did
  (`test/fort/support/lifetime.ft` gives the run).
  No form stores the zero value over a live aggregate: `x = T{}` quiets the error of the leak rule
  and leaks, and until the migration of the whole stores ends, D17.11 only reports such a store
  (`--whole-store-report`).
- **The ownership summaries come from the call graph of the proved functions**
  (`spec/ownership-design.md` 8, `src/fort/owner/summary.ft`). `summary.add` takes each function
  that the emptiness pass proves with no error, with `proof.inner_work` (the largest of the three
  counts of the proof) and the callee symbol of each `call fn`; a call through a function value,
  a call of an extern and a call of a function that the graph does not hold add no edge.
  `driver.overwrite_entry` adds them under `--check`, `--use` and `--lib` to a graph that its
  caller owns and solves it after a run with no error (`test/fort/driver_summary_test.ft`), and
  `gen_fir.check_proved` adds them in a whole program, whose `gen_fir.emit_program` solves the
  graph (`driver.emit_module` passes one only when the build names no `--lib` and no `--use`, so
  no build adds a function twice). `summary.order` gives
  the strongly connected components callees first (Tarjan, with a stack of its own: a chain of
  2000 calls in `test/fort/owner/summary_test.ft`); the order is a function of the order of `add`
  and of each body. `summary.solve` runs the rounds through `start`, `give` and `end_round`, which
  the tests drive by hand with lines of each kind; the work of a component passes
  `summary.WORK_LIMIT` (2^26) only when it is greater, and a sum stays at the largest u64. Past it,
  `summary.set_safe` gives each member the safe value of each kind: no must line, and
  `result _k` and `keep _k` for each parameter `k` (`node.params`, the `lk_param` locals). V1 has
  no kind of line: `summary.infer` finds none and charges the work that `add` recorded, so each
  component takes one round and each table is empty. **A refinement that adds a kind changes
  `infer`**: it must run the prover of the member in each round with the lines of its callees, so
  the prover must run in the order of the components and keep the FIR of a component while its
  rounds run; today each function is proved once, in module order, and freed. The text form is
  `summary.write` and `summary.parse`, which accepts only a text that `write` writes
  (`test/fort/owner/summary_text_test.ft`); it holds the form of a line and not its meaning, so a
  refinement checks each `k`, field and index against the declaration. `src/lsp` runs no check
  yet, so it computes nothing.
- **The FIR interpreter runs the FIR of a closure before the emptiness pass**
  (`spec/ownership-design.md` 3, `src/fort/fir/interpreter.ft`). It is the reference semantics of
  the ownership checks, and `--fir-run` and the pass `run` of `--fir-test` start it
  (`toolchain.md` 1). It runs FIR as the lowering makes it, so each check of the checked mode is
  there and `check(overwrite: p)` continues. The rules a change to it must keep:
  - The address space has regions: each global, data constant, function, frame and allocation
    takes a new range above `FIRST_ADDRESS` (65536), and no address comes twice. A frame region
    holds each local of one call at the offset of its type. A load or a store finds its live
    region, so a pointer is an address and `cast` to `u64` keeps it (D3.14); an access below
    `FIRST_ADDRESS` is the fault `null dereference`, and one outside each live region is
    `invalid access`. A constant and the bytes of a string constant are read-only (D7.10). The
    bytes are little-endian and the layout is that of `types.type_layout_struct`.
  - The order is the order of the translator (`spec/fir.md` 6, 7, 12.5): the operands in order,
    a `move` zeroes its place after its read, an aggregate reads each member before its first
    store, and the address of a destination comes after the right side. A positional `aggregate`
    writes each member and keeps the padding of its destination; `aggregate zeroed` zeroes it.
    A `live` gives each byte of its local the value 0 (D10.1), so run 2 of a loop body reads no
    byte of run 1 (`test/fir/run/010_live_zero.fir`).
  - A call reads each argument before it pushes the new frame, so a fault in an argument names
    the call and not the callee. It copies each argument into the parameters of the frame, the
    caller-made copy of `toolchain.md` 6 item 7, and the result goes from the `_0` of the callee
    to the destination after the callee ends. The frames stand on an explicit stack whose limit
    is 8 MiB (`STACK_LIMIT`), and a push past the limit is a fault. Each frame counts 16 bytes
    (`FRAME_BYTES`) and none of its locals: the `-O1` of a checked build keeps in a register
    each local that has no address, so a native frame is often 16 bytes, the least frame of a
    call on both targets. `r(n - 1) + 1` takes 16 bytes a frame natively and completes at
    depth 522,931; a count of the 56 bytes of its FIR locals stopped the run at depth 116,506,
    and a count of its two allocas would stop it at 262,144. The limit is approximate in the
    other direction: a native frame that holds an array or a spill is larger, so a deep
    recursion of such frames ends natively by SIGSEGV and here runs on, and a call that the
    native build inlines or turns into a jump takes no native frame. A fort function that no
    module of the run holds is lowered when the run calls it first, and the verifier tests it
    then.
  - A load gives the bits of a value as native code gives them. An integer narrower than 64 bits
    is zero-extended, and a `bool` keeps only its low bit, because `toolchain.md` 6 item 2 loads
    an `i8` and truncates it to `i1`. A bound of `slice_ptr` becomes 64 bits by its own
    signedness, as `fir_llvm.bound_value` makes it.
  - A failed check calls the runtime entry of its kind with its reported operands and the file, the
    line and the column of its terminator, `alloc<T>(n)` calls `std.rt.alloc` with the location of
    its statement, and `del` calls `std.rt.release` with its pointer and the location of its
    statement and then zeroes its place (`spec/fir.md` 12.2, 12.4). So the message of a runtime
    error is the message of `std.rt`, byte for byte.
  - An extern of the list of `spec/ownership-design.md` 3.2 runs in `call_model`, from `std.rt` or
    from the program: `exit`, `abort`, `write`, `calloc`, `free`, `isatty`, `strlen`, `memcpy`,
    `memmove`, `memcmp`, `__errno_location`, `__error`, `strtod` and `strtof` with a null end
    pointer, and `snprintf` with the one form of `std.libc.snprintf_float`. The model externs
    `oracle_keep`, `oracle_pass` and `oracle_call` serve the tests. Each other extern, each other
    form, and each function with no body, ends the run as `outside`. A `calloc` that the C
    library of the compiler refuses gives the program null, as a native `calloc` does. The errno
    of the compiler goes to the errno of the run only after a call that sets it.
  - The run does not run the allocator `std.heap`: its `mmap` is no extern of the list. `run_heap`
    models `take`, `drop`, `is_start`, `can_take` and `live_bytes`, which it finds by symbol
    (`HEAP_NAMES`).
    `take` makes a region of the kind `rk_heap`, and `drop` changes its kind to `rk_dead`. A dead
    region keeps its entry and its bytes while the run lasts: an access to it reads or changes
    the bytes that it held last, and the shadow reports `use after release` (D10.9 rule 2). A
    `calloc` makes a region of the kind `rk_c`, which only the `free` of C ends. The start test of
    `drop` (`is_start`) reads the kind, so a `del` of an allocation of C and a `free` of an
    allocation of the runtime both fail. The result of a modelled function derives from its
    argument, as the result of an extern does (D10.7). A function of `std.heap` with no model
    runs its body, and the run ends as `outside` when that body reaches `mmap`.
  - The shadow state `fir.shadow` (`src/fort/fir/shadow.ft`) observes each run, with or without
    `--fir-events`, and changes no outcome. The interpreter calls it at each access (`reach`),
    load, store, move zeroing (`zero_bytes`), `del`, `live`, `dead`, frame push and pop, `calloc`,
    release and global. The interpreter drops the entry of a region that ended after 4096 of them
    (`COMPACT_FLOOR`), so the shadow keeps its own ranges: every allocation, live or released,
    and each frame that ended after it made a frame tag. The shadow runs in each run, so it adds
    to the time of each run; `test/fir_run_test.py` prints the time of the run corpus.
  - The owner count of an allocation counts its owner slots and its owners in transit. A `move`
    or a `del` of an owner slot puts the owner in transit and the next typed store of an owning
    word (`settle`) takes it, so a transfer keeps the count; the test at the end of each statement
    and terminator ends the owners still in transit and reports a count of 0 as a `leak`. A
    `return` tests with `_0` in transit, and the items of the `std.rt.release` that a `del` calls
    test nothing, so an owner inside a freed object leaks at the `del`.
  - The test of `leak at exit` (`test_exit`) tags an allocation as of a cycle or not. It counts,
    for each live allocation that no global reaches, the owner slots inside such allocations that
    hold it (`range.holders`). It then ends the slots of each allocation with a count of 0 and
    repeats. Each allocation that this reaches hangs below a leaked allocation with no holder, and
    its event has no tag. Each allocation that keeps a count above 0 is in a cycle of owners or
    below one. Its event has `cycle` set (` of a cycle` in the line). `cycle` is a part of the
    key that makes an event unique, so one run gives at most one line of each sort. The cases
    `SELF`, `BELOW`, `LONG` and `BOTH` of `test/fort/fir/shadow_test.ft` hold the four shapes.
  - The tags of a value stand in `sh.value` while it is in flight and in the record of its word in
    memory. A value holds one owner tag or lend (`o`) and one frame or moved tag (`f`), and a
    binary rvalue keeps each part of both operands; of two owner tags it keeps the first. A load
    gives a copy of an owner slot of the frame that runs its owner tag, and a store of a word other
    than 8 bytes keeps no tag, so a comparison drops it. The tags of an aggregate, an argument and
    a result go through the scratch buffer of the machine, in `sh.scratch`, and `memcpy` keeps
    the tags of the words that it copies. An extern reaches memory through the tags of the
    argument whose value starts the access (`sh.args`). Of such arguments, the one with the
    lowest index that has a tag gives the tags: in `memcmp(laundered, p, 1)`, where a callee
    cast away the tag of the first argument, the access takes the owner tag of `p` (case
    `args_owner` of `test/fort/fir/shadow_test.ft`).
  - The derivation of D10.7 stands in the `d` of each tag (`shadow.derivation`): the ids of the
    entries of the locals whose addresses the value derives from. An `addr` or a slice of fixed
    storage gives the id of the entry of the base local of its place (`tag_address`), which the
    first such address makes and the next `live` ends (`revive`); the entries of a frame end with
    it. A binary rvalue joins both derivations, a load joins those of the words that it reads, and a
    store adds its derivation to each word that it writes; `cut` keeps the derivation of a word that
    a store writes in part. A `move` keeps the derivation and drops the other tags. A place that
    loads no pointer is `named`, and the interpreter marks `sh.place` named before it reads an index
    local, writes the parameters of a new frame, or reads the `_0` of a returning frame. So a change
    that adds an access must set `sh.place` first, or the access is tested against the last address.
    An access of a local of a frame that stands, through a value whose derivation has no id of the
    entry of that local, is `forged` (`is_forged`), and the run reports no event after it. The
    arguments of a runtime entry keep their derivation (`write_values`), an owner slot keeps it
    (`claim`), a `dead` keeps that of a word that it shares with a live local (`cut`), and a `del`
    tests the access of its whole place. Three limits give fewer events than D10.7: a value that
    derives from more than 4 entries (`DERIVATION_IDS`) derives from each; a byte of padding counts
    as a byte of the local before it; and a word keeps its derivation until a store writes the whole
    word, so a `u32` local keeps the derivation of an earlier value (case `copy_partial_word` uses
    new locals for this reason). One gives more: the bytes that `snprintf` writes derive from no
    address, and the results of `memcmp`, `strlen` and `strtod` derive only from their arguments
    and not from the bytes that they read, so a value that C computes from such bytes loses their
    derivation.
  - Four known limits. A `use after move` needs an access below `FIRST_ADDRESS` (65536): a field
    far past a null base, or an index of 65536 bytes or more, faults as `invalid access` with no
    event. `ended` keeps the range of each frame that made a frame tag, one for each such call,
    so a run of many such calls holds as many ranges. A `memcpy` of a frame tag of the frame that
    runs into a global does not keep it, while the same copy by a fort callee does. And a copy of
    bytes that covers a word only in part gives the destination word the whole tag of the source.
  What it cannot see: a program whose native build has undefined behavior (D10.7) runs another
  way or ends in a fault, and a value that depends on an address, such as `print_ptr`, differs
  from the native one.
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
- **Each copy of an aggregate is `llvm.memmove`, and the translator writes no `llvm.memcpy`**
  (D6.3, D19.3). `llvm.memcpy` of two ranges that overlap in part has no defined result, and a
  view cast gives a program two such places (D3.14). `fir_llvm.copy_to` writes each copy. It has
  2 callers: `fir_llvm.hold`, which fills a `%tmp` slot for an aggregate operand that is a
  place, and `fir_llvm.write_held`, which writes a held operand into its destination.
  `gen.gen_memmove` writes the call. The call and the declaration take the name from one
  constant, `gen.COPY_INTRINSIC`. The rule has no premise: the translator does not prove that
  two ranges are apart, and the copy into a `%tmp` slot and the copy out of it are
  `llvm.memmove` too. A proof for that slot does not hold today. A pointer of the program can
  name a range that overlaps a slot in part. One such pointer is the address of a parameter,
  which is an argument slot of the caller, after the frame of that caller ends. A second is an
  element that an index out of range names under `--no-bounds-check`. A probe of each gave a
  wrong `llvm.memcpy` copy into a slot at `-Xcc -O0`. `run_tests.py --verify-ir` fails a module
  that calls or declares `llvm.memcpy`, so each module of the language corpus and of the module
  tests holds the count 0 (`notes/testing.md` 6). `test/fort/fir_llvm_copy_test.ft` and
  `test/fort/gen_data_test.ft` hold the text of each case. `test/lang/run/casts/021` to `024`
  hold the result of a store through two views at `-O0`, `-O1`, `-O2` and under `--release`.
  They link `test/lang/ffi/memcpy_overlap.c`, which stops the run when the C `memcpy` gets two
  ranges that overlap. The interpreter gave that result before the translator did. The frozen
  C compiler in `bootstrap0/` keeps `llvm.memcpy` for each copy.
- **One signature map writes every definition, extern declaration and call** (T-251).
  `gen.signature_of` maps a fort function type to the result type and its extension attribute,
  the `sret` type, each parameter with its attribute, and the variadic flag of an `extern fn`.
  `gen.gen_define`, `gen_data.emit_extern`, `gen.gen_main` and `fir_llvm.call_of` all read it
  (and `gen_expr.gen_call` of the direct path did, until T-255 deleted it), and
  `gen.gen_call_sig` writes each `call`. A fixed argument takes the attribute of its parameter,
  and an argument of the variadic tail none. The move did
  not change the direct path's text: `tools/ir_snapshot.sh` of `main` and of the branch gave an
  empty `diff -r` over 427 run tests in three modes. The calls that the translator makes itself,
  `std.rt.alloc`, `std.rt.release` and the failure entries, still come from the rows of
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
  no other `std.rt` function: `alloc`, `release`, `args_init`, `args` and `flush_all` are assumed,
  so `del(p)` under `--release` with an empty `std.rt` writes a call to an undefined
  `std.rt.release`.
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
  Each byte of a local holds 0 when its declaration runs (D10.1), and three parts give that.
  `fir_llvm.live_zero` writes the zero value at each `live`, the padding included, and
  `interpreter.clear_bytes` does the same. A parameter takes a whole store. The lowering writes a
  positional literal of a type with padding as `aggregate zeroed` when its destination is the
  whole storage of a temporary or of `_0` (`fir_lower.fresh_storage`), and rule V4 refuses the
  form with no `zeroed` there. Before this, `run/structs/016_local_padding` printed
  18446744073709551361 at `-Xcc -O0` and 1 at `-O1`: no store wrote the padding of the `alloca`,
  so LLVM read `undef`. A `live` writes its local, so it ends the note of an overflow check as an
  assignment does (`spec/fir.md` 12.2).
  `fir_llvm.write_locals` stores the zero value into `%result`, the storage of a scalar `_0`, in
  the entry block. No rule of the verifier asks for an assignment of `_0` before a `return`, so
  FIR text such as `fn main.f() -> u64 { bb0: { return; } }` loaded `%result` with no store, and
  the interpreter gave 0 (`test/fir/run/011_result_zero.fir`). An aggregate `_0` takes no such
  store: `%ret.sret` is the destination of the caller, and `s = f(&s)` lets the callee read it.
  A probe compiler with `llvm.memset` over `%ret.sret` at the entry printed 10, 20 and 20 for
  `run/functions/015_result_reads_destination`, whose output is 16, 22 and 26. The entry store
  costs 0 bytes of `__text` in the compiler at the default level and at `-Xcc -O2` (1,466 more
  IR lines).
- **A unit defines its own modules alone** (module-system.md 10.4). A module with
  `module.interface` set is a section of an interface file: `gen_fir.gen_declaration` puts each
  bodiless function of it in `g->declared`, and `gen_data.gen_finish` writes those `declare
  dso_local ... #0` lines first in the declarations section, from `gen.signature_of` as a
  definition does. `gen_data.gen_global` writes each constant and global of it as an `external
  dso_local` global and writes no initializer, so the unit takes no `@.str.N` from the library.
  The named types and the enum tables come from the same walks as in a whole program. A
  whole-program set holds no interface module, so its text does not change, and a `--lib` set
  holds none either: it has no entry module, so it gets no C `main`. The counters of the private
  data start at 0 in each unit because each invocation makes one emitter. `gen_interface_test`
  holds each declaration against the definition of a whole-program build of the same sources,
  and the ctest `library-artifact` does so for every declaration that the programs of
  `test/library/programs` reach in `std`.
- **`regrow` is one rvalue and three runtime calls** (D12.2, `spec/fir.md` 6, 9.4 and 12.2). The
  lowering writes `_s = move p`, `_p = regrow<T>(move _s, copy _c)` and `dst = aggregate T mut@
  own(move _p, copy _u)`. The form is an rvalue and no call, so `src/fort/owner/` names
  `rv_regrow` only in the arm of the other rvalues of each `switch` over `fir.rvalue_kind`: the
  prover reads the `move` of the operand, with the kill rule of a vacate (the next entry), and a
  store of a value that is not zero. `test/fort/owner/prover_regrow_test.ft` holds 14 functions of
  FIR text against the events of the interpreter. `fir.rvalue_owns` names `rv_regrow`, which rule
  V6 reads. `alias.add_rvalue` borrows the origins of its operands, and `place.is_fresh_new` is
  false for it. `fir_llvm.regrow_value` calls `std.rt.regrow`, `std.rt.regrow_move` and
  `std.rt.release` with the position of the `regrow`, and gives `regrow_move` the private
  constant `@.words.N`, which holds the offsets of `fir.owner_word_at`. `gen.gen_words_ref` gives
  one name for each `regrow` site and joins no two by content (D19.5), and `gen_data.emit_data`
  writes the constants after the enum tables. So a site stores no offset:
  `test/fort/fir_llvm_regrow_test.ft` counts 0 such stores and 3 constants for 3 sites of 2
  tables. `fir.interpreter` calls `std.rt.regrow` and `std.rt.release` and moves the elements and
  their owners itself (`regrow_back`, `move_owners`), because the shadow gives each owner slot of
  the new storage its owner there. `move_owners` and `std.rt.regrow_move` are two implementations of
  one move, and `test/fir_run_test.py` holds the two equal on each program that it runs. The
  entry makes the operand test with `std.heap.is_start`, which the interpreter models, so the run
  writes the same `invalid release` line as a build. `regrow_from` repeats the first three tests of
  D12.2 (`passes_count_tests`) only to report the event of the refused release first, and
  `invalid release` comes before `out of memory` (`test/fir/run/013_regrow_released.fir`).
- **A vacate ends each fact below the owner that it vacates** (the kill at a vacate, `spec/fir.md`
  11, Kill). Before, `del` and `move` had a gen rule only, so `top[0].sub = regrow(move(top), 2)`
  compiled and stored an owner into released storage, and `p->next = move(p)` compiled and made a
  cycle that no owner releases. `prover.kill_past` runs at the two vacates of `prover.gen_reads`,
  before the gen rule. It kills each fact whose place `place.can_read_through` accepts: a `deref` or
  a header `index` stands right after a place `b` that `place.can_vacate` accepts. For a `b` with
  the base of the vacated place `p`, `can_vacate` refuses three cases: `b` and `p` are separate;
  `p` lies below `b` and `b` is not reached through memory; `p` lies below `b` and has no second
  `deref` or header `index` after the one that follows `b`. A `field` step and an `index` on a
  fixed array keep the fact. Where the rule has two readings it rejects more: two places with two
  bases alias when both are reached through memory, and two keys can name one element.
  `spec/fir.md` 11 holds the exact rule. D17.11 lists 5 kill rules and does not hold it: the
  kill holds the sentence of D17.11 that a right-hand side that can change the storage that the
  lvalue names makes the store an error. `test/fort/owner/prover_vacate_test.ft` holds 23
  functions of FIR text against the events of the interpreter,
  `test/lang/fail/ownership/113` to `118` hold the errors and
  `test/lang/run/ownership/086_vacate_keeps_fixed_storage.ft` holds the forms that pass. The cost in
  `test/fort/owner/oracle_test.ft`: of 29954 seeded store sites, the sites with a false reject go
  from 1121 to 1195, and the count with a false accept stays 0. A whole store below a vacated owner
  (`top[0] = e` with `regrow(move(top), 2)` in `e`) is a line of `--whole-store-report` and no error
  (D17.11). A fact through a view has no owner on its path, so the rule keeps it: the read is
  a read of dead storage (D10.9). The third case of `can_vacate` holds when no owner owns the
  storage that holds it. A store through a view can make a span own its storage; the rule then
  keeps the fact, and the store is an access to dead storage (the group `VIEWS` of the test).

## 7. The runtime and the standard library

- **`std.rt.regrow` takes the pointer and the length of the span, and no span** (D12.2,
  `spec/toolchain.md` 5.1). The entry makes the tests in the order of D12.2. Its operand test is
  `heap.is_start`, the test of `drop` with no release (D10.9 rule 4): a zero span passes, and a null
  pointer with a length that is not 0, or a pointer that starts no live allocation, is `invalid
  release`. A view of dead storage gives no such pointer: after `q = &s[0]` and `s = regrow(move(s),
  2)`, the owner slot `q->data` holds the zero value, because `regrow` stores it into each owner
  slot of each old element (D12.2). So `regrow(move(q->data), 3)` gets the zero span and new
  storage, and no error. Only an owner that a trusted module forges gives such a pointer:
  `run/errors/068` holds a forged span owner whose `.ptr` is byte 16 of an allocation. The compiled
  code calls `std.rt.release` for the old storage after `regrow_move`. The tests `181` to `185` of
  `test/lang/run/stdlib` call `regrow` and `regrow_move` directly, and they hold the order of the
  tests and the zero stores of `regrow_move`.

- **Extern declarations keep allocator failure calls** (T-149).
  Apple clang `-O1` removes a `calloc` and matching `free` when the caller reads
  no storage, even with `#3 nobuiltin` at the call site. The emitter adds
  `nobuiltin` to each extern declaration on both targets.
  Check the optimized entry function for `call ptr @calloc` in the OOM fixtures.
  A `new` calls no `calloc` since the runtime has its own allocator (`std.heap`, below). The
  attribute stays for a program that calls `libc.calloc` or `libc.malloc` itself.

- **The allocator of the runtime is `std.heap`, and no byte of an allocation holds its state**
  (D10.3, D10.9, `stdlib.md` 2.16). `std.rt.alloc` calls `heap.take`, and `std.rt.release` calls
  `heap.drop` and reports `invalid release` for a `false`. The invariants, each with the test
  that holds it in `tools/mutations/heap_fort.json`:
  - **No address comes twice.** A bump pointer and a frontier only move up, no `mmap` has a fixed
    address, and no `munmap` takes a range that an allocation used. `munmap` has two callers:
    `add_segment` after a failed `mprotect` of the table and `can_map`, whose probe mapping no
    allocation used. A dead huge allocation keeps its mapping with no access.
  - **The state stands outside the allocations.** It stands in the globals of the module, in the
    first unit of each segment and in the first 3 units of each small chunk. So a store through
    a view of dead storage changes no state, and `drop` reads no byte at its argument. `drop`
    tests the address against the records of the mappings, then against the table word of the
    chunk, then against the live bit of the granule. A small chunk that goes back gets the table
    word of a chunk with no allocation. So a later `drop` of an address in it reads no state of
    the chunk, which nothing can access then (`run/errors/057_invalid_release_chunk_went_back`).
  - **Give-back takes the access away first.** `mprotect(PROT_NONE)` comes before `madvise`. In
    the other order a read between the two calls gets zero bytes, which is no result of D10.9
    rule 2. `MADV_DISCARD` is `MADV_DONTNEED` on Linux and `MADV_FREE` on Darwin.
  - **The holes have a limit.** A range with no access between two ranges with access costs
    kernel map entries, and Linux limits them (`vm.max_map_count`, 65,530 by default). `holes`
    counts them, and a give-back that makes one more than `HOLE_LIMIT` (8192) does not run.
    Such storage keeps its pages too: a `madvise` with no `mprotect` before it makes a later read
    give zero bytes on Linux, which rule 2 forbids (`stdlib.md` 2.16 states the growth).
  - **A refused call changes no record.** A refused `mprotect` or `mmap` for new storage gives
    null (`out of memory`) and moves no frontier. A refused give-back drops no page. A small chunk
    whose give-back the system refused gets the table word `TABLE_KEPT`, because a refused call
    can take the access to its state away: no later `drop` reads that state. The C file
    `test/lang/ffi/heap_calls.c` refuses a chosen call in the tests `run/stdlib/190` and `191`
    and `run/errors/064` to `067`.
  A segment is exactly 2^32 bytes, the size of `libc.map_range`: the pointee of the result of an
  extern lies inside its storage (A7), so `mmap` cannot give a pointer to a struct that is larger
  than the mapping. One reservation of 2^46 bytes was the first design; it cost 61 to 80 ms in
  each process on macOS, so each segment is a mapping of its own. A module that imports
  `std.heap` can call each of its 26 functions other than the 6 of `stdlib.md` 2.16, and it can
  store into each of its 7 `mut` globals: a call of `heap.room` or a store into `heap.bump`
  breaks rule 1, and a call of `heap.seal` breaks rule 2. No rule of the language refuses such a
  call or store yet (`stdlib.md` 2.16).
  `std.rt.free(p)` stays beside `release`: the compiler of `tools/bootstrap.ref` emits a call of
  `free` with one argument for a `del`. Remove `free` when the pinned compiler emits `release`.

- **Mac std.net uses Darwin's 16-byte IPv4 address** (T-145).
  `test/lang/ffi/sockaddr.c` holds the length, family, port and address offsets at 0, 1, 2 and 4.
  Set the length byte to 16 before bind and connect.
  Use Darwin SOL_SOCKET 65535 and SO_REUSEADDR 4.
  Mac `std.libc` declares seven socket calls with a 4-byte `socklen_t`.
  Mac `std.net` uses `libc.set_errno()` for its two errno writes.
  Linux `std/linux/net.ft` and `std/linux/libc.ft` keep their Linux layout and calls.

- **`src/fort` and the suites of `test/fort` call C only through functions of `std` that take
  spans and strings** (D9.8, `stdlib.md` 2.1). `src/fort` declares no extern, calls no extern, and
  names no boundary function:
  `os.error_text` appends a copy of the text of an errno value, `os.list_dir` and
  `os.next_entry` read a directory, `sys.run` runs `--cc` and the archiver, and `str.parse_f64`,
  `str.format_f64` and `mem.can_allocate` serve the interpreter of `--fir-run`. A function of
  `std` that a module outside the trusted set calls copies what C gives into storage of the
  caller: a view of storage of C can end at the next call of C (`strerror(3)`, `setenv(3)`). The
  exception is `sys.args`, whose views name the `argv` storage that C gives to `main` and that
  no call of C frees while the process runs. Four functions of `std` give storage of C to their
  caller. `rt.args_init`, `rt.args` and `sys.args` give the argument strings. `str.from_cstr`
  gives a view of what its trusted caller gave. Their `// gives:` lines say why nothing releases
  or changes that storage (D9.8, "Storage of C"). `libc.errno()` and `libc.set_errno()` give and
  take errno as a value. No function of `std` gives the address of errno, which each later call
  of C can change. A function of a trusted module whose result reaches a pointer, a span or a
  string states the storage in a `// gives:` line. The lint reports the function without that
  line, and it does not judge what the line says (`notes/testing.md` 8). A
  new C call of the compiler goes into such a function of `std`, with a run test in
  `test/lang/run/stdlib`.
  `git grep -nE 'libc\.[a-z_0-9]+\(|str\.from_cstr\(|^\s*extern fn' -- src/fort` finds nothing.
  A suite of `test/fort` makes its files with `os.set_mode`, `os.make_symlink`, `os.make_fifo`
  and `os.set_umask`, moves a descriptor with `io.dup` and `io.dup2`, and removes a variable with
  `sys.unset_env`. No suite module there declares a C function: `test/fort/support/heap.ft`
  reads `std.heap.live_bytes` for the leak probes. `test/fort/heap_full_test.ft` and
  `test/fort/heap_segments_test.ft` are the two suite modules that start with `import extern;`,
  each for calls of `std.heap.drop`, a boundary function (D9.8). A suite text
  that a test compiles is a program and not a suite module: it declares or calls C when it starts
  with `check_env.TRUST`, `import extern; ` with no newline, so each item keeps its line.

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
  signature is paid by all of them at once: T-045 added `qsort` there, and a program that redeclared
  it had to write the same parameter types, `fn (void*, void*) i32` included. Since the migration of
  the rule of a typed `void*` (D3.14), `std/<target>/libc.ft` declares no `qsort`: its comparator
  reads a `void*`, which no cast types. `grep -rn 'extern fn qsort' .` finds the declarations of the
  programs that call it, each with the parameter types that it needs, and one in
  `spec/module-system.md` 8.5, which spells it out as its callback example; `run/ffi/019` runs that
  example. **Retyping one costs the same**, and the bill is the programs that redeclare it, not the
  ones that call it: T-086 gave `read`, `write` and the four `<string.h>` byte functions `u8*` and
  `u8 mut*` and made `malloc` return `void mut* own`, and 9 corpus tests had to redeclare their
  `extern` while 7 only dropped a cast. `spec/decisions.md` D9.8 carried a `void* buf` example of
  `write` that a program could then no longer copy, so a signature in the specification is part of
  that bill.
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
  A value of `libc.malloc` or `libc.calloc` is released with `libc.free(move(p))` (`stdlib.md`
  2.2), and a value of `new` with `del` only. The runtime has its own allocator, so a `libc.free`
  of a value of `new` gives the allocator of C an address that it did not give, and a `del` of a
  value of `malloc` is the runtime error `invalid release` (D10.3, D10.9). A `T mut@ own` from
  `new(T, n)` is released with
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

**`std.rt` imports `std.str` since D6.9**, because `std.rt` reads each C string that the compiler
gives (a file, a type name, the name of an enum member) and each argument of `argv` through
`str.from_cstr`, the one function beside `rt.args_init` that takes a span of a raw pointer. So
every closure holds `std.str` as well. Measured on 2026-11-01 in VM slot 4 (linux, debug preset)
for `fn main() i32 { println("hello, world!"); return 0; }`: the module of bootstrap-2 over its
own standard root holds 92 definitions, 205,078 bytes and 15,469 bytes of `.text`; the module of
the product compiler holds 110 definitions (20 of `std.str`, 2 fewer of `std.rt`), 259,756 bytes
and 17,837 bytes of `.text` (`grep -c '^define'`, `wc -c`, `size -A`).

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
  groups: it reports `expected a type, found '('` at a group, so a tree that bootstrap-1 builds
  uses the struct wrapper. Since D6.9 moved `std.rt.print_str`, `std.rt.panic` and
  `std.rt.print_enum` to a `string` and a span, the chain holds bootstrap-2: a commit
  whose lowering takes both forms of those entries, which bootstrap-1 builds over its own
  pointer-and-length runtime. bootstrap-3 (`2e56fa63`) is the first pin that compiles `regrow`
  (D12.2), so `src/fort` and `std` call `regrow` only after it, and bootstrap-3 builds working-tree
  HEAD. Each pin after the first gets an empty `rt_float.ft` in its standard
  root, because bootstrap-1 loads that file for a closure that holds a float. A tree that the last
  pin builds may use a group. T-153 puts an
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
    The fort form is a span and an index. No span of a raw pointer exists either: only
    `str.from_cstr` and `rt.args_init` take `p[lo..hi]` of one (D6.9). Foreign memory reaches fort
    code through `str.from_cstr` for a C string that C gave, through `str.from_cbuf` for a C
    string in a buffer of fort code, or through an `extern fn` that copies into a span of fort
    code (`libc.memcpy(&buf[0], src, n)`).
  - **No implicit conversion but dropping `mut` and `own`** (D5.4, D17.4) and **no integer
    promotion, not even for `u8`/`i8`** (D6.2). Every mixed-width or mixed-signedness expression
    that C writes silently needs an explicit `cast`, and that is the single largest mechanical
    difference in the port. `.len` is a `u64`, so an `i64` loop counter over a span is a type
    error rather than a warning.
  - `char` is a distinct one-byte type with no arithmetic and no bitwise operators (D3.2):
    `c - '0'` becomes `cast(c, i64) - cast('0', i64)`.
  - `sizeof` takes a type, never an expression (D3.15): `sizeof(x)` becomes `sizeof(T)`.
  - A C string is a NUL-terminated `const char*`; a fort `string` is a pointer and a length and may
    hold an embedded NUL (D3.7). A literal carries a trailing NUL that `len` does not count, so
    `s.ptr` is a C string for a literal and for nothing else: a sub-string is not NUL-terminated,
    and `str.to_cstr` is the conversion at the C boundary, `str.from_cstr` the one coming back.
    `.ptr` stands only in the two positions of D3.5: an argument of an `extern fn` that can give
    fort code no pointer, at any level and in any struct field, and an operand of `==` or `!=`. No
    cast takes it, and `str.from_cstr` takes none; each other use of the address of the first byte
    is `&s[0]`, whose index is checked: `cast(&s[0], void*)`, `cast(&s[0], u64)`,
    `str.from_cbuf(cast(buf[..], u8@))` for a buffer that C filled. `strcmp` of two names becomes
    `==` on two `string`s, which compares `len` and then the bytes.
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
    (`src/fort/ast.ft`), and an index is the other way out where one of the two may be opaque:
    `scope.binding.module` holds `k + 1` for module `k` of the set, because a cast back from
    `void*` adds each pointer place that carries a mark (D3.14). **The same cut applies to two
    C files that call each other**: `check.c`
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

  **The compiler imports its own modules as `fort.<path>`** (D9.11, module-system.md 10.1).
  The file `src/fort/a/b.ft` is the module `fort.a.b` of the library `fort`, and every
  whole-program build reaches it through the search root `src`: the HEAD edge, the fixpoint,
  `fort_lsp`, the lint, the VS Code extension and the `//! flags:` of `test/fort` and `test/lsp`
  pass `-I src`. Each pin edge passes `-I <pin>/src` too, so a pin after this spelling builds;
  the pin `0fe521c` finds its short imports beside its entry file and never reads that root.
  The pins after bootstrap-1 build with this spelling. `tools/long_imports.py` writes a short
  import of a compiler module in the long spelling, and the ctest `long-imports` fails on one.
  A branch that adds a short import runs the tool after its rebase. A branch that edits an
  import header which the tool rewrote gets a conflict in that header: keep the branch side of
  the header, then run the tool, which writes each short import of it in the long spelling.

**The fort compiler can implement language forms that the C compiler does not implement.**
Product tests hold these forms directly. The project does not maintain C-to-fort parity (T-160).

## 9. The census of the undefined states

The goal: no program outside the trusted modules has an undefined state. This section lists each
construct of the emitted IR that has a precondition in the LLVM language reference, each raw
operation of `std/rt.ft` and of `std/heap.ft`, and the back end line. It gives each one a row. The
census changes no behavior of the compiler: it lists a gap and does not close it.

The reference is the LLVM 18.1.8 language reference; each text in double quotes is from it. Each
line number and each count of source lines is a measurement of the tree of `main` at f386f857
(2026-11-14), moved to the tree of the change that adds the allocator `std.heap`; no test holds
them. A `.ft` path with no directory is under `src/fort/`. The probes ran in the VM: clang 18.1.3,
`--target=x86_64-linux-gnu`, the program under `qemu-x86_64`. "Both modes" means the checked mode
and the release mode (D11.1). Exit 134 is SIGABRT after a runtime error, exit 139 is SIGSEGV, and
exit 124 is the limit of 20 s.

### 9.1 The summary

- **How to read a row.** A row gives the construct and its undefined state, the sites where the
  compiler writes it, the precondition, the guard that holds the precondition or `none`, the
  evidence, the state and the method. The id starts with `E` for the emitter (`gen.ft`,
  `gen_data.ft`, `fir_llvm.ft`), `R` for `std/rt.ft`, `H` for `std/heap.ft` and `B` for the back end
  line.
- **The states.** `guarded`: the construct has a precondition, and a compile error, a run-time check
  or a fixed property of the emitter holds it. `closed by` a planned work item: no guard holds it
  today, and the item will. `no plan`: no guard holds it and no planned item closes it. `no
  undefined state`: the reference gives the emitted form no precondition, or the emitter has 0
  sites.
- **The planned work items** are seven, and this tree implements none of them. `spec/decisions.md`
  holds the rule of each of the first three. The null check: a dereference of a null pointer is the
  runtime error `null dereference`, and a call of a null function pointer is the runtime error `null
  call` (D10.5, D10.6, D3.10). Frame storage: a local that an `&` names gets storage of the same
  kind, which is dead after its run returns (D10.1, D10.9). The removal of `--no-bounds-check`: the
  compiler has no option that removes a check (D10.6, D14.1). The trusted premise that names the
  options of the builder: a build with one of these options is outside the guarantee, as a trusted
  module is. Case 4 of D10.7 names `--std-dir` and `--use std=<path>`; the rule for `-Xcc`, `--cc`
  and `-l` is a ruling and not yet a decision. The test of the runtime table: a compile step or a
  test compares each part of each row of `runtime_sig.ft` that the emitters call with the definition
  in `std/rt.ft`, where rule V7 does not (E48); it is a ruling and not a decision. A reserved first
  segment `llvm` of a module path: no module path starts with the segment `llvm` (E49); it is a
  change of D9.2 that waits for the user. A rule of visibility (`priv`) for the state of `std.heap`:
  a module outside the trusted set names no `mut` global and no helper function of `std.heap` (H06);
  it is a change of D9.6 that waits for the user. The zero bytes of a local when its declaration
  runs (D10.1) were an eighth item; the tree implements them since 7a1d42fc, so the rows of that
  item are guarded now. Dead storage was an item of the first census, and the visibility rule took
  its place: the runtime never gives the storage of a `del` to another object, and a read of it
  gives the value that it held last or a memory fault (D10.9). The allocator `std.heap` implements
  it (`stdlib.md` 2.16), so its rows are guarded now (E02).
- **The methods.** `mutation-measured`: a mutant of the compiler removed the property and the test
  `ir-flags` (`test/ir_flags_test.py`) went red. `probed`: a program was compiled and run, or the
  test `ir-flags` counted the corpus, and the row quotes the result. `read`: the code was read and
  nothing ran. Use a `read` row as an argument, not as a test.
- **The rows: 82.** 50 for the emitter, 23 for `std/rt.ft`, 6 for `std/heap.ft`, 3 for the back end
  line.
- **By state.** `guarded`: 52. Closed by a planned work item: 10. `no plan`: 0. `no undefined
  state`: 20. The rows of each item: the null check 4; frame storage 2; the removal of
  `--no-bounds-check` 1; the trusted premise that names the options of the builder 1; the test of
  the runtime table 1; a reserved first segment `llvm` of a module path 1; a rule of visibility
  (`priv`) for the state of `std.heap` 1. E19 names two items, so these seven counts add up to 11.
- **By method.** `mutation-measured`: 4. `probed`: 54. `read`: 24.
- **The rows with no plan: 0.** The census found 4 rows with no guard and no planned work item. One
  was E41, the copy of two places that overlap in part: the emitter wrote `llvm.memcpy` for it, and
  it writes `llvm.memmove` now (D6.3, D19.3). E11: a `load` of a padding byte that no `store` wrote:
  the positional literal. Each `live` writes the zero bytes of its local now (D10.1), so it is
  guarded. B03: the options in the hands of the builder: `-Xcc`, `--cc`, `--std-dir`, `-l`. It has a
  planned work item, and no guard of this tree holds it. 9.2 gives the program of each of these 3.
  The fourth is E49: the name of a fort function or of a fort global with the prefix `llvm.`. A
  program with a module of the name `llvm` and no trusted module ends with SIGSEGV, or clang ends
  with a signal or refuses the module. No rule of `spec/decisions.md` holds it. It has a planned
  work item now: a reserved first segment `llvm` of a module path. The row gives the 4 programs.
- **The guard of `inbounds`.** It is complete for an index or a span of a live object (E04). It is
  not complete under `--no-bounds-check` (E05) and for a null base (E06). A dead base adds no
  undefined state at the instruction (E07).
- **What the null check must cover.** D10.6 makes each evaluation of a place through a null pointer
  the error, and D3.10 each call of a null function pointer. A check before each `load` and each
  `store` leaves three rows open: the address of a field through a null pointer (E06), the `sret`
  destination of a call (E19) and the call of a null function pointer (E16).
- **What the test `ir-flags` holds.** The test compiles each run entry of `test/lang`, the compiler
  and the language server for 2 targets and 2 modes, and it reads each line of each module. It finds
  0 of each of the 66 tokens of `ZERO` on each line of each kind. Among them are `nsw`, `nuw`,
  `exact`, `inrange`, the 8 fast-math flags, `noundef`, `nonnull`, `noalias`, `dereferenceable`,
  `dereferenceable_or_null`, `readnone`, `undef`, `poison`, `fptosi` and `fptoui`. Each line has a
  kind, and each kind has a form and a list of permitted words; a line of no known kind and a word
  outside the list are problems (E44). So `willreturn`, `nofree`, `readonly`, `memory` and
  `speculatable` stand only on the declaration of an intrinsic and in an attribute group, which the
  test holds at exact texts. It holds each `inbounds` at one of three forms (field, array, element)
  and each `unreachable` at one of three (after a failure entry, after `llvm.trap`, alone in a block
  that no path reaches). It holds 0 lines with the text `llvm.memcpy` outside a global, and 0 lines
  with the name of an intrinsic in quotes (E41, E49). The test prints the counts of the occurrences
  and holds none of them, and it holds no equal count of the run entries: each new test and each
  edit of `std` changes these counts.

### 9.2 The programs of E41, E11 and B03

Each program has no `import extern;` and names no boundary function. The compile lines are `fort
--cc clang --std-dir <std> -o p p.ft` and the same with `--release`.

**E41, the copy of two places that overlap in part.** The right output is `0 1 2 ... 15`: element 0
takes `head`, and element `i` takes the old element `i - 1`. Both modes print `0 1 2 3 4 5 6 7 8 9
10 11 12 13 14 15`. The IR of both modes holds `call void @llvm.memmove.p0.p0.i64(ptr align 8 %t6,
ptr align 8 %t5, i64 128, i1 false)`, where `%t5` is the address of `x` and `%t6` is that address
plus 8 bytes. With `llvm.memcpy` in that call, which the emitter wrote before, the release mode
printed `0 1 1 3 3 5 5 7 7 9 9 11 11 13 13 15`.

```fort
struct lo {
    u64[16] a;
    u64 tail;
}

struct hi {
    u64 head;
    u64[16] a;
}

fn main() i32 {
    hi mut x = hi{};
    u64 mut i = 0;
    while (i < 16) {
        x.a[i] = i + 1;
        i += 1;
    }
    lo* p = cast(&x, lo*);
    x.a = p->a;
    i = 0;
    while (i < 16) {
        print(x.a[i]);
        print(" ");
        i += 1;
    }
    println("");
    return 0;
}
```

**E11, the padding byte of a positional literal.** At ff4441b8 the IR of `peek` held one `store` for
each of the two fields of `%x.1`, no fill, and then a `load i8` of the byte 1. The checked mode
ended with SIGSEGV (exit 139), and the release mode printed `the padding byte is 0`. Now the `live`
of `x` writes `call void @llvm.memset.p0.i64(ptr align 8 %x.1, i8 0, i64 16, i1 false)` before the
two stores, and both modes print `the padding byte is 0`. With the designated literal `padded{.small
= v, .big = 5}` both modes print the same line.

```fort
struct padded {
    u8 small;
    u64 big;
}

fn peek(u8 v) u8 {
    padded x = padded{v, 5};
    u8[16]* b = cast(&x, u8[16]*);
    return (*b)[1];
}

fn main() i32 {
    u8 pad = peek(7);
    if (pad == 0) {
        println("the padding byte is 0");
    } else {
        println("the padding byte is ", pad);
    }
    return 0;
}
```

**B03, the options of the builder.** The program prints `0` with no option. With `-Xcc -static -Xcc
-Wl,--defsym=mprotect=getpid` the linker gives the name `mprotect` to `getpid`, the allocator gets
no storage, and the first `new` is the runtime error `out of memory` (exit 134). `new` calls no
`calloc` now, so the option `--defsym=calloc=malloc` of the first census changes nothing. `-Xcc
-ffast-math` gave the same result as no option for the probe of E29 in this VM.

```fort
fn main() i32 {
    u64 mut@ own a = new(u64, 4);
    println(a[3]);
    del(a);
    return 0;
}
```

### 9.3 The rows of the emitter

- **E01. A `load` or a `store` through a null pointer.** Sites: `gen_load` `gen.ft:1017` and
  `gen_store` `gen.ft:1029`, 1 site each. Precondition: "Any memory access must be done through a
  pointer value associated with an address range of the memory access, otherwise the behavior is
  undefined." No function carries `null_pointer_is_valid`, so the address 0 is in no range. Guard:
  none in this tree. D10.5 and D10.6 make the dereference of a null pointer the runtime error `null
  dereference` in both modes, and the compiler implements the check in a later change. Evidence:
  `i32* p = pick(false, &v); println(*p);`, where `pick` returns `null`, exits 139 (SIGSEGV) in both
  modes. State: closed by the null check. Method: probed.
- **E02. A `load` or a `store` through a view after the `del` of its storage.** Sites: as E01.
  Precondition: as E01; the reference gives a freed object no address range. Guard: the allocator of
  the runtime, `std.heap` (`stdlib.md` 2.16). A `del` calls `std.rt.release`, which makes the
  storage dead storage (R11). The storage is a part of a mapping that `mmap` gave (H01), and no call
  that the module of the program can see ends that mapping, so the address range stays: no later
  allocation, frame or mapping takes it (D10.9 rule 1). A read gives the value that the storage held
  last, or a memory fault where the allocator took the access away (rule 2, H04). Evidence: `i32* v
  = p; del(p); i32 mut* own q = new(i32); *q = 9; println(*v);` prints `7`, the value that `*p` held
  last, in both modes. `test/lang/run/pointers/015` to `025` hold the two results. State: guarded.
  Method: probed.
- **E03. A `load` or a `store` through a pointer to a local after its function returns.** Sites: as
  E01, and each `alloca` (E39). Precondition: as E01; the storage of an `alloca` ends when its
  function returns. Guard: none in this tree. D10.1 puts a local that an `&` names in frame storage,
  and D10.9 (rule 3) makes that storage dead storage after the run returns, with the two results of
  E02. Evidence: `fn leak() i32* { i32 x = 7; return &x; }`, then one other call, then `println(*p,
  " ", n)`: the checked mode prints `7 10` and the release mode prints `0 10`. State: closed by
  frame storage. Method: probed.
- **E04. `getelementptr inbounds`, array form and element form: an index or a span outside the
  object.** Sites: `gen_gep_array` `gen.ft:1082` and `gen_gep_element` `gen.ft:1095`, which write
  the one text `getelementptr inbounds` of `gep_begin`, `gen.ft:1073`. Precondition: with `inbounds`
  the base and the result must be in bounds of one allocated object, or the result is poison, and an
  access through poison is undefined. Guard: `chk_bounds` before each index (`fir_lower.ft:1689`)
  and `chk_span` before each span expression (`fir_lower.ft:1741`). `term_removable` (`fir.ft:1728`)
  removes them only for `--no-bounds-check` (E05). The length is true: an array has the length of
  its type, a span carries the length of its source, and a module that is not trusted cannot make a
  span of a pointer (`check.ft:3411`), index a pointer (`check.ft:3351`) or add to a pointer
  (`check.ft:1914`). A null span has the length 0, so the check fails before the instruction. The
  guard is complete for a live object. A dead object is E02 and E03, and a null pointer to an array
  is E06. Evidence: the index 4 and the index -1 of an `i32[4]` give `runtime error: index 4 out of
  range for length 4` and `index -1 out of range for length 4`, exit 134, in both modes. `a[3..1]`
  gives `span bounds 3..1 out of range for length 4`, exit 134, in both modes. `p[0..8]` for an
  `i32* p` is the error `cannot take a span of i32*: a pointer has no length`. State: guarded.
  Method: probed.
- **E05. The same instruction under `--no-bounds-check`.** Sites and precondition: as E04. Guard:
  none: the option removes `chk_bounds` and `chk_span`. D10.6 says that the compiler has no option
  that removes a check, and D14.1 lists no such option. This tree still takes it (`driver.ft:422`).
  Evidence: `i32[4] a = {1, 2, 3, 4}; i32 after = 99; println(a[pass(4)], " ", after);` prints `0
  99` and exits 0 with the option. State: closed by the removal of `--no-bounds-check`. Method:
  probed.
- **E06. `getelementptr inbounds` on a null base: `&p->f`, and an index through a null pointer to an
  array.** Sites: `gen_gep_field` `gen.ft:1106` and `gen_gep_array` (E04). Precondition: "the only
  pointer in bounds of the null pointer in the default address space is the null pointer itself", so
  the address of a field at an offset above 0 is poison. No load is necessary. Guard: none in this
  tree. D10.6 makes each evaluation of a place through a null pointer the runtime error `null
  dereference`, "also when the operation reads no storage, as `&p->f`". Evidence: for a `struct s {
  i32 a; i32 b; }` and a null `s* p`, `i32* q = &p->b; println(q == null, " ", cast(q, u64));`
  prints `false 4` in both modes: the program compares and prints a poison value. So the null check
  stands at the address computation, and not only at a `load` or a `store`. State: closed by the
  null check. Method: probed.
- **E07. `getelementptr inbounds` on a base in a dead object.** Sites: as E04 and E06. The
  reference: "the object does not have to be live anymore; being in-bounds of a deallocated object
  is sufficient". So the instruction has no undefined state of its own. The access after it is E02
  or E03. State: no undefined state. Method: read.
- **E08. The alignment of a `load`, a `store`, an `alloca`, a global, a copy and a fill.** Sites: 12
  texts of `align`: `gen.ft:1022`, `gen.ft:1035`, `gen.ft:1143`, `gen.ft:1147`, `gen.ft:1160`,
  `gen.ft:1592`, `gen.ft:1808`, `fir_llvm.ft:306`, `gen_data.ft:103`, `gen_data.ft:147`,
  `gen_data.ft:436` and `gen_data.ft:452`. Precondition: "Overestimating the alignment results in
  undefined behavior." Guard: each number is `type_alignof` of the type of the place
  (`types.ft:1307`). Each object has that alignment or more: an `alloca` and a global carry the same
  number, and `heap.take` gives storage at a multiple of 16 (R10). A cast never gives a pointer a
  pointee with more alignment (`report_pointee_growth`, `check.ft:4154`). Evidence: `cast(&pair,
  i64*)` for an `i32[2] pair` is the error `a cast never makes the alignment larger (4 to 8 bytes)`.
  The test `ir-flags` holds an alignment on each `load` and each `store`; a mutant that removes the
  text turns it red. State: guarded. Method: probed.
- **E09. A `store` into a global `constant`.** Sites: the 4 texts of `constant` for a global:
  `gen_data.ft:99`, `gen_data.ft:124`, `gen_data.ft:430` and `gen_data.ft:449`. Precondition: "the
  contents of the variable will never be modified". Guard: a pointer to a constant has no `mut`, and
  a cast never adds `mut` (`check.ft:4217`). The verifier refuses a FIR function that writes one
  (V3, `fir_verify.ft:60`). Evidence: `cast(s, u8 mut@)` for a `string s` is the error `cannot cast
  string to u8 mut@: a cast never adds 'mut'`. State: guarded. Method: probed.
- **E10. A `load` from storage that no `store` wrote: a local, a temporary, the heap.** Sites: each
  `alloca` (E39). Precondition: "The allocated memory is uninitialized, and loading from
  uninitialized memory produces an undefined value." A branch on that value is undefined (E37).
  Guard: `var_decl_head` of `spec/grammar.md` gives each local an initializer: `i32 mut x;` is the
  error `expected '=', found ';'`. `{}` writes the whole place with `llvm.memset` (`place_zero`,
  `fir_llvm.ft:497`), and a designated literal does the same before its fields (`aggregate_into`,
  `fir_llvm.ft:1147`). The verifier refuses a read of a temporary before its assignment (V13,
  `fir_verify.ft:119`). `new` gives storage that no allocation held, all 0 (R10). Each `live` writes
  the zero value into the storage of its local before the initializer stores: `llvm.memset` for an
  aggregate and a `store` of the zero constant for a scalar (`live_zero`, `fir_llvm.ft:510`, called
  at `fir_llvm.ft:1262`; D10.1). The entry block stores the zero value into `%result`, the storage
  of a scalar `_0` (`fir_llvm.ft:345`). Evidence: for a `struct padded { u8 small; u64 big; }` the 7
  padding bytes read 0 after `{}`, after a designated literal, in an array literal, in a parameter
  and after a copy, in both modes. The padding of a positional literal is E11. State: guarded.
  Method: probed.
- **E11. A `load` of a padding byte that no `store` wrote: the positional literal.** Sites:
  `lower_literal` writes `aggregate T(...)` with no fill for a literal with no designator
  (`fir_lower.ft:2024`), and `aggregate_into` then writes one `store` for each field
  (`fir_llvm.ft:1136`). D3.14 lets a program read each byte of a struct through a cast to a pointee
  of the same size. Precondition: as E10. Guard: D10.1 gives each byte of a local the value 0 when
  its declaration runs. The `live` of a named local writes `llvm.memset` over its storage before the
  field stores (E10). Into the whole storage of a temporary or of `_0`, the lowering writes a
  literal of a type with padding as `aggregate zeroed` (`fresh_storage`, `fir_lower.ft:1450`,
  `fir_lower.ft:2018`), and rule V4 of the verifier refuses the form with no `zeroed` there
  (`keeps_padding`, `fir_verify.ft:738`). The verifier does not check that a `live` stands before
  each use of a named local; the lowering writes it (`spec/fir.md` 7). A read through a kept pointer
  to the storage of a call that returned is E03. Evidence: the program of 9.2 ended with SIGSEGV in
  the checked mode at ff4441b8, before the emitter wrote the zero bytes. Now the IR of `peek` holds
  `llvm.memset` over `%x.1` before the two field stores, and both modes print `the padding byte is
  0`. State: guarded. Method: probed.
- **E12. A `bool` place that holds a byte other than 0 or 1.** Sites: a `bool` in memory is an `i8`;
  a read is `load i8` and `trunc` to `i1` (`gen.ft:1052`), and a write is `zext` (`gen.ft:1062`).
  The emitter writes no `load i1` and no range metadata, so each byte is a defined value: D3.3 says
  that a load gives the low bit of the byte. The test `ir-flags` holds 0 `load i1` and 0 metadata
  attachments. Evidence: through `cast(&two, bool*)` the byte 2 reads `false` and the byte 3 reads
  `true`, in both modes: the low bit decides. State: no undefined state. Method: probed.
- **E13. A `switch` on an enum value outside its set.** Sites: `switch`, `gen.ft:1200`.
  Precondition: none in the reference: a value with no case goes to the default label. The risk is a
  default label that holds `unreachable`. Guard: the default label of a `switch` with no `default`
  is the block of `fail_enum` (`chk_enum`, `fir_lower.ft:1250`), which no build mode removes
  (`fir.ft:1728`). Evidence: `cast(7, color)` and a `color*` cast of an `i32` that holds 7 both give
  `runtime error: enum value 7 is not a member of color`, exit 134, in both modes. State: guarded.
  Method: probed.
- **E14. A `load` through a pointer to another type of the same size or smaller.** Sites: as E01.
  Precondition: none for an integer or a float, because the emitter writes no metadata, so no
  type-based rule exists; D10.7 says that there is no strict-aliasing rule. A pointer that a `load`
  reads from bytes that an integer `store` wrote has no object, and an access through it is
  undefined. Guard: a cast never adds a pointer place and a `mut` cast never hides one
  (`report_pointer_place`, `check.ft:3957`; D3.14). Evidence: `cast(&f, string*)` for a struct of
  two `u64` is the error `a cast adds the pointer string at byte 0`, and `cast(&h, u64 mut*)` for a
  struct that holds an `i32*` is the error `a 'mut' cast hides the pointer i32* at byte 0`. The test
  `ir-flags` holds 0 metadata attachments. The padding of a positional literal holds 0 (E11), and
  the copy of two places that overlap in part has a defined result (E41). State: guarded. Method:
  probed.
- **E15. A `load` that races with a `store` of another thread.** Sites: as E01. Precondition: the
  memory model of the reference gives such a `load` an undefined value, and a branch on it is
  undefined. D10.7 says that a data race cannot occur, because a fort program has one thread (D9.8,
  "One thread"). Guard: none in the compiler. The lint reports each `extern fn` with the name
  `pthread_create`, `clone`, `clone3` or `thrd_create` (`tools/fort_lint.py:1905`,
  `tools/fort_lint.py:1943`), in each file that it reads, and no row makes such a declaration legal.
  A thread needs an `extern`, so a trusted module (E20). The lint reports these 4 names and no other
  name: a declaration of another C function that starts a thread is case 1 of the trusted premise,
  and C code that starts a thread which reads or writes storage of fort code is case 2 (D10.7).
  `grep -rn 'pthread\|clone(' std` finds 0 lines. Evidence: for a file with `extern fn
  pthread_create(i64 v) i64;` the lint prints a problem that starts with `extern 'pthread_create': a
  fort program has one thread`, and it exits 1. State: guarded. Method: probed.
- **E16. An indirect `call` through a null function pointer.** Sites: `gen_call_sig` with `pointer`
  set, `gen.ft:1463`. Precondition: the callee must be a function. Guard: none in this tree. D3.10
  and D10.6 make the call the runtime error `null call` in both modes, in a later change. Evidence:
  `hook h = {}; println(h.f(1));` for a `struct hook { fn (i32) i32 f; }` exits 139 in both modes.
  State: closed by the null check. Method: probed.
- **E17. An indirect `call` with a function type that the callee does not have.** Sites: as E16.
  Precondition: a call through a pointer needs the function type and the calling convention of the
  callee, "or else the behavior is undefined". Guard: no cast changes a function pointer type or
  makes one (`check.ft:3979`), and a function pointer in storage is a pointer place (E14). A
  function pointer that C gave has the type of a trusted declaration (E20), and no function of a
  trusted module gives one to its caller (D9.8, "Storage of C"). Evidence: `cast(g, fn (i32) i32)`
  for a `fn (u64) u64 g` is the error `cannot cast fn (u64) u64 to fn (i32) i32`, and `cast(&bits,
  hook*)` for a `u64 bits` is the error `a cast adds the pointer fn () void at byte 0`. State:
  guarded. Method: probed.
- **E18. A direct `call`: the signature of the call and of the definition.** Sites: 8 texts of
  `call`. `gen_call_sig`, `gen.ft:1463`, writes each call of a fort function, of an `extern` and of
  a function pointer. 2 texts write a call of a runtime entry (E48), and 5 write a call of an
  intrinsic (E34, E41, E42, E43, E28). Precondition: the type and the calling convention of a call
  match the callee. Guard: one function, `signature_of` (`gen.ft:1374`), gives the text of the call
  (`fir_llvm.ft:1030`), of the definition (`fir_llvm.ft:1541`), of the two declarations
  (`gen_data.ft:464`, `gen_data.ft:529`) and of `main` (`gen.ft:1621`). The emitter writes no
  calling convention keyword, so each side has the default one: the test `ir-flags` holds the
  permitted words of a `declare`, a `define` and a `call` line (E44). State: guarded. Method: read.
- **E19. The `sret` pointer of a call that returns an aggregate.** Sites: 4 texts of `sret`:
  `gen.ft:1414`, `gen.ft:1510`, `gen.ft:1609`, `gen_data.ft:487`. Precondition: "This pointer must
  be guaranteed by the caller to be valid: loads and stores to the structure may be assumed by the
  callee not to trap and to be properly aligned." Guard: none when the destination is a place behind
  a pointer: the caller passes that address. Evidence: `*h.p = make(7);` with a null `two mut* p`
  gives the line `call void @"sret_null.make"(ptr sret(%struct.sret_null.two) %t1, i64 7)`, where
  `%t1` is the null pointer that a `load` gave, and the program exits 139 in both modes. D10.6 makes
  each evaluation of a place through a null pointer the runtime error `null dereference`, so the
  null check stands before the call. A dead destination is E02 and E03. State: closed by the null
  check and frame storage. Method: probed.
- **E20. The declaration and the call of an `extern` function.** Sites: `declare ... nobuiltin`,
  `gen_data.ft:531` and `gen_data.ft:549`. Precondition: the declaration agrees with the C function,
  and each call holds the preconditions of that function. Guard: only a trusted module declares or
  calls one (`check.ft:2919`, `check.ft:2928`; D9.8). The census lists the calls of `std/rt.ft` in
  9.4. The emitter writes no `returns_twice`, so a call must return one time or end the process:
  D9.8 ("Calls from C") lets no row make a declaration of `setjmp`, `longjmp`, `vfork` and 8 more
  names legal, and the lint reports each one (`tools/fort_lint.py:1913`, `tools/fort_lint.py:1919`).
  A C function with another name that returns a second time is case 1 of the trusted premise
  (D10.7). Evidence: `extern fn abort() noreturn;` and a call of it, in a module with no `import
  extern;`, give 2 errors: `extern 'abort' needs a trusted module` and `a call of extern 'abort'
  needs a trusted module`. State: guarded. Method: probed.
- **E21. A tail call.** Sites: 0. The emitter writes no `tail`, `musttail` or `notail`, and the test
  `ir-flags` holds 0 of each. The back end can still make a tail call of its own; that changes no
  meaning. State: no undefined state. Method: probed.
- **E22. `nounwind` on each definition.** Sites: the groups 0, 1 and 8: `gen_data.ft:23`,
  `gen_data.ft:25`, `gen_data.ft:42`. Precondition: "If the function does raise an exception, its
  runtime behavior is undefined." Guard: fort has no construct that unwinds, and the test `ir-flags`
  holds 0 `invoke`, `landingpad`, `resume` and `personality`. A C function does not unwind. A call
  of C returns one time to its caller, or it ends the process (D9.8, "Calls from C"): the lint
  reports a declaration of `longjmp` and of 9 more names (`tools/fort_lint.py:1919`), and `std`
  declares none of them. State: guarded. Method: read.
- **E23. `sdiv`, `udiv`, `srem`, `urem` by 0.** Sites: `fir_llvm.ft:787`, `fir_llvm.ft:789`,
  `fir_llvm.ft:792`, `fir_llvm.ft:794`. Precondition: "Division by zero is undefined behavior", and
  the remainder is the same. Guard: `chk_div_zero` (`fir_lower.ft:2752`), which no build mode
  removes. Evidence: an `i32` and a `u64` division by a 0 that the compiler does not know give
  `runtime error: division by zero`, exit 134, in both modes. State: guarded. Method: probed.
- **E24. `sdiv` and `srem` of the minimum by -1.** Sites: as E23. Precondition: "Overflow also leads
  to undefined behavior". Guard: `chk_div_overflow` (`fir_lower.ft:2763`), which no build mode
  removes. Evidence: `min % -1` for an `i32` and `-128 / -1` for an `i8` give `runtime error:
  division overflow`, exit 134, in both modes. State: guarded. Method: probed.
- **E25. `shl`, `lshr`, `ashr` by a count below 0 or not below the width.** Sites:
  `fir_llvm.ft:802`, `fir_llvm.ft:805`, `fir_llvm.ft:807`. Precondition: for a count "equal to or
  larger than the number of bits in op1, this instruction returns a poison value". Guard: the
  checked mode keeps `chk_shift` (`fir_lower.ft:2785`). The release mode removes it and masks the
  count with the width minus 1 (`write_mask`, `fir_passes.ft:123`; D11.1). Evidence: `one << 32` for
  a `u32` gives `runtime error: shift count 32 out of range for u32`, exit 134, in the checked mode.
  In the release mode `one << 32`, `one << 33` and `4096 >> 40` print `1 2 16`. A count of -1 gives
  the error in the checked mode and `-2147483648` in the release mode. State: guarded. Method:
  probed.
- **E26. `add`, `sub`, `mul`: integer overflow.** Sites: `fir_llvm.ft:780`, `fir_llvm.ft:782`,
  `fir_llvm.ft:784`, and the `sub` of a span length (`fir_llvm.ft:1168`, `fir_llvm.ft:1181`).
  Precondition: none, because the emitter writes no `nsw` and no `nuw`: the result wraps. The
  checked mode calls the overflow intrinsic and keeps `chk_overflow` (`fir_lower.ft:2735`), so an
  overflow of `+`, `-`, `*`, unary `-`, `++` and `--` traps. The release mode removes the check and
  the result wraps (D11.1). `regrow_value` writes 2 more plain `mul` in both modes, the bytes of the
  old elements and of the new storage (`fir_llvm.ft:984`, `fir_llvm.ft:985`). They take no check:
  `std.rt.regrow` ends the program before them when the count is below the length or the product of
  the count and the size does not fit, so neither product wraps. Evidence: a mutant that writes `add
  nsw` turns the test `ir-flags` red. `max + 1` and `-min` for an `i32` give `runtime error: integer
  overflow`, exit 134, in the checked mode and `-2147483648` in the release mode. State: no
  undefined state. Method: mutation-measured.
- **E27. `exact`, `nneg`, `disjoint`, `inrange` and the fast-math flags.** Sites: 0. Each one makes
  a result poison for some operand. The test `ir-flags` holds 0 of `exact`, `nneg`, `disjoint`,
  `inrange`, `nnan`, `ninf`, `nsz`, `arcp`, `contract`, `afn`, `reassoc` and `fast`. State: no
  undefined state. Method: probed.
- **E28. A float to an integer.** Sites: `llvm.fptosi.sat` and `llvm.fptoui.sat`: the name
  (`gen.ft:1759`, `gen.ft:1761`), the call (`fir_llvm.ft:1762`) and the declaration
  (`gen_data.ft:575`). A plain `fptosi` or `fptoui` is poison when "the value cannot fit"; the
  emitter writes 0 of them and the test `ir-flags` holds 0. The saturating intrinsic has no
  precondition (D3.14). Evidence: `cast(1e300, i32)`, `cast(nan, i32)`, `cast(inf, u8)` and
  `cast(-inf, i64)` print `2147483647 0 255 -9223372036854775808` in both modes. State: no undefined
  state. Method: probed.
- **E29. The float instructions and the integer to float conversions.** Sites: `fadd`, `fsub`,
  `fmul`, `fdiv` (`fir_llvm.ft:809`, `fir_llvm.ft:811`, `fir_llvm.ft:813`, `fir_llvm.ft:815`),
  `fneg` (`fir_llvm.ft:1087`), `fcmp` (`gen.ft:958`), `fptrunc`, `fpext` (`fir_llvm.ft:1741`,
  `fir_llvm.ft:1743`), `sitofp`, `uitofp` (`fir_llvm.ft:1747`, `fir_llvm.ft:1749`). Precondition: no
  operand is undefined: `fdiv` by 0 gives an infinity or a NaN, and `sitofp` rounds. Each
  instruction assumes the default floating-point environment: "Running LLVM code in an environment
  where these assumptions are not met can lead to undefined behavior." Guard: fort has no construct
  that changes the rounding mode or enables a trap, and a C function that does needs a trusted
  module (E20). The build line can change the environment (B03). Probes that ran: `1.0 / zero`
  prints `inf`, `cast(18446744073709551615, f32)` prints `1.8446744e+19`, and the product of the
  smallest normal `f64` and 0.5 is above 0. State: guarded. Method: read.
- **E30. A pointer comparison, and a pointer to an integer.** Sites: `icmp`, `gen.ft:942`, and
  `ptrtoint`, `fir_llvm.ft:1704`. Precondition: none: `icmp` compares the two addresses, and
  `ptrtoint` gives the address. fort has `==` and `!=` on pointers only: `a < f` for two `i32*` is
  the error `'<' takes ordered operands, not i32*` (`check.ft:2333`). D10.7 ("The number of an
  address") fixes no number: one storage has one number in one run, and two runs can give different
  numbers. Two literals of one text gave one address in both modes. State: no undefined state.
  Method: probed.
- **E31. An integer to a pointer.** Sites: `inttoptr` in `cast_value`, `fir_llvm.ft:1701`.
  Precondition: none at the instruction; an access through the result needs an address inside a live
  object. Guard: no fort source reaches the site, because the checker refuses each cast of an
  integer to a pointer (`check.ft:4192`; D3.14). Evidence: `cast(bits, i32*)` for a `u64 bits` is
  the error `a cast never makes a pointer from an integer`, and the test `ir-flags` holds 0
  `inttoptr`. State: guarded. Method: probed.
- **E32. A pointer cast.** Sites: no instruction: a pointer is `ptr`, and `cast_value` returns its
  operand (`fir_llvm.ft:1689`). Precondition: none at the cast. The access after it has the
  preconditions of E01, E04, E08, E09 and E14. Guard: D3.14: a cast never makes the pointee larger
  or more aligned (`check.ft:4154`), never gives a type to a `void*` (`check.ft:4201`), never adds
  `mut` (`check.ft:4217`) or `own` (`check.ft:4227`), and keeps each pointer place (E14). Evidence:
  9 refused casts and pointer operations in one probe, 3 added pointer places in a second and 2
  hidden pointer places in a third; each one is a compile error. A legal cast still gives two places
  that overlap in part, whose copy has a defined result (E41), and a view of the padding, which
  holds 0 (E11). State: guarded. Method: probed.
- **E33. `unreachable` after the call of a failure entry.** Sites: `fail_block`, `gen.ft:1697`, and
  `fail_call`, `fir_llvm.ft:1398`. Precondition: `unreachable` "has no defined semantics", so
  control must not reach it. Why control does not: the line before it is a call of one of the 10
  failure entries of `std.rt` (`fail_overflow`, `fail_shift`, `fail_div_zero`, `fail_div_overflow`,
  `fail_bounds`, `fail_span`, `fail_alloc_count`, `fail_enum`, `assert_fail`, `panic`). Each one is
  `noreturn` (E36) and ends in `abort()` (`std/rt.ft:830`). Evidence: the test `ir-flags` holds this
  form for each such `unreachable`. State: guarded. Method: probed.
- **E34. `unreachable` after `llvm.trap`.** Sites: `fir_llvm.ft:1513`. Why control does not reach
  it: `llvm.trap` ends the program. The lowering writes the pair after each call of a `noreturn`
  function (`fir_lower.ft:2138`) and at the end of a `noreturn` function (`fir_lower.ft:270`).
  Evidence: a mutant that removes the call of `llvm.trap` turns the test `ir-flags` red. State:
  guarded. Method: mutation-measured.
- **E35. `unreachable` alone.** Sites: `fir_llvm.ft:1515`, from the end of a function that returns a
  value (`lower_end`, `fir_lower.ft:274`). Why control does not reach it: the checker refuses a body
  whose end a path reaches (`missing return`, `check_stmt.ft:908`), and the verifier refuses a
  function in which a path from `bb0` reaches it (V9, `fir_verify.ft:115`). Evidence: `fn value(bool
  c) i32 { if (c) { return 1; } }` is the error `missing return`. The test `ir-flags` walks the
  blocks of each function and holds that no path from `entry` reaches such a block. State: guarded.
  Method: probed.
- **E36. `noreturn` on a definition and on a declaration.** Sites: the groups 1 and 8
  (`gen_data.ft:25`, `gen_data.ft:42`) and the group 7 of `llvm.trap` (`gen_data.ft:40`).
  Precondition: "This produces undefined behavior at runtime if the function ever does dynamically
  return." Guard: the checker refuses a `noreturn` body that reaches its end (`check_stmt.ft:905`);
  the lowering ends such a function with `llvm.trap` (E34); the verifier holds the trap after each
  `noreturn` call (V8, `fir_verify.ft:114`). A `noreturn` `extern` is a trusted declaration (E20).
  Evidence: `fn stop() noreturn { }` is the error `a noreturn function must end in a terminating
  statement`. State: guarded. Method: probed.
- **E37. `undef`, `poison`, `phi`, `select`, `freeze`; a branch on an undefined value.** Sites: 0 of
  each. "Branching on an undefined value is undefined behavior." The emitter writes no `undef` and
  no `poison`, and the test `ir-flags` holds 0 of the 5 tokens. A local lives in an `alloca`, so no
  `phi` joins two values. The undefined values that an instruction can still make are in E06. State:
  no undefined state. Method: probed.
- **E38. A loop with no end and no effect.** Sites: each `br`. Precondition: none, because no
  definition carries `mustprogress` (`gen_data.ft:16`). With that attribute a loop with no end and
  no effect is undefined. Evidence: a mutant that adds `mustprogress` to the group 0 turns the test
  `ir-flags` red. `while (true) { }` runs until the limit of 20 s in both modes. State: no undefined
  state. Method: mutation-measured.
- **E39. `alloca`: the size of the frame and the end of the stack.** Sites: 3: `gen.ft:1590`,
  `gen.ft:1806`, `fir_llvm.ft:304`. Precondition: "The operation itself is undefined if there is
  insufficient stack space for the allocation." Guard: each definition carries
  `"probe-stack"="inline-asm"` (`gen_data.ft:23`; D10.8), so a frame above one page touches each
  page, and the guard page ends the program. D10.7 names this memory fault as a defined result. The
  ctest `stack_depth` holds the probes (`CMakeLists.txt:853`). Evidence: a frame of 64 MB whose
  first write goes to the far end exits 139 (SIGSEGV) in both modes. Limit: the reference has no
  exception for a probed stack. The guard is a property of the two targets, not of the IR. State:
  guarded. Method: probed.
- **E40. An `alloca` of a variable size, `llvm.stacksave`, `llvm.lifetime`.** Sites: 0. The test
  `ir-flags` holds each `alloca` at a fixed type and in the block `entry`, and it refuses the
  declaration of an intrinsic outside the 5 of E43. So a local keeps its storage until its function
  returns, and the IR gives a pointer to a local of a block that ended no undefined state. The
  result is not the one of D10.9, which says that each entry of the block that declares a named
  local starts a new local with new storage. In this tree each entry uses the one `alloca`, so a
  pointer to the local of an earlier entry reads the local of the later entry. Frame storage (E03)
  changes that result. Evidence: a `while` body with `i32 x = i + 7;` runs for `i` 0 and 1 and keeps
  `&x` of the entry 0. After the loop `println(*keep)` prints `8` in both modes, and the IR holds
  one `alloca` for `x`. State: no undefined state. Method: probed.
- **E41. The copy intrinsic: two places that overlap in part.** Sites: the copy intrinsic is the one
  intrinsic that copies an aggregate. It is `llvm.memmove`, and the emitter has 1 text of its name,
  `COPY_INTRINSIC` (`gen.ft:28`). 2 sites write that name: 1 call, in `gen_memmove` (`gen.ft:1142`),
  and 1 declaration (`gen_data.ft:559`). `gen_memmove` has 1 caller, `copy_to` (`fir_llvm.ft:528`),
  and `copy_to` has 2 callers (`fir_llvm.ft:650`, `fir_llvm.ft:666`). `llvm.memcpy` has 0 sites:
  `grep -n 'llvm\.memcpy' src/fort/*.ft` gives 2 lines, and both are comment lines (`gen.ft:26`,
  `gen.ft:27`). Precondition: none for an overlap: `llvm.memmove` "allows the two memory locations
  to overlap", and its declaration has no `noalias` (`gen_data.ft:560`). For `llvm.memcpy` the
  source and the destination "must either be equal or non-overlapping", and the emitter writes 0
  `llvm.memcpy` (D6.3, D19.3). Two places of one type are equal or apart. A cast to a pointee of the
  same size or smaller (D3.14) gives a second struct type over the same bytes, and then a field of
  one overlaps a field of the other in part. Evidence: the mutant that puts `llvm.memcpy` into
  `COPY_INTRINSIC` turns the test `ir-flags` red, with the name in quotes also: the test knows the
  declaration and the call of `llvm.memmove` only, it holds 0 lines with the text `llvm.memcpy`
  outside a global, and it holds 0 lines with the name of an intrinsic in quotes. `run_tests.py
  --verify-ir` fails a module that holds `llvm.memcpy`, and `test/lang/run/casts/021` to `024` hold
  the result of such a store. The program of 9.2 prints the right array in both modes. The same copy
  through the `sret` result of a call (`x.w = get(cast(&x, lo*));`) prints the right array in both
  modes too. State: no undefined state. Method: mutation-measured.
- **E42. The copy intrinsic and `llvm.memset`: the length, the alignment, the volatile flag.**
  Sites: the copy intrinsic of E41, `gen.ft:1143`, and `gen.ft:1160`. `memset_at` writes each
  `llvm.memset` of the translator. It has 6 call sites: `place_zero` and `live_zero`
  (`fir_llvm.ft:500`, `fir_llvm.ft:514`), `hold` (`fir_llvm.ft:652`), `write_held`
  (`fir_llvm.ft:662`), `regrow_value` (`fir_llvm.ft:999`) and `aggregate_into` (`fir_llvm.ft:1148`).
  Precondition: both pointers are valid for the length, and the length is a defined value. Guard:
  the length is the literal `type_sizeof` of the type of the place (`types.ft:1298`), the alignment
  is E08, and the last argument is the literal `false`. The pointers are the places of E01, E02 and
  E03. Evidence: the test `ir-flags` holds each call at one of two forms with a literal length.
  State: guarded. Method: probed.
- **E43. Each other intrinsic.** Sites: the 6 overflow intrinsics
  `llvm.{s,u}{add,sub,mul}.with.overflow` (call `fir_llvm.ft:1640`, declaration `gen_data.ft:587`),
  the 2 saturating conversions (E28) and `llvm.trap` (`gen_data.ft:570`). None has a precondition.
  With the copy intrinsic (E41) and `llvm.memset` the emitter declares 5 forms, and the test
  `ir-flags` refuses a `declare` line of another form. State: no undefined state. Method: probed.
- **E44. The attributes on the declaration of an intrinsic.** Sites: the groups 4, 5, 6 and 7
  (`gen_data.ft:34`, `gen_data.ft:36`, `gen_data.ft:38`, `gen_data.ft:40`), which hold `nocallback`,
  `nofree`, `nosync`, `speculatable`, `willreturn` and `memory(...)`, and the parameter attributes
  `nocapture`, `readonly`, `writeonly` and `immarg` (`gen_data.ft:560`, `gen_data.ft:565`). No
  declaration has `noalias` (E41). Precondition: an attribute that is false for the callee makes the
  call undefined. Guard: each one stands only on the `declare` line of an LLVM intrinsic or in an
  attribute group. The test `ir-flags` holds each of these lines at one exact text. Each other line
  has a kind, and each kind has a list of permitted words (`PERMITTED`): a `declare` line of an
  `extern` takes `zeroext`, `signext` and `nobuiltin`; a `define` line takes `dso_local`, `zeroext`,
  `signext` and `sret`; a `call` line takes `zeroext`, `signext` and `sret`; a global line takes
  `private`, `unnamed_addr`, `dso_local`, `external`, `constant`, `global`, `zeroinitializer`,
  `null` and `align`; an instruction takes its opcode, the predicates, `align` and `inbounds`. Each
  other word on such a line is a problem. So a new attribute of the emitter fails the test until the
  list names it. Evidence: clang 18.1.3 prints the same groups and the same parameter attributes for
  the 5 declarations when the input gives none, so the emitter claims nothing new. A program can
  make no attribute of this row false. The `immarg` argument is the literal `false`. State: guarded.
  Method: probed.
- **E45. `cold`, `nobuiltin`, `"frame-pointer"`, `"probe-stack"`; the call attributes.** Sites:
  `cold` (`gen_data.ft:40`, `gen_data.ft:42`; the group 2 of `gen_data.ft:30` is never emitted),
  `nobuiltin` (group 3, `gen_data.ft:32`; on an `extern` declaration, `gen_data.ft:549`), and the
  two string attributes (`gen_data.ft:23`). None has a precondition: `cold` is a hint, `nobuiltin`
  removes the assumption that a function with a C name is the C function, and the string attributes
  select code. A call carries the group 3 or no group; the test `ir-flags` holds that. State: no
  undefined state. Method: read.
- **E46. `zeroext` and `signext`.** Sites: `gen.ft:629`, `gen.ft:631`. Precondition: the caller
  extends a parameter and the callee extends a result, as the target ABI says; the two sides must
  agree. Guard: `signature_of` gives both sides one text (E18). For a C function the declaration is
  trusted (E20). State: guarded. Method: read.
- **E47. `dso_local`, `private`, `unnamed_addr`, `external`, `zeroinitializer`, `null`.** Sites: the
  lines of a global and of a definition, for example `gen_data.ft:99`, `gen_data.ft:428` and
  `gen.ft:1499`. The constant `@.words.N` of each `regrow` holds the byte offsets of the owner words
  of an element (`gen_data.ft:153`). None gives a program an undefined state. The test `ir-flags`
  holds each global line at one of 5 forms. `unnamed_addr` makes the address of a constant
  unspecified (E30). `dso_local` is a promise to the linker: a symbol that another shared object
  defines breaks the link and not the run. State: no undefined state. Method: read.
- **E48. A `call` of a runtime entry: the signature of the call and of `std/rt.ft`.** Sites:
  `gen_call_rt` (`gen.ft:1659`) and `gen_call_rt_value` (`gen.ft:1669`), from 10 call sites: the
  allocation (`fir_llvm.ft:914`), the release (`fir_llvm.ft:1235`), the failure entry of a check
  (`fir_llvm.ft:1397`), the fail block (`gen.ft:1696`), the 3 calls of `regrow` (`fir_llvm.ft:982`,
  `fir_llvm.ft:992`, `fir_llvm.ft:997`) and the 3 calls of the emitted `main` (`gen.ft:1603`,
  `gen.ft:1610`, `gen.ft:1633`). Precondition: as E18. `signature_of` does not write these calls.
  The call takes its name and its result type from a row of the table of `runtime_sig.ft`
  (`rt_sig_of`, `runtime_sig.ft:160`) and its argument types from the call site. The definition
  takes its text from the fort function of `std/rt.ft`. The emitters can call 17 entries of the
  table: `grep -o 'rt_entry\.rt_[a-z_0-9]*' src/fort/fir_llvm.ft src/fort/gen.ft`, unique, less
  `rt_none`. Guard: partial. Rule V7 of the verifier (`runtime_holds`, `fir_verify.ft:192`;
  `entry_fits`, `fir_verify.ft:211`; called at `gen_fir.ft:138`) reads the declaration in
  `std/rt.ft` of the entry of each `check` and `fail` that the build mode keeps, at each compile. It
  compares the count of its parameters with the count of the reported operands plus 3, and the type
  of each reported operand. So V7 holds the operands of the 10 check entries: `fail_bounds`,
  `fail_span`, `fail_overflow`, `fail_shift`, `fail_div_zero`, `fail_div_overflow`,
  `fail_alloc_count`, `fail_enum`, `assert_fail` and `panic`. The gap: V7 does not compare the types
  of the 3 location parameters (the file, the line and the column) and the result type of these 10,
  and it compares nothing of the 7 other entries: `alloc`, `free`, `args_init`, `args`, `flush_all`,
  `regrow` and `regrow_move`. No test compares these with `std/rt.ft`: a test of `test/fort` runs in
  a temporary directory and cannot open the file, and `bootstrap0/test/runtime_sig_test.c` compares
  the table of the C compiler. `fir_terminator_test.ft` and `runtime_sig_test.ft` compare rows with
  signatures that a person wrote. The rows are the same for each program, so no fort source changes
  them. Evidence: in review, a `std/rt.ft` whose `fail_bounds` takes `u64 index` gives the compile
  error "the runtime `std.rt` has no entry `fail_bounds` with the parameters that `cmp` needs". One
  whose `fail_bounds` takes `u64 line` builds, and the program prints the runtime error and exits
  134. Probes that ran: clang 18.1.3 and `opt -passes=verify` refuse a module whose call names a
  function that the module does not hold (`use of undefined value`). Both accept a call with the
  argument types `(i32, i32)` of a function that the module defines with `(i64)`. So a wrong name in
  a row stops the build, and a wrong type in a row does not. State: closed by the test of the
  runtime table. Method: probed.
- **E49. The name of a fort function or of a fort global with the prefix `llvm.`.** Sites:
  `gen_symbol` joins the module path, a dot and the name (`gen.ft:680`), and `gen_symbol_ref` writes
  the result in quotes (`gen.ft:698`). So each function and each global of a module whose path
  starts with the segment `llvm` gets a name such as `@"llvm.assume"`. Precondition: "This prefix is
  reserved in LLVM for intrinsic names; thus, function names may not begin with this prefix." The
  reference also gives a global with one of 4 names a meaning of its own: `llvm.used`,
  `llvm.compiler.used`, `llvm.global_ctors` and `llvm.global_dtors`. The quotes are spelling only
  (D9.7): LLVM reads `@"llvm.assume"` as the name `llvm.assume`. Guard: none. D9.1 permits each
  identifier that is not a keyword as a segment of a module path, D9.2 reserves the first segment
  `std` only, and the compiler has no check of the name. Evidence: 4 programs. Each has a file
  `llvm.ft` beside the entry file, has `import llvm;` and has no `import extern;`. (1) `llvm.ft`
  holds `fn assume(bool c) void`, which prints a line. `main` runs `llvm.assume(false);` and then
  prints a line. The compiler exits 0 in both modes. The program prints no line and ends with
  SIGSEGV (exit 139) in both modes. (2) `llvm.ft` holds `i32 mut used = 7;` and `fn bump() void {
  used += 1; }`. `main` runs `llvm.bump();` and prints `llvm.used`. The right output is 8. In both
  modes clang ends with a signal (SIGSEGV) in its optimizer, and the compiler exits 2. (3) `llvm.ft`
  holds `fn trap() void`. clang refuses the module in both modes (`invalid redefinition of function
  'llvm.trap'`), because each module declares that intrinsic. (4) `llvm.ft` holds `fn hello() void`
  only. Both modes build, and the program prints the right line. The test `ir-flags` holds 0 lines
  with the text `@"llvm.` in the corpus, so no run entry has such a module. State: closed by a
  reserved first segment `llvm` of a module path. Method: probed.
- **E50. The spans that the code of `regrow` gives to `std.rt.regrow_move`.** Sites: `span_slot`
  (`fir_llvm.ft:919`) stores a pointer and a length into a new temporary span, 3 times for each
  `regrow`: the new storage, the old elements, and the offsets of the owner words
  (`owner_words_slot`, `fir_llvm.ft:929`). Precondition: `regrow_move` copies `old.len` bytes with
  `libc.memcpy` and writes zero into the old elements (R08), so each span must hold its bytes inside
  its object. Guard: the new span is the result of `std.rt.regrow`, an allocation of `alloc` of the
  count times the size bytes, with the product of the count and the size as its length. The old span
  is the pointer and the length of the operand times the size, and `std.rt.regrow` refuses an
  operand that is no zero span and starts no live allocation (`heap.is_start`, D10.9 rule 4). The
  span of the offsets is the constant `@.words.N` (E47) and its count, or `null` and 0 when the
  element has no owner word. Each length is a product that does not wrap (E26). The release of the
  old storage and of each `del` tests the start of its allocation, so the guard does not need the
  owner rules (R11). State: guarded. Method: read.

### 9.4 The rows of `std/rt.ft`

The module has 1160 lines. `grep -n 'libc\.[a-z_]*(' std/rt.ft`, less the comment lines, gives 16
call lines of 12 functions of `std.libc`. 9 of them are `extern` functions of the C library. The
other 3 are fort functions of `std.libc` that call C: `snprintf_float`, `errno` and `set_errno`.
Each of the 12 has one row, but `errno` and `set_errno` share one row: 11 rows. Two more rows give
the calls of `std.heap` that replaced `calloc` and `free` (R10, R11). Each call of C carries a `// C
call` comment that states its pointer arguments (D9.8). A row says what C behavior the operation
relies on.

- **R01. `libc.write`.** Site: `std/rt.ft:112`. C behavior: POSIX `write` reads `rest.len` bytes at
  `rest.ptr`; an `fd` that is not open gives -1 and `EBADF`. Guard: the pointer and the length come
  from one span, and `rest.len > 0` because `done < bytes.len`. State: guarded. Method: read.
- **R02. `libc.errno` and `libc.set_errno`: the `errno` of C as a value.** Sites: `std/rt.ft:83`,
  `std/rt.ft:85`, `std/rt.ft:114`. `std/rt.ft` holds no pointer to `errno`: the two functions give
  and take the value. The load and the store through the pointer stand in `std.libc`, directly at
  the call of C: `std/linux/libc.ft:180`, `std/linux/libc.ft:187`, `std/darwin/libc.ft:180`,
  `std/darwin/libc.ft:187`. C behavior: `__errno_location` of glibc and `__error` of the Darwin C
  library return a pointer to the `errno` of the thread, which is never null and lives as long as
  the thread (C11 7.5). Guard: the trusted declaration of `std.libc`; no call of C stands between
  the call that gives the pointer and the access through it (D9.8). State: guarded. Method: read.
- **R03. `libc.isatty`.** Site: `std/rt.ft:84`. C behavior: POSIX `isatty` takes each `int`; a bad
  descriptor gives 0 and sets `errno`, which the function saves and restores. No pointer argument.
  State: guarded. Method: read.
- **R04. `libc.memmove`.** Site: `std/rt.ft:164`. C behavior: `memmove` copies `n` bytes and permits
  an overlap (C11 7.24.2.2); both pointers must be valid, also for `n` 0. Guard: `n` is `bytes.len`,
  the function returns before the call when `n` is 0, and the flush before it leaves `n` bytes in
  `room`. Evidence: a module with no `import extern;` appends `b->data[1..5]` to the buffer `b` that
  holds `abcdef`; the output is `abcdefbcde` in both modes. State: guarded. Method: probed.
- **R05. `libc.strtod`.** Site: `std/rt.ft:620`. C behavior: `strtod` reads a string up to its NUL
  (C11 7.22.1.3); a null `endptr` is legal. Guard: `text` is a local array, and `decimal_text`
  stores the NUL through a checked index. State: guarded. Method: read.
- **R06. `libc.strtof`.** Site: `std/rt.ft:628`. C behavior and guard: as R05. State: guarded.
  Method: read.
- **R07. `libc.snprintf_float`.** Sites: `std/rt.ft:641`, `std/rt.ft:666`. C behavior: `snprintf`
  writes at most the given size with a NUL, and `%.*e` reads one `int` and one `double` from the
  variable arguments (C11 7.21.6.1, 7.21.6.5). An argument of another type is undefined in C. Guard:
  the size is the length of the local `scratch`; the format is one constant; the declaration of
  `std.libc` fixes the two argument types, and the `f32` caller casts to `f64`. State: guarded.
  Method: read.
- **R08. `libc.memcpy`.** Sites: `std/rt.ft:692`, `std/rt.ft:700`, `std/rt.ft:1083`. C behavior:
  `memcpy` needs two objects of `n` bytes that do not overlap (C11 7.24.2.1). Guard: in the first 2,
  the two pointers are the addresses of two locals of 8 bytes (4 for `f32`), and the length is that
  constant. In `regrow_move`, `room` is `fresh[0..old.len]`, whose span expression checks its bound,
  and the length is `old.len`. `fresh` is storage that `std.rt.regrow` allocated after the
  allocation of `old`, so the two do not overlap; the function returns before the call when
  `old.len` is 0, because the pointer of an empty span is null. The two spans are the spans of E50.
  State: guarded. Method: read.
- **R09. `libc.abort`.** Site: `std/rt.ft:830`. C behavior: `abort` raises SIGABRT and does not
  return (C11 7.22.4.1). `std` installs no handler (R22), so the process ends. Evidence: each
  runtime error of 9.3 exits 134. State: guarded. Method: probed.
- **R10. The storage of `alloc`: `heap.take`, which replaced `libc.calloc`.** Site: `std/rt.ft:979`,
  in `alloc`. Behavior: `std.heap.take` gives null or the storage of a new allocation of the size,
  or of 1 byte for 0, at a multiple of 16 and all 0, which no earlier allocation held (`stdlib.md`
  2.16, H01). E10 and E08 rely on this. `std/rt.ft` calls no allocator of C. Guard: `alloc` tests
  the product of the element size and the count for overflow and reports `out of memory` for null
  (D10.9 rule 5). The compiler tests a count below 0 before the call (`chk_alloc_count`,
  `fir_lower.ft:1860`). `regrow` makes its own tests of the count before it calls `alloc`. Evidence:
  the run tests `test/lang/run/stdlib/186` to `195` and `run/errors/054` and `064` to `067`. State:
  guarded. Method: probed.
- **R11. The release: `heap.drop` in `release` and `free`, which replaced `libc.free`.** Sites:
  `std/rt.ft:999`, in `release`, and `std/rt.ft:1019`, in `free`. Behavior: `std.heap.drop` releases
  the live allocation that starts at its argument and returns `true`, or returns `false` and changes
  nothing. It reads no byte at the argument: it tests the address against the records of the
  allocator (`stdlib.md` 2.16). Guard: `release` and `free` are boundary functions, and they report
  a `false` for a pointer that is not null as the runtime error `invalid release` (D10.9 rule 4). So
  a second `del`, a `del` of an inner address, of a local, of a global or of storage of C is that
  error and no release of other storage; the guard does not need the owner rules. The compiler calls
  `release` for `del` and for the old storage of `regrow`; only the compiler of
  `tools/bootstrap.ref` calls `free`. Evidence: `test/lang/run/errors/048` to `053`, `055` to `063`
  and `068`; `rt.free(null)` in a module that is not trusted is the error `'std.rt.free' is a
  boundary function`. State: guarded. Method: probed.
- **R12. `libc.memcmp`.** Site: `std/rt.ft:1113`, in `str_eq`. C behavior: `memcmp` reads `n` bytes
  of two objects; a null pointer is undefined, also for `n` 0 (C11 7.24.1, 7.24.4.1). Guard: the
  call runs only when the two lengths are equal and above 0. `str_eq` is a boundary function, and
  the compiler calls it with the `.ptr` and the `.len` of two strings. Evidence: `rt.str_eq(null, 1,
  null, 1)` in a module that is not trusted is the error `'std.rt.str_eq' is a boundary function`.
  State: guarded. Method: probed.
- **R13. `libc.exit`.** Site: `std/rt.ft:1159`. C behavior: `exit` runs the `atexit` handlers and
  does not return; a second call of `exit` from a handler is undefined (C11 7.22.4.4). Guard: `std`
  registers no handler: `grep -rn 'extern fn atexit\|\.atexit(' std` finds 0 lines. State: guarded.
  Method: read.
- **R14. `str.from_cstr`, which calls `libc.strlen`.** Sites: 6 calls: `std/rt.ft:370`,
  `std/rt.ft:814`, `std/rt.ft:886`, `std/rt.ft:935`, `std/rt.ft:958`, `std/rt.ft:1138`. C behavior:
  `strlen` reads up to a NUL inside the object (C11 7.24.6.3). Guard: `from_cstr` is a boundary
  function and tests for null. 5 calls take a pointer that the compiler emits: a file name, a type
  name, the text of an `assert` or the name of an enum member, each a constant with its NUL. The
  last of the 6 calls takes an element of `argv` (R21). State: guarded. Method: read.
- **R15. A span of a raw pointer: `argv[0..n]`.** Site: `std/rt.ft:1136`, in `args_init`. C
  behavior: `argv` has `argc` elements (C11 5.1.2.2.1). Guard: `n` is `argc`, and the function
  returns before the span when `argc <= 0`. Only the bodies of `str.from_cstr` and `rt.args_init`
  can write this form (`raw_span_allowed`, `check.ft:2713`; D6.9). A span beyond the object is case
  3 of the trusted premise (D10.7). Evidence: `p[0..8]` for an `i32* p` in a module that is not
  trusted is the error `cannot take a span of i32*: a pointer has no length`. State: guarded.
  Method: probed.
- **R16. A pointer cast: 6 casts to `u8 mut*` and `u8*`.** Sites: `std/rt.ft:692`, `std/rt.ft:700`,
  `std/rt.ft:1113`. C behavior: a pointer to a character type can read and write each byte of an
  object (C11 6.5 paragraph 7). Guard: each cast goes to a pointee of 1 byte, which D3.14 permits in
  each module. The other 42 casts of the module are 35 among integers, `char` and floats, 6 from a
  `string` or a `char` span to `u8@`, and 1 pointer to `u64` (R17). State: guarded. Method: read.
- **R17. A pointer to an integer: `cast(p, u64)`.** Site: `std/rt.ft:349`, in `print_ptr`. C
  behavior: none: the function prints the address. The emitted instruction is `ptrtoint` (E30).
  State: no undefined state. Method: read.
- **R18. Pointer arithmetic.** Sites: 0. The language has none: `&x + 1` is the error `there is no
  pointer arithmetic` (E04). The module reaches an element through a span index, which carries its
  check, and takes `.ptr` of a span on 2 lines (`std/rt.ft:112`, `std/rt.ft:164`) for R01 and R04.
  State: no undefined state. Method: probed.
- **R19. A cast of an integer to a pointer.** Sites: 0 of the 48 casts of the module. D3.14 refuses
  the cast in each module, a trusted one too (E31). State: no undefined state. Method: probed.
- **R20. A direct system call.** Sites: 0. Each system call goes through a function of the C
  library. The emitter writes no inline assembly, and the test `ir-flags` holds 0 `asm`. State: no
  undefined state. Method: probed.
- **R21. The start-up code.** Sites: the emitted `main` (`gen_main`, `gen.ft:1581`) and `args_init`
  (`std/rt.ft:1129`). The start files of the C library call `main(argc, argv)`. The emitted `main`
  calls `std.rt.args_init`, `std.rt.args`, the `main` of the program and `std.rt.flush_all`, and
  returns the status with the mask 255 (`gen.ft:1635`). C behavior: `argv[0]` to `argv[argc - 1]`
  point to strings with a NUL, and the return from `main` calls `exit` (C11 5.1.2.2.1, 5.1.2.2.3).
  Guard: no module can call `args_init` (`check.ft:2681`). Evidence: `rt.args_init(1, null)` in a
  program is the error `'std.rt.args_init' belongs to the start of the program`. State: guarded.
  Method: probed.
- **R22. The handling of a signal.** Sites: 0 handlers. `grep -rn 'sigaction\|[^_]signal(' std`
  finds 0 lines, so no fort code runs at a signal. `abort` gives SIGABRT (exit 134), the guard page
  gives SIGSEGV (exit 139), and `llvm.trap` gives the trap of the target. `sys.raise_signal` sends a
  signal to the process, whose default action applies. State: no undefined state. Method: read.
- **R23. The 6 mutable globals.** Sites: `std/rt.ft:65`, `std/rt.ft:66`, `std/rt.ft:70`,
  `std/rt.ft:71`, `std/rt.ft:74`, `std/rt.ft:77`. C behavior: none. Each output call and each
  failure reads and writes them with no lock, so a second thread gives the race of E15. D10.7 says
  that a fort program has one thread (D9.8). Guard: as E15. State: guarded. Method: read.

### 9.5 The rows of `std/heap.ft`

The allocator of the runtime (`stdlib.md` 2.16) has 840 lines. `grep -n 'libc\.[a-z_]*('
std/heap.ft`, less the comment lines, gives 11 call lines of 4 `extern` functions of the C library:
`mmap`, `munmap`, `mprotect` and `madvise`. Each has one row, and the casts and the state of the
module have one row each.

- **H01. `libc.mmap`.** Site: `std/heap.ft:162`, in `map_bytes`, for a segment and for a huge
  allocation. C behavior: POSIX `mmap` with a null address, `MAP_PRIVATE | MAP_ANON`, the descriptor
  -1 and the offset 0 gives a new mapping of `bytes` bytes, each 0, at an address that no mapping
  holds, or `MAP_FAILED` with `errno`. Guard: `map_bytes` asks for `SEGMENT_BYTES` (2^32) or more,
  so the whole pointee of the result, a `libc.map_range` of 2^32 bytes, lies in the mapping, and it
  tests the result against `MAP_FAILED` before each other use (`std/heap.ft:163`). The flags have no
  `MAP_FIXED`, so the call changes no range that an object holds (D10.9 rule 6). A null result is
  `out of memory` (D10.9 rule 5). Evidence: `test/lang/run/stdlib/190` and `193` refuse a chosen
  call through `test/lang/ffi/heap_calls.c`; the rows `D10.9-5-map-failed` and `D10.9-1-unmap-huge`
  of `tools/mutations/heap_fort.json` turn the run tests red. State: guarded. Method: probed.
- **H02. `libc.munmap`.** Sites: `std/heap.ft:187`, in `add_segment`, and `std/heap.ft:730`, in
  `can_map`. C behavior: POSIX `munmap` ends the mapping of the range; a later mapping can take it.
  Guard: each call ends a whole mapping that `map_bytes` gave in the same call, before an allocation
  took storage of it: a segment whose top table the system refused, and the probe of `can_take` for
  a huge size. So no dead storage loses its range (D10.9 rule 1). The mapping of a dead huge
  allocation stays. Evidence: `test/lang/run/stdlib/188` counts 0 calls at the release of a huge
  block, and `190` counts 1 call for each of the two cases. State: guarded. Method: probed.
- **H03. `libc.mprotect`.** Sites: 6 calls: `std/heap.ft:185`, `std/heap.ft:237`, `std/heap.ft:264`,
  `std/heap.ft:267`, `std/heap.ft:319`, `std/heap.ft:707`. C behavior: POSIX `mprotect` sets the
  access of the whole pages of a range of one mapping. It gives -1 with `ENOMEM` at the limit of the
  map entries, and it can change the first part of the range before it fails. Guard: each address is
  `&h->words[i]` of a checked index, or the start of a mapping of `map_bytes`, and each length stays
  inside the same mapping (D9.8 rule 2). Access goes only to the top table, to chunks at or above
  the frontier and to a probe; `PROT_NONE` goes only to dead storage, to state with no live
  allocation, or to a range that no allocation used (D10.9 rule 6). A refusal for new storage is
  `out of memory`; a refused give-back drops no page, and a small chunk whose give-back the system
  refused takes no later read of its state. Evidence: `test/lang/run/stdlib/190`, `191` and
  `run/errors/064` to `067` refuse each call through `test/lang/ffi/heap_calls.c`, also with a
  changed first part. State: guarded. Method: probed.
- **H04. `libc.madvise`.** Sites: `std/heap.ft:322`, in `seal`, and `std/heap.ft:708`, in
  `drop_huge`. C behavior: `MADV_DONTNEED` of Linux drops the pages of the range, and a later read
  of a page that the program can read gives 0. `MADV_FREE` of Darwin can drop them later. Guard:
  each call follows an `mprotect` with `PROT_NONE` of the same range that returned 0, so no read
  reaches a dropped page: it stops with a memory fault, a result of D10.9 rule 2. At the limit of
  the holes the allocator makes neither call, and the storage keeps its pages and its bytes.
  Evidence: `test/lang/run/stdlib/187` and `188` hold the order and the range of the two calls and
  the memory of the process; with `MADV_DONTNEED` on a range that the program can read, 1024 of 1024
  pages read 0 in the VM. State: guarded. Method: probed.
- **H05. The casts of an address: 17 casts.** Sites: 4 casts of a pointer to `u64`, for a test and
  for an offset: `std/heap.ft:163`, `std/heap.ft:657`, `std/heap.ft:776` and `std/heap.ft:798`; 13
  casts of a pointer to `void*` or `void mut*`: the arguments of the 4 calls of C above and the
  results of `take`, for example `std/heap.ft:508`. C behavior: none. The emitted instructions are
  `ptrtoint` (E30) and no instruction. Guard: D3.14 permits a cast to `void` and to an integer in
  each module, and it refuses a cast of an integer to a pointer and a cast that gives a type to a
  `void*`, so no cast of the module makes a pointer from an integer (R19). The compiler types the
  result of `new` itself. State: guarded. Method: read.
- **H06. The 7 mutable globals and the 26 helper functions of `std.heap`.** Sites:
  `std/heap.ft:132`, `std/heap.ft:133`, `std/heap.ft:134`, `std/heap.ft:138`, `std/heap.ft:139`,
  `std/heap.ft:142` and `std/heap.ft:143`. C behavior: none. Guard: none for a module outside the
  trusted set. D9.6 exports each module-level name, so such a module can store into each of the 7
  globals and call each of the 26 functions of `std.heap` other than the 6 of `stdlib.md` 2.16. A
  store into `heap.bump` or a call of `heap.room` gives a live allocation to a second object (D10.9
  rule 1), and a call of `heap.seal` takes the access to live storage away (rule 2). A second thread
  gives the race of E15. No file of the repository outside `std/heap.ft` names one of them.
  Evidence: a module with no `import extern;` that stores into `heap.room(1)->frontier` gets one
  address for two live allocations (`same address true`); one that calls `heap.seal` on a live block
  ends with a bus error at its next access (exit 138, macOS). State: closed by a rule of visibility
  (`priv`) for the state of `std.heap`. Method: probed.

### 9.6 The rows of the back end line

- **B01. The compile line and the link line.** Sites: `push_cc_line` (`driver.ft:1154`) and
  `link_entry` (`driver.ft:2666`). The compile line is `<cc> --target=<triple> -O1 -fPIE
  -Wno-override-module [-c] -o <out> <module.ll> [<-l options>] [<-Xcc arguments>]`, with `-O2` in
  place of `-O1` under `--release`. The link line of a build with libraries is `<cc>
  --target=<triple> -o <out> <object> <archives> <-l options> <-Xcc arguments>`. No fixed option
  changes the meaning of the IR: `-O1` and `-O2` select the passes, `-fPIE` selects the relocation
  model, `-Wno-override-module` removes a warning. The line holds no `-ffast-math`, no `-fwrapv`, no
  `-ffp-contract`, no `-march` and no `-mcpu`, and the module holds no `target-cpu` and no
  `target-features` attribute. For an IR input the `-O` level decides how far the back end uses an
  undefined state: at ff4441b8 the program of E11 ended with SIGSEGV in the checked mode (`-O1`) and
  printed a line in the release mode (`-O2`). State: no undefined state. Method: read.
- **B02. The data layout and the target.** Sites: the module holds `target triple`
  (`gen_data.ft:665`) and no `target datalayout` line; clang gives the layout of `--target`.
  Precondition: the size and the alignment that the emitter computes for a type must be the ones of
  that layout: a copy takes its length from `type_sizeof`, and a field address takes its offset from
  the layout of the back end. Guard: the tests `006_struct_layout.ft`, `011_multidim_layout.ft` and
  `013_sockaddr_layout.ft` of `test/lang/run/ffi` hold sizes and offsets against C; the test
  `ir-flags` holds 0 `target datalayout` lines. State: guarded. Method: read.
- **B03. The options in the hands of the builder: `-Xcc`, `--cc`, `--std-dir`, `-l`.** Sites: `-Xcc`
  arguments go to both lines, last and unchanged (`driver.ft:1182`); `--cc` names the program that
  reads the module; `--std-dir` names the tree of the trusted modules; `-l` adds a library.
  Precondition: each row above assumes clang with the fixed line and the `std` of the compiler.
  Guard: none. The source of the program holds no trusted module, and the build line still changes
  what a trusted call means. Case 4 of the trusted premise (D10.7) names a build with `--std-dir`
  (D14.1) and with `--use std=<path>` (D9.11). No decision names `-Xcc`, `--cc` or `-l` yet.
  Evidence: the third program of 9.2 prints `0` with no option and stops with the runtime error `out
  of memory` (exit 134) with `-Xcc -static -Xcc -Wl,--defsym=mprotect=getpid`. State: closed by the
  trusted premise that names the options of the builder. Method: probed.

### 9.7 The index, the inventory and the limits

- **The constructs by name.** `getelementptr inbounds`: E04, E05, E06, E07. `unreachable`: E33, E34,
  E35. `load`, `store`: E01, E02, E03, E08, E09, E10, E11, E14, E15. An indirect `call`: E16, E17.
  `sdiv`, `udiv`, `srem`, `urem`: E23, E24. `shl`, `lshr`, `ashr`: E25. A float to an integer, an
  integer to a float: E28, E29. An `alloca` of a variable size: E40, E39. `memcpy`, `memmove`,
  `memset`: E41, E42. Each other intrinsic: E43. Each function attribute: E22, E36, E38, E44, E45.
  Each parameter attribute: E19, E46, E44. Each call attribute: E45. Each flag on an instruction:
  E26, E27, E04. The data layout and the target: B02, E08, B01. `phi` or `select` over `undef` or
  `poison`: E37. A `switch` on an enum value outside its set: E13. A `bool` that holds a value other
  than 0 or 1: E12. A local that no statement wrote: E10, E11. A tail call: E21. Integer overflow:
  E26. A pointer comparison: E30. A pointer cast: E32, E14. An integer to a pointer: E31. The name
  of a function or of a global: E49, E47. A span that the emitted code makes of a pointer and a
  length: E50.
- **The inventory.** The string literals of `gen.ft`, `gen_data.ft`, `fir_llvm.ft` and `gen_fir.ft`
  hold 76 LLVM keywords. 68 stand in a row above. The other 8 have no precondition in a form that
  the emitter writes: `and`, `attributes`, `extractvalue`, `or`, `ret`, `sext`, `type`, `xor`. A
  `ret` in a `noreturn` function is E36.
- **Defined, and not one value for each run.** The number of an address (E30; D10.7 fixes no
  number). The bits of a NaN that an operation gives. The end of a recursion with no end: SIGSEGV at
  the guard page in the checked mode, and a loop with no end in the release mode, where the back end
  removes the frame.
- **The limits.** The probes ran for the Linux target only; the test `ir-flags` reads the Darwin
  modules and runs none. The census lists the calls of `std/rt.ft` and of `std/heap.ft` and not the
  call lines of the C library in the other modules of `std` that have one (`grep -rn
  'libc\.[a-z_0-9]*(' std`, less the comment lines). It does not audit the ownership rules, a
  library archive, or the passes of clang. It does not cover the C bootstrap compiler, which builds
  `bootstrap-1` and still writes `llvm.memcpy` with `noalias` (`bootstrap0/src/gen.c:664`,
  `bootstrap0/src/gen_data.c:585`). A row with the method `read` has no test.

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
  start at it (`toolchain.md` 4). `test/parser_range_test.c` catches a missed `finish` only when
  the node has a child that ends after the node's anchor token, since what it checks is that a
  child's range lies inside its parent's: dropping the `finish` of a childless node (`break`,
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
  that parsed from one that did not: a speculation made during an unwind, which is what a
  recovery point and the first statement of a body with no `{` do, would otherwise read the
  unwind and answer no. And every recovery must consume a token unless it is
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
  raises that constant in the same commit, and one that adds a suite of its own to `test/` runs
  `tools/vm configure` before `build`, since the executables are globbed at configure time.

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
  10 of `test/check_layout_test.c`'s 17 tests check both declaration orders
  (`grep -c 'TEST_RUN(.*_in_either_order)'` against `grep -c 'TEST_RUN('`), and
  `test/gen_aggregate_test.c` holds the two emitted modules against each other line by line,
  since a layout is only observable through the offsets it moves. `suffixes_store_base` is the
  third spelling of "look behind fixed arrays" beside `behind_arrays` in `types.c` and
  `struct_of` in `check.c`; keep the three in step by reading them together. Its generality was
  untestable in stage1 by construction, since that compiler refuses `b[2][3]` and `b[3] mut@`,
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
  diagnostics under stage1 and 545 under stage2, 9 notes each, of which 5 stand in another file
  than their error and all 5 put the error in the module being checked. The three other
  note-carrying diagnostics put their note where their error stands: `module '<p>' not found`,
  `module '<a>' is the same file as module '<b>'`, and the struct-or-enum note of the extern
  conflict. The 60 corpus diagnostics that stand in a file the reader did not write are all
  `not supported by the bootstrap compiler: ?:` inside `std/math.ft`, which has one place and is
  not this shape. What holds the rule, and why a `fail` test alone does not, is
  `notes/testing.md` 3. Mutation-measured on 2026-09-14: swapping the two
  places of the extern conflict takes `unit-check_extern` red and `lang` to 5 failed in stage1,
  and `fort-modules` red and `lang-stage2` to 5 failed in stage2; swapping them at the
  redeclaration takes `unit-modules_closure` and `lang` red in stage1 and `fort-modules` and
  `lang-stage2` red in stage2.

## 6. The IR emitter

- **An emitter that copies a value and then clears its source needs an intermediate.** `move(lv)`
  writes into a destination the emitter cannot prove distinct from the operand (`s = move(s)`,
  `*p = move(*q)`, `v[i] = move(v[j])`), so it reads into a register or a `%tmpK` slot first and
  zeroes the operand only after. The first version copied straight to the destination and zeroed
  after, which destroyed the value in three of six build-mode/shape cells and was invisible to
  every language test, because the two aliasing forms it did not cover are not statically
  comparable. Fix the general case rather than banning the syntax that exposes it.
- **The IR emitter** (`src/bootstrap/gen*.c`, toolchain.md 6): it runs inside that window, before
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
  **A runtime entry point is described in two places, and one test holds them together**: the
  fort signature in `std/rt.ft`, which is what defines it, and one row per entry point in
  `src/bootstrap/runtime_sig.c` (`RT_SIG`, indexed by the `rt_entry_t` of `runtime_sig.h`), which
  carries the mangled name of D9.7, the result form, the `noreturn` mark and the parameter forms;
  `toolchain.md` 5.1 states the same signatures in prose. The emitter writes its call sites from
  the row's name, result form and `noreturn` mark, and declares nothing, since the module holds
  the definition too (item 8). **The argument list of a call is not in the table**: each site
  builds its own operands in `gen_expr.c` and `gen_stmt.c`, so a wrong argument type or a wrong
  order is held by the per-site text assertions in `test/gen*_test.c` and by nothing else --
  every entry point a program can reach has one today, by count and not by construction, so a
  ticket that adds an argument to a call writes the assertion with it. The witness for the row is
  `test/runtime_sig_test.c`'s `every_row_is_the_fort_signature_of_std_rt`, which loads `std/rt.ft`
  with the compiler's own front end (CMake passes `FORT_STD_SOURCE_DIR`) and holds each row
  against the declaration of the same name: the result form, the arity, each parameter form and
  the `noreturn` mark, plus the text the emitter writes for that definition's result, which is
  what a call site must state for the two to agree. Nothing below the compiler holds them
  together, so there is no second oracle: before a witness existed, giving `fort_rt_print_f64` an
  `i64` parameter or `fort_rt_fail_div_zero` a 64-bit line number left the whole gate green. The
  fort table (`src/fort/runtime_sig.ft`) cannot read that file -- a `test/fort` program runs in a
  temporary directory holding only itself -- so `test/fort/runtime_sig_test.ft` holds every row
  against the text of 5.1 transcribed into it, and `tools/diff_ir.sh` holds the two tables against
  each other over every program the corpus spells. A narrow result carries its extension attribute
  on the definition *and* at the call site (`define dso_local zeroext i1 @"std.rt.str_eq"(...)`,
  `%t = call zeroext i1 @...`), which is why a form in `RT_SIG` is a type text with its attribute
  and not a type. `opt` accepts a call site whose attributes differ from the callee's and LLVM
  falls back to the callee's, so *dropping* one at a call site cannot change the assumption while
  *adding* one the definition lacks can: T-021's review dropped the `zeroext` from the
  `fort_rt_str_eq` call site and the entire language corpus stayed green, only the emitted-text
  assertion failing. The attribute is not decorative -- on a return it licenses eliding the
  `movzbl` -- and it stops being invisible the moment a lowering compares or widens the narrow
  result instead of truncating it straight to `i1`.
  **The emitter decides lvalue-ness syntactically.** The checker computes `expr_t.lvalue` (D6.7)
  and writes no bit for it on the node, so `is_place_expr` in `gen_expr.c` re-derives it from the
  node kind for the one question that needs it, whether `del` empties its operand (D17.9). A new
  expression form that designates storage is added there as well as to `gen_expr_place`, or `del`
  of it silently frees without emptying.
  Two C declarations of one name are one ELF symbol, so anything the module emits once -- an
  `extern` declaration above all -- deduplicates by the C name and never by `sym_t*`: two modules
  declaring the same function are two symbols, and a second `declare` is a redefinition `opt`
  rejects. The suites (`test/gen*_test.c` over `test/gen_helpers.h`) emit into a sandbox they
  `chdir` into, so `@.file.N` holds a bare file name and the text does not depend on the build
  directory; CMake gives every `test/gen*_test.c` `FORT_OPT` and `FORT_IR_DIR`, and the ctest
  `lang` passes `--verify-ir`, so every module either suite produces is checked by
  `opt -passes=verify`. `opt` is a default, not a configure-time requirement, so each suite must
  report a missing one as a broken environment (`gen_no_verifier`, exit `TEST_RESULT_ERR`) the
  way `test/pipeline_test.sh` exits 2: a spawn that succeeds and a child that exits 127 otherwise
  reads as "the verifier rejected this IR", which blames the wrong thing.
  A duplicate the emitter stops writing is only a fix if something else refuses the program: `opt`
  rejected a `declare` beside a `define` of `fort_entry`, and dropping the declaration to satisfy
  it turned a hard compile error into a call through the declared type -- a SIGSEGV in the test
  that declared a wrong signature (T-018's review). When a tool's rejection is the only thing
  standing between a legal-looking program and wrong code, the front end takes the rejection over
  before the emitter stops producing it.
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
  `a_bool_widens_to_a_byte_because_its_value_is_one_bit` in `test/gen_cast_test.c`.
  **`opt -passes=verify` does not reject an instruction after a terminator.** It splits the block,
  invents an unnamed successor which it prints as `0: ; No predecessors!`, and exits 0 -- so it
  quietly manufactures the implicit numbering D19.5 forbids rather than reporting the module that
  caused it. An emitter whose block structure is wrong therefore passes the whole gate: `gen_for`
  emitted its back edge after a `noreturn` call in a `for` header for a whole ticket while the unit
  tests, the language corpus, `--verify-ir` and `test/pipeline_test.sh` all stayed green. Block
  structure is asserted by the suites themselves: `verified()` in `test/gen_helpers.h` runs
  `gen_block_terminators` before it calls `opt`, so every call site in every emitter suite checks
  contract item 10 and a new suite inherits it. Treat `opt` as a floor that catches type and
  dominance errors, never as the proof of a structural contract item -- and when a contract item
  names a tool as its check, confirm the tool actually rejects a violation before believing it.
  **No internal-ABI mutation is catchable by a language run test.** Flipping a `zeroext` to
  `signext`, dropping either extension attribute, passing an aggregate `byval` and dropping the
  `sret` of a definition each leave the whole of `test/lang` green (measured on all 280 tests,
  T-017's review): a fort program is one LLVM module, caller and callee are compiled together, the
  `-O1` of the `--cc` line inlines the mismatch away, and a convention both sides get wrong agrees
  with itself. The corpus can only see what the *program* can observe -- evaluation order, a callee
  writing to its parameter, a trap that must fire. Everything else is pinned by the text: the
  assertions in `test/gen*_test.c` and the goldens in `test/ir/*.ll`. A ticket that touches the
  calling convention therefore asserts the emitted text and proves the assertion by mutation --
  change the emitter, watch that one test fail, change it back -- rather than trusting that a run
  test would have caught it. The same held for the `llvm.trap` of D19.7: the review broke it and
  all 55 suites and 280 language tests stayed green. **A C mirror is the exception, and only when
  the mirror is optimised.** T-025 re-measured the `zeroext`/`signext` swap with
  `test/lang/run/ffi/007` sending `i8`, `u8`, `i16`, `u16`, `char` and `bool` into separately
  compiled helpers: with the helper at `-O0` the whole corpus stayed green, with the helper at
  `-O1` it printed `4295032812` for `-20` and failed. An unoptimised callee spills its narrow
  parameter to a stack slot and re-narrows it from there, which repairs the caller's mistake; at
  `-O1` the callee keeps the argument under the `AssertSext`/`AssertZext` its parameter attribute
  states, folds the re-narrowing away, and the wrong extension reaches the arithmetic. That is why
  `link_command` in `test/lang/run_tests.py` passes `-O1`, matching the `-O1` the driver gives the
  fort side: a mirror built at `-O0` silently answers a weaker question than the one it was
  written to ask. So the emitted text in `test/gen*_test.c` is where a convention rule is pinned
  *first*, and a `run/ffi` mirror is the second, independent witness -- not a blind one.
  **Under opaque pointers a field's type in a named struct type is observable through the offsets
  it moves and through one other window, a module-level constant initializer.** A substitution
  that leaves every later offset unchanged is invisible to every run test and to C interop, when
  it also keeps the struct's size and alignment. Two shapes do that. One is a same-size swap:
  `i32` for an enum, `i64` for a `ptr`. The other is a widening that fits in padding the field
  already had, such as `u16` written as `i32` or as `i64` in `{ u8; i32; u16; i64 }`.
  It is **not** invisible to `opt` when the module
  holds a constant or a global of that struct type. LLVM writes such an initializer with the named
  type. It then checks each element against the type's element. T-078 printed a `ptr` field
  as `i64`. `opt-18` answered `element 1 of struct initializer doesn't match struct element type` on
  `@"main.N" = dso_local constant %struct.main.node { i32 7, ptr @"main.N" }`. Two tests of
  `gen_global_test.c` went red through `verified()`, at `:201` and at `:313`. That window opens
  only for a struct that D7.10 gives a module-level declaration. A struct that appears in no such
  initializer keeps the weaker guarantee. So one assertion pins a field's IR type in every case:
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

## 7. The runtime and the standard library

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
  redeclares it must now write the same parameter types, `fn i32(void*, void*)` included.
  `grep -rn 'extern fn void qsort' .` finds one declaration in `std/libc.ft` and one in
  `spec/module-system.md` 8.5, which spells it out as its callback example. **Retyping one costs
  the same**, and the bill is the programs that redeclare it, not the ones that call it: T-086
  gave `read`, `write` and the four `<string.h>` byte functions `u8*` and `u8 mut*` and made
  `malloc` return `void mut* own`, and 9 corpus tests had to redeclare their `extern` while 7
  only dropped a cast. `spec/decisions.md` D9.8 carried a `void* buf` example of `write` that a
  program could then no longer copy, so a signature in the specification is part of that bill.
  A set with no
  such directory loads no runtime, which is what every in-process unit suite is, so a suite that
  drives the whole driver writes an **empty** `std/rt.ft` in its sandbox (`test/driver_helpers.h`,
  `test/modules_helpers.h`, `test/fort/support/modules_env.ft` and the three `test/fort/driver*`
  suites) rather than the real one: the driver needs a file that parses and the assertions stay
  short. And the `"files"` of D20.2 and the `"symbols"` of D20.3 now hold the library's records
  too, whose file names are the `--std-dir` the run was given, so `run_tests.py`'s golden index
  compares the records of the test's own directory alone (`index_of_the_test`) -- a byte-for-byte
  golden of the whole index would name a build directory and could not be checked out on another
  machine.
- **Writing a library module against C** (`stdlib.md` 1.4, D13.4): a span is not a pointer, so
  `cast(u8 mut@ own, void* own)` is rejected (D3.14 lists pointer-to-pointer, not span-to-pointer).
  The `libc.free(cast(move(p), void* own))` idiom of `stdlib.md` 2.2 therefore applies to a
  `T mut* own` from `new(T)` or `libc.malloc`; a `T mut@ own` from `new(T, n)` is released with
  `del`, and what crosses to C is its `.ptr`, a view. A byte buffer crosses with no cast at all,
  since T-086 gave the six byte functions `u8*` and `u8 mut*`: write
  `libc.read(fd, buf.ptr, buf.len)` for a `u8 mut@ buf`, and keep the cast only where the types
  really differ, as a `string`'s `char*` does.

**The standard library may use such a feature before `src/fort` can.** `std/rt_float.ft` holds the
float printers of D18.1 and is written with floats, because stage1 never loads it: the loader
takes it into a closure that holds a float and into no other (`src/fort/modules.ft`), so the
compiler that has no floats never reads it and `tools/diff_ir.sh` keeps comparing every program
both compilers build. What that costs is one ctest of its own, `fort_lint_float`, since the lint
runs the compiler without floats over `std/*.ft` and cannot check that one.

**What the split costs, measured on 2026-09-14 (T-096).** A program that prints no float pays
nothing for it. A program that prints one pays 106,765 bytes of emitted IR, 8,565 bytes of `.text`
in the linked binary, and about a quarter more emit time. **Name the unit**: the emitted IR of this
module is about twelve times the `.text` it becomes, so an IR figure offered as the cost of a
binary overstates that cost by that factor. The commands, run at the top of the worktree in the VM
after `tools/vm build debug fort_stage2`:

```sh
printf 'fn i32 main() {\n    println(1);\n    return 0;\n}\n' > /tmp/int.ft
printf 'fn i32 main() {\n    println(1.5);\n    return 0;\n}\n' > /tmp/flt.ft
for p in int flt; do
    build/debug/stage2/fort -S --std-dir build/debug/std -o /tmp/$p.ll /tmp/$p.ft
    echo "$p $(wc -c < /tmp/$p.ll) $(grep -c '^define' /tmp/$p.ll)"
    build/debug/stage2/fort --index --std-dir build/debug/std /tmp/$p.ft |
        python3 -c 'import json, sys; print(len(json.load(sys.stdin)["files"]))'
    build/debug/stage2/fort --std-dir build/debug/std --cc "$(command -v clang)" \
        -o /tmp/$p /tmp/$p.ft
    size /tmp/$p | tail -1
    for i in $(seq 7); do
        t=$(date +%s%N)
        build/debug/stage2/fort -S --std-dir build/debug/std -o /tmp/$p.ll /tmp/$p.ft
        echo $(( ($(date +%s%N) - t) / 1000000 ))
    done | sort -n | sed -n 4p
done
awk '/^define /{c=""; if (match($0, /@"[^"]+"/)) {m=substr($0, RSTART+2, RLENGTH-3);
     sub(/\.[^.]*$/, "", m); c=m}} c!=""{b[c]+=length($0)+1} /^}$/{c=""}
     END {for (m in b) print b[m], m}' /tmp/flt.ll | sort -rn
```

The integer program's closure holds 3 files, `std/rt.ft` and `std/libc.ft` beside the program, and
its module is 84,395 bytes and 51 definitions, 48 of them `std.rt`'s. Its binary holds 10,136 bytes
of `.text`. The float program's closure holds 6 files, the three above plus `std/rt_float.ft`,
`std/strbuf.ft` and `std/mem.ft`; its module is 191,160 bytes and 91 definitions, the 40 new ones
being `std.rt_float` (76,889 bytes), `std.strbuf` (24,179) and `std.mem` (2,732); and its binary
holds 18,701 bytes of `.text`. The integer program's module holds no byte of `std.rt_float`, because
`load_float_runtime` in `src/fort/modules.ft` loads that module into a closure that holds a float
and into no other. The emit takes 55 ms against 69 ms, each the median of seven runs of the loop
above on an idle VM, and 72 ms against 94 ms in a later run that shared the machine with a second
build: read the pair, never one number of it. **A fold of `std.rt_float` into `std.rt` would put
those 106,765 bytes of IR and 8,565 bytes of `.text` into every program**, because a module emits
every definition of every module in the closure, called or not, and external linkage carries each
one past the linker. 14 of the 48 `std.rt` definitions in the integer program's module are named by
no call in it. Each of the 14 stands on its own `define` line and appears nowhere else in the
module, and `nm` finds all 14 in the binary:

**The checker does the same thing, and that is what blocks the fold rather than floats being hard.**
`load_runtime` (`src/bootstrap/modules.c:906`) puts the whole of `std.rt` into every closure stage1
reads, and `base_type` (`src/bootstrap/check.c:476`) refuses `f64` on sight, so a declaration
nothing names is enough. Measured on 2026-09-14: a copy of `std/rt.ft` carrying only
`fn void probe_f64(f64 v) { return; }` -- no literal, no body, no arithmetic -- gives
`not supported by the bootstrap compiler: floats` and exit 1 for a program whose only call is
`println(1)`. `src/fort` itself holds **no float value**: every `f64` in its seven mentioning files
is a comment, a string literal or an enum member, which it must be, since stage1 builds stage2
today. So the compiler never needs float arithmetic; a float **spelling** in a module it loads is
what it refuses. The gate is one branch, and behind it there is nothing to reach:
`grep -c 'f64\|f32'` over `src/bootstrap/types.c`, `gen_expr.c` and `consts.c` reads 0, 0, 0
against 10 and 13 in `src/fort/gen_expr.ft` and `consts.ft` (T-096, and a user's question on
2026-09-14 that found it).

```sh
grep -o '^define .*@"std\.rt\.[a-z_0-9]*"' /tmp/int.ll | sed 's/.*@//' | sort -u > /tmp/d.txt
grep -o 'call [^@]*@"std\.rt\.[a-z_0-9]*"' /tmp/int.ll | sed 's/.*@//' | sort -u > /tmp/c.txt
comm -23 /tmp/d.txt /tmp/c.txt > /tmp/u.txt; wc -l < /tmp/u.txt
while read -r n; do printf '%s ' "$(grep -c -- "$n" /tmp/int.ll)"; done < /tmp/u.txt; echo
nm /tmp/int | sed 's/.* //' | sort -u > /tmp/nm.txt
comm -12 /tmp/nm.txt <(sed 's/"//g' /tmp/u.txt | sort -u) | wc -l
```

**The float-free half of `std.rt_float` can move into `std.rt` today, and it costs 3,004 bytes of
`.text` on every binary to move** (T-096). 25 of the module's 42 top-level declarations hold no
`f32`, no `f64` and no float literal: the `decimal` struct, fifteen constants, `scan`, `step_up`,
`put_exponent`, `layout`, `decimal_text`, `fixed_text` and the three digit helpers `is_digit`,
`digit_of` and `char_of`. The counts come from one command, which reads a declaration as a line
that starts in column 1 and is neither an `import` nor a closing brace:

```sh
sed 's://.*::' std/rt_float.ft |
awk '/^[^[:space:]}]/ && !/^import / && NF {d++; free[d]=1}
     d && /(^|[^A-Za-z0-9_])(f32|f64)([^A-Za-z0-9_]|$)|[0-9]\.[0-9]|[0-9]e[-+]?[0-9]/ {free[d]=0}
     END {n=0; for (i=1; i<=d; i++) n+=free[i]; print d, n}'
free='is_digit|digit_of|char_of|scan|step_up|put_exponent|layout|decimal_text|fixed_text'
awk -v re="^define .*@\"std[.]rt_float[.]($free)\"" \
    '$0 ~ re {c=1; n++} /^define /{if ($0 !~ re) c=0} c {b+=length($0)+1} /^}$/{c=0}
     END {print n, b}' /tmp/flt.ll
```

Copy those 25 declarations into a file that starts with `import std.libc;` and stage1 checks the
file clean (`build/debug/fort --check --std-dir build/debug/std /tmp/free.ft`, exit 0), so
`std/rt.ft` could hold them and the bootstrap would never meet a float. Their nine definitions emit
50,958 bytes of IR, 60 percent of the whole module of a float-free program. The binary is the unit
that decides: a copy of the standard library directory whose `rt.ft` carries the 24 declarations
that `std.rt` does not already define (`DECIMAL_BASE` is there) links the integer program with
13,140 bytes of `.text` against 10,136, and emits 136,174 bytes of IR against 84,395.

T-096 left those declarations where they stand. The move puts 3,004 bytes of unreachable `.text`
into every binary the project builds, and it ends no split: `std.rt_float` keeps the printers, the
search and its `std.strbuf` import. D18.1 states the one condition that ends the split, which is a
fort compiler built by a released fort compiler.

`std/math.ft` is the second such module (T-042) and it costs more, because an `import std.math`
is an ordinary import and no closure rule hides it: stage1 parses the whole closure, so it
refuses every program that imports the module, with
`not supported by the bootstrap compiler: ?:` pointing into `std/math.ft`, even when the program
names no float and calls `abs_i32` alone. Every test of the module is therefore in
`test/lang/bootstrap-unsupported.txt` and stage2 alone runs them. A library module that holds a
float and that programs import by name has this shape; one that only the compiler loads for a
closure, as `std.rt_float` is, does not.

`std/sort.ft` is the counter-example and it is the cheaper shape (T-045). The module holds no
float, no `?:` and no `do`-`while`, so stage1 parses its whole closure and accepts it: the module
and the nine `run` tests T-045 added raised `CLEAN_FILES` from 472 to 482, which is every file
that was meant to check clean, and `test/lang/bootstrap-unsupported.txt` gained no line. The
eleventh file is a `fail` test, which stage1 refuses because that is what it is for. A library
module written inside the bootstrap subset costs nothing at all, and one that leaves it refuses
every importer, so read
`bootstrap-unsupported.txt` before you spell a `?:` in `std/`.

**The consequence costs a build: no module of `src/fort/` may import `std.math`, and no std
module in the closure of `src/fort` may either.** stage1 compiles `src/fort` into stage2, so such
an import makes stage1 refuse the compiler itself and the bootstrap stops. `src/fort` imports
`std.io`, `std.libc`, `std.mem`, `std.str`, `std.strbuf`, `std.strmap` and `std.sys`, and
`std.rt` through them, so those seven and `std.rt` are closed against `std.math` as well.

The rule is wider than the compiler: **a std module that a stage1-compiled program imports may
not import `std.math` either**, or stage1 refuses that program. The worked example is in the
tree. `std/vec.ft:10` declares a private `u64 U64_MAX = 18446744073709551615;`, which duplicates
`math.U64_MAX` digit for digit, and **that duplicate must stay**: 16 files under `test/lang`
import `std.vec` (`grep -rl 'import std.vec' test/lang`), none of them is in `xfail.txt` or in
`bootstrap-unsupported.txt`, so stage1 compiles all 16 and an `import std.math` in `std/vec.ft`
would refuse all 16 at once. The tidy-up that deletes the duplicate is the change to refuse.
Copy the limit into the module that needs it, with a comment naming this rule, until stage1 is
gone.

## 8. The port to fort

- **`src/fort` is written re-entrant** (D20.5), because a language server is a planned consumer of
  the compiler's modules and retrofitting that later would touch every pass. Four rules, set by the
  skeleton (`src/fort/containers.ft`, `diag.ft`, `session.ft`) and followed by every module ported
  after it. No module-level mutable state that outlives one analysis: the diagnostic sink, the
  counters and the caches are fields of `session.session`, which the driver creates, passes down
  and frees, so two analyses in one process share nothing (`src/bootstrap/diag.c` keeps one
  file-scope `sink` holding every counter, which is the habit not to transliterate). Every
  allocation of an analysis comes from that session's pool or from a container the session frees,
  so a document analysed a thousand times leaves the heap where it found it. Nothing in a library
  module ends the process: an impossible input is a
  `panic` at the boundary that broke the precondition (D13.3), never an exit deep in a leaf (the C
  bootstrap ends the process at 73 sites across 17 modules:
  `grep -rn 'fatal_internal(\|fatal_oom(\|\bexit(' src/bootstrap/*.c | grep -v fail.c | wc -l`;
  and its arenas are never freed). A panic buys a documented boundary and a stated precondition,
  not in-process recovery: `std.rt.panic` aborts like every other failure (D11.4), so a server that
  must survive a malformed document runs the analysis where it can observe that abort (D20.5).
  And every read of a source file goes through `session.read_source`, which answers from the
  overlay a `session.set_source` installed before it opens anything, so a server points the
  compiler at an editor buffer without touching a pass. Each module's header comment also records
  whether it uses a function-pointer dispatch table (D3.10) or the switches the C used, so a port
  stays comparable with its oracle.
- **A port answers to its C oracle, not to a reading of the rule.** `diag.ft` first enforced the
  twenty-diagnostics-per-file cap of D14.2 inside `report` and charged a note to the budget, which
  reads like the decision and is not what the compiler does: `src/bootstrap/diag.c` counts a file's
  errors only (`diag_note` touches no counter) and the cap is checked by `lexer.c` and `parser.c`
  before they report, so `check.c` and `modules.c` are uncapped. The divergence is stage-visible --
  25 type errors would have printed 20 under stage2 and 25 under stage1 -- and a stage2 that
  reports differently from stage1 is what the fixpoint work has to not fight. So when a ported
  module can enforce a rule in a place the C does not, read the C: the oracle is where the rule
  lives, and a difference is a bug even when the new place looks tidier.
- **Transliterating the bootstrap into fort.** Phase B rewrites `src/bootstrap/*.c` as
  `src/fort/*.ft`, and stage2 is compiled by stage1, so a compiler source may use only what the
  bootstrap itself accepts. `test/lang/bootstrap-unsupported.txt` is that list: no floats (`f32`,
  `f64`, float literals), no second array or span level in one written type (`i32[3][4]`,
  `i32[4]@`, `u8@@`, `node@[4]`, T-043), no `do { } while` and no `?:`. Function
  pointers are inside the subset (D3.10), so a dispatch table is fine. These are the constructs a
  C file may hold that have no fort spelling, with what replaces each; the rules the bootstrap
  already follows so that it stays portable are the first four.
  **`src/fort` stays inside that subset, and T-046 does not release it.** That ticket froze
  stage1; it did not stop stage1 compiling stage2. The CMake target `fort_stage2` compiles
  `src/fort` with stage1 at every build and the ctest `bootstrap` compiles it twice more, so a
  `src/fort` file that uses a construct stage1 lacks breaks the build and the fixed point on the
  same commit. **The freeze is this: `src/bootstrap` accepts a bug fix, and the smallest
  type-layer edit that lets it parse and check a form `std/` uses. Everything else is still
  forbidden.** A bug fix makes stage1 answer the way the specification already says it must, and
  the ticket that writes one names the decision it restores. The second half is T-086's, and the
  reason is that stage1 checks every `.ft` file in the repository: it compiles `src/fort` and the
  import closure of `std/` into stage2, so any form `std/` spells forces a stage1 change, and a
  corpus test of a new feature must be **refused** with the exact words `not supported by the
  bootstrap compiler` (`run_tests.py`'s `judge_unsupported`), which is a stage1 edit as well.
  T-086 gave `std.libc` a `void mut* own malloc` and paid the smaller of the two: nine code
  sites in `types.c`, `types.h` and `check.c`, no new diagnostic and no new code path.
  T-135 is the second such edit and paid less: two sites, `parse_alloc_type` in `parser.c` and
  the `TYPE_POS_ALLOC` arm of `check_type_at` in `check.c`, both of which got shorter. The rule
  it carries is uniform over the positions of a type, so the smallest edit that reaches the
  forms `std/` spells is the whole rule; a stage1 that took `new(void mut*, n)` for `std/vec.ft`
  and refused `new(ast.sym*, cap)` for `src/fort/gen.ft` would need a condition that the rule
  does not have, and more code than the rule. Measure both ways before you call an edit
  the smallest. Count them
  and list them, including the mechanical ones: the ticket's first inventory named six, and the
  two it missed were `type_build`'s call of `type_voidptr` and the `TYPE_POS_ALLOC` arm of
  `check_type_at`, which is the one site of the nine that changes what an existing program
  means. A feature that
  `std/` does not spell still goes to `src/fort` alone. A feature therefore leaves
  `test/lang/unsupported-stage2.txt` when stage2 gains it, and leaves
  `test/lang/bootstrap-unsupported.txt` never. The ctest `lang` holds that list: stage1 runs over
  the whole corpus with it and with an empty `xfail.txt`, so a test the list names must be refused
  with `not supported by the bootstrap compiler` and a test it does not name must pass. T-046
  measured both directions on 47f98c2 and found the two sets equal: 111 of the 643 corpus tests
  carry that diagnostic under stage1, 111 entries stand in the list, and neither side holds a test
  the other lacks. A compiler that accepts a construct its own source may not hold is this
  section (T-041, floats).
  **`src/lsp` is under no such rule**: stage2 compiles it, so it may use anything `src/fort`
  implements (D20.5). A file of it is reached as `lsp.<name>` through the search root `src`,
  because two files of one closure that share a module path emit one set of symbols (T-063).
  T-039 and not T-046 is the gate for the language server, because the server needs a self-hosted
  compiler that reproduces itself and not the frozen bootstrap. The ctest
  `bootstrap` is what says the compiler reproduces itself.
  **T-063 wrote the first module of `src/lsp` and stayed inside the subset anyway**, and the
  measurement says why. A file stage1 refuses is skipped by `diff_ast.sh`, by `diff_check.sh` and
  by `diff_ir.sh`, and the ctest `fort-modules` drives `test/fort` with stage1, so a module with a
  float in it leaves three of the four differential oracles and needs a corpus of its own under
  qemu. `src/lsp/json.ft` therefore names no float type: a JSON number answers the bits of its
  binary64 value through `flt.flt_parse`, the compiler's own literal reader, and a caller that
  wants the value copies the bits into an `f64` as `std/rt_float.ft` does. The choice is each
  module's to make again, and the first one that needs a float pays for the corpus.
  - No unions, no bitfields, no anonymous struct or union members: a fat tagged struct with a
    kind enum and every field in the open, which is what `ast.h`, `types.h` and `sym.h` already
    are.
  - No macro beyond a constant, and no token pasting or stringizing: a module constant
    (`u64 WORD_BITS = 64;`) or an ordinary function. A C `enum { NAME = value }` becomes a module
    constant or a fort `enum`, whose members are qualified (`kind.num`, D3.9).
  - No compiler builtin fort lacks. `__builtin_add_overflow` and `__builtin_mul_overflow` are
    the exception the exact constant folder needs, and the port replaces their three sites in
    `src/bootstrap/consts.c` by pre-checks: `am > UINT64_MAX - bm` in `add_raw`,
    `a.mag != 0 && b.mag > UINT64_MAX / a.mag` in `cv_mul` and `a.mag == UINT64_MAX` in `cv_not`.
    No new builtin may be added without the same note.
  - No `goto`, no `switch` fallthrough, and an `enum` switch must name every member or carry a
    `default` (D7.6). The bootstrap uses none of the three today; keep it that way.
  - **No pointer arithmetic at all** (D10.4): `p + 1`, `p++` and `p[i]` are errors, and
    `src/bootstrap/str.c` is the file that uses them (`p->cur + p->used`, `b->data + b->len`).
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
  - No variadics in either direction. An extern signature may not declare one (D9.8); a C
    variadic is declared with a fixed prototype for the arguments actually passed, and only
    `i32`, `i64`, `f64` or a pointer may stand in a variadic position (module-system.md 8.4).
    The print family is a builtin (D11.7), so `printf`, `fprintf` and `snprintf` inside the
    compiler become `print`/`println`/`eprintln` or an explicit string buffer.
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
    move. What is not fine is the shape T-035's test environment had, `fn env open()` building an
    `env` in a local and calling `check.check_init(&e.ck, &e.m.s)` on a field of that local before
    returning it: every diagnostic then went to the dead local's session and the suite saw a check
    that reported *nothing at all*, which reads as a pass. The fix is to fill the caller's value
    (`fn void open(env mut* e)`). `modules.ft` sidesteps the question entirely by taking the
    session as a parameter of every call instead of holding it.
  - `_Static_assert` has no fort spelling. The invariant becomes a unit test, or a runtime
    `assert` at the one place that depends on it; `prim.h`'s assertion on the order of
    `prim_kind_t` is the site.
  - `malloc`/`free` become `new`/`del` with ownership (D17), and **there is no `realloc`**: growth
    is allocate, copy, `del`, as `std/vec.ft` and `src/bootstrap/str.c` already write it.
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
  A ported module is judged against the C one it replaces: the same unit suite runs over both, so
  the oracle is the existing test, not a reading of the new code. A module that the two compilers
  can both be made to *show* -- the lexer, through `fort --tokens`, and the parser, through
  `fort --ast` (D14.1) -- gets a differential oracle as well, and that one is worth building
  before the port: `tools/diff_tokens.sh` compares
  the two token dumps, their diagnostics and their exit statuses over every `.ft` file in the
  repository (the ctest `diff-tokens`, a command of `check-lang` so that the gate runs it), and it
  caught every mutation the port was probed with. Two guards make it an oracle rather than a
  ritual, and both were added after a review broke it: `FT_FILES` is the exact number of `.ft`
  files, so a ticket that adds or removes one changes that line in the same commit and no file
  can slip out of the comparison; and stage1's own answer is held against what a lexer must
  produce -- exit 0 or 1 and a dump ending in the end-of-file token -- before the two are
  compared, because two compilers that both refuse `--tokens` agree about everything, and the
  script passed over the whole corpus with both binaries replaced by a stub. The same check is
  what a path with a space needs, since word-splitting the file list makes both compilers fail
  alike. `tools/diff_ast.sh` (the ctest `diff-ast`, T-033) is the same script one pass later over
  the S-expression of `fort --ast`, with the same two guards and the same `FT_FILES` equality, so
  a ticket that adds a `.ft` file raises the constant in **both** scripts. Read what such an
  oracle cannot see before trusting it: the AST dump prints no position, so a node's range and
  its name range (D20.4) are invisible to it and are pinned instead by
  `test/fort/parser_range_test.ft`, whose expected values are the ones `test/parser_loc_test.c`
  asserts of the C parser, source for source. Both scripts also see only what the corpus
  **spells**, which is not the same as what the language has: T-080 removed `::` from the
  language and then gave the fort lexer alone a rule that still lexed one, and both scripts
  stayed green over the whole corpus, because after the same ticket no `.ft` file held a `::`
  outside a string literal or a comment. A construct the corpus does not write is held by the
  two lexer suites (`test/lexer_test.c` and `test/fort/lexer_test.ft`) and by nothing else, so a
  ticket that *removes* a construct writes the assertion that it is gone into both of them
  rather than trusting the differential. A tree dump is one long line, so the script reports
  the first differing byte and a window of each side rather than a `diff` of two whole trees.
  **The root of `test/fort` holds tests and nothing else** -- a `.ft` directly there whose
  stem does not end in `_test` is a lint failure, since `run_tests.py` registers every root `.ft`
  as a test -- so a fixture common to several suites is either repeated in each or put in
  `test/fort/support/`; what that directory is and what it costs is under **Build and test**
  above, in one place rather than two. `tools/lines.py` counts
  `src/fort`, so the ported lines carry the 3:1 ratio like any others.
  Eight more facts the first ports paid for: five from T-032 (`prim.ft`, `consts.ft`,
  `types.ft`) -- keywords, `new(T, n)`, `==`, enum ordering, the forked tests -- and three from
  T-033 (`ast.ft`, `parser.ft`, `test/fort/support/parse_env.ft`) -- joining strings, the
  NULL-for-no-message parameter, and the fixture's token vector. A fourth, that two mutually
  recursive structs had to be declared in one particular order, was a stage1 bug and is gone
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
  - **`fort -S` run by stage1 is the oracle for the emitter port** (T-037). The IR of a program
    is a function of the program alone (D19.5), so the expected text of a fort emitter test is
    read off stage1's own output over a program whose body the test writes, and never
    transcribed from the fort under test. One mechanical step makes the two comparable: a suite
    that drives one expression at a time renumbers `%tN` and `%LN` from zero, which is what the
    emitter produces when the expression is a function's first, so a probe body puts only
    integer-constant declarations before the expression under test (a `bool` or a `string`
    initializer emits instructions and shifts the numbering). It earns its keep: the port and
    the reading of D10.2 disagreed about whether `new(T, 3)` checks its literal count, and the
    oracle said the emitter was right.
  - **The C emitter's four files are one dependency cycle, so the fort port is layered and not
    cut where the C is.** `gen.c` calls `gen_data.c` (`gen_file_ref`, `gen_call_rt`,
    `gen_append_name`) and `gen_stmt.c` (`gen_block_scoped`), and both call back, which no set of
    fort modules can express (D9.5). `src/fort/gen.ft` is therefore gen.c's primitives together
    with gen_data.c's private data, name spelling and runtime calls -- what the checks of D19.6
    need -- `gen_expr.ft` sits above it, and gen.c's function definitions (`gen_function`,
    `gen_fort_entry`, `gen_module`, `gen_program`) belong with the statements and the module
    assembly they call. Every function keeps its C name, so the two emitters are still read side
    by side name by name.

**A feature stage2 has and stage1 lacks is written without using it** (T-041, floats). The
compiler may accept a construct its own source may not hold, and the two halves of that are
separate: the checker and the emitter gain the construct, and the code that implements it stays in
the bootstrap subset. `src/fort/flt.ft` holds an IEEE 754 value as the `u64` of its binary64
pattern and computes on it with integer arithmetic over bignums, because a float variable in the
compiler's own source would stop stage1 building stage2. Three costs a ticket of this shape pays:
`tools/diff_ast.sh` cannot compare a file the two parsers disagree about, so it skips the ones
whose stage1 diagnostics carry the refusal and holds the number skipped as an equality; the corpus
needs one unsupported list per compiler (`notes/testing.md`); and the differential oracles see
less, so the new construct is pinned by named assertions -- `test/fort/gen_float_test.ft` for the
emitted text and `test/fort/check_float_test.ft` for the checker's answers -- rather than by
stage1's output.

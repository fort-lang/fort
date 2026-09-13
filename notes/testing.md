# fort testing: how a test is written and how a change is judged

This document holds every fact about the test corpora and the merge gate: what each suite is, how
to write one, what each oracle can see, and what it cannot see. `spec/toolchain.md` 7 specifies
the language test format and wins over this file; `spec/decisions.md` and `spec/grammar.md` win
over both (D1.2).

T-098 created the headings below. T-099 moved into them the test half of the `## Build and test`
section of `AGENTS.md`, and the test bullets of its `## Technical Standards` section with it, one
bullet at a time and without a rewrite.

## 1. The merge gate

- `tools/vm gate` is the merge gate: `format-check`, `tidy`, and `check-all` under `debug`,
  `asan` and `ubsan` (it configures `debug` first, then configures and builds each preset before
  its `check-all`).
  **Never kill a guest process by pattern.** The VM is shared by every worktree, so
  `tools/vm run 'pkill -f ctest'` or `pkill -f run_tests.py` ends the runs of the other agents as
  well, and each of them reads the kill as a test failure in their own branch. T-043 did it to
  stop its own gate and had to report the damage it could not undo. To stop a run of your own,
  send TERM to the pid the shell gave you (`tools/vm gate > build/gate.log 2>&1 & pid=$!`, then
  `kill -TERM "$pid"`); never `pkill -f 'tools/vm gate'`, which also matches every waiter that
  quotes the command, and never INT, which a backgrounded script cannot trap (T-113). Since T-113
  the gate ends its guest step within a second of the TERM and releases its hold on the worktree.
  What a kill does **not** do is stop a ninja already running in the guest: `ssh -T` allocates no
  pty, so the guest command gets no `SIGHUP` when the connection drops. A leftover build is made
  harmless by the hold rather than prevented; `notes/environment.md` 1 says how to find one.
  **One worktree has one `build/<preset>`, so two gates in it collide** and the collision reads as
  a test failure rather than as contention: two ninja processes drive the same directory, one
  rewrites an object the other is linking, and the tail of the log names whichever test lost. The
  worktree belongs to whoever holds the ticket until they hand it back, so a coordinator re-gates
  only after the implementor has reported, never beside it (T-087, where a coordinator gate and
  an implementor gate ran together and the exit 1 was the collision). Two other readings cost the
  same hour there and are worth knowing as shapes: a `tail` of a log file the run has not finished
  writing reports the previous run's verdict, and piping the gate into `head` closes the pipe
  early, which kills it with SIGPIPE and yields a status that has nothing to do with the tests.
  Capture the whole output to a file under the worktree, wait for the process, and report the
  exit status the shell gives (`tools/vm gate > build/gate.log 2>&1; echo $?`).

## 2. Unit tests in C

- Unit tests: `test/<component>_test.c` with `test/test.h`; the suite name is the file stem and
  `test/` already holds one per component, `runtime_test.c` being the C runtime's and not the
  compiler's, so check the name is free before writing the file (a shell redirection overwrites a
  suite silently and the gate then reports only its absence); every `test/*_test.c` is globbed
  into an executable `build/<preset>/test/<component>_test` linked against `fort_core`, and a
  ctest `unit-<component>`. A `TEST` body is one macro argument: a comma
  outside parentheses (a brace initializer, for example) splits it. `#val` in an assertion
  message is the argument after macro expansion, so compare through a variable when the
  expected text matters. Suites are ordinary C11: no `__VA_OPT__`, and `-Wtype-limits` (gcc)
  rejects assertions that are always true, such as `TEST_ASSERT_GE_SIZE(n, 0)`. The
  `TEST_ASSERT_*_INT64`/`_SIZE` operands are printed with `PRId64`/`%zu`, so cast plain
  literals and `long` values (`(int64_t)0`) or `-Wformat` fails the build. A suite whose
  literals are the test data (sample values, expected texts) wraps them in
  `// NOLINTBEGIN(readability-magic-numbers)` with a comment saying so rather than naming each.
  The sanitizer presets run the unit tests with `allocator_may_return_null=1` (ctest sets the
  environment, `cmake/sanitizers.cmake`) because the runtime's out-of-memory path is tested with
  an impossible allocation; run a suite by hand under those presets with the same variable.
- A test that must observe a program the compiler spawns uses a fake one: `test/fake_cc.sh` is
  the `--cc` of the driver suites, it writes its own path and every argument, one per line, into
  `$FORT_FAKE_CC_LOG` and exits with `$FORT_FAKE_CC_STATUS`, so the whole clang command line is
  one string comparison and the failure path is a variable away. CMake passes its path as
  `FORT_FAKE_CC` to the suites that name it, and to no others (a `target_compile_definitions`
  over a list after the glob loop: `driver_test` asserts the command line, `driver_check_test`
  that `--check` spawns nothing and `driver_conformance_test` that the runs which stop before
  `--cc` spawn nothing), since a unit test has no working directory it can rely on. A new suite
  that needs it is added to that list, not left to inherit it.
- Test code that is compiled rather than included lives in a `test/*.c` that is not a suite:
  CMake globs every such file into the `fort_test_support` object library and links it into every
  suite, so `-Werror` and clang-tidy cover it once. The list is empty today -- its one member,
  `test/ast_dump.c`, became what `fort --ast` writes and moved to `src/bootstrap/ast_dump.c`
  (T-033) -- so the library is created only when the glob finds something, a CMake target with no
  source being a configure error; the glob and the link stay, so the next such file needs no CMake
  edit. A suite links `fort_core`, so a helper may not take the name of a compiler function
  (`type_error` is types.h's error-type constructor, not a test helper).
- clang-tidy's `readability-function-size` caps `main` at about 60 `TEST_RUN`s (statement
  threshold 800; each `TEST_RUN` expands to about 13 statements, so 89 measured 1162): split a
  larger suite into two files with a shared `test/<component>_helpers.h` whose helpers are
  `static inline` so that a suite using only some of them still builds under `-Werror`.
  `test/fork.h` runs a function in a forked child and captures its stderr and exit status, for
  paths that end the process (`fatal_oom`); under asan the child runs LeakSanitizer at exit, so
  the forked function must not drop a block it allocated (blocks its still-live frames point to
  are reachable and fine).
- `TEST_ASSERT_TRUE(f())` and `TEST_ASSERT_FALSE(f())` evaluate their argument twice when it
  fails, once for the test and once for the `#val` of the message, so a call with side effects
  runs a second time on the failing path only. Keep it: it caught a checker that could not run
  twice over one tree. A helper whose second run must not differ either stores the result in a
  variable first or is made idempotent.

## 3. Language tests

- A run test's program executes in a temporary directory (`tempfile.mkdtemp`) holding only the
  compiled program itself, so it cannot open a **pre-existing** file that ships beside the test. It
  may freely create a file there and read it back, which `run/stdlib/051`, `054` and `055` do. A
  program that must read a file it did not write is given `/dev/stdin` as its argument and fed by a
  `//! stdin:` block -- the harness gives the child a pipe on fd 0 and opening `/dev/stdin` re-opens
  it -- which is how `programs/wc.ft`, `cat.ft` and `wordfreq.ft` run. `programs/wc.ft` is
  additionally the fenced block of `spec/stdlib.md` 4 byte for byte below its directives, so an
  edit to either must be made to both; strip the `//!`, `//|` and `//<` lines and diff to check.
- Language tests: `test/lang/run_tests.py [filter]` (decisions D14.4, D14.5; toolchain.md 7.3
  describes every option and verdict). `test/lang/xfail.txt` lists tests the compiler cannot
  pass yet; a listed test that passes fails the run, so shrink the list in the same commit that
  makes tests pass. `test/lang/bootstrap-unsupported.txt` lists tests that use features the C
  bootstrap deliberately lacks (floats, the nested array and span levels of D3.6, do-while,
  `?:`; function pointers are in its subset, D3.10), and `test/lang/unsupported-stage2.txt` is
  the same list for stage2, which implements all four and is therefore empty (T-043, T-044,
  T-041); keep such features out of core tests, or
  split them into their own test, so the core tests exercise stage1. `run_tests.py --lint`
  validates directives without a compiler and runs before every test run;
  `run_tests.py --check-json` is a mode of its own (ctest `lang_check_json`, also run by
  check-lang) that holds the document of `fort --check --json` against the text form on every fail
  test and ignores `xfail.txt`, since it judges the
  two forms of one run rather than the test. It also selects a test with an `index.json` beside
  it, runs that one with `--index` and holds its `"symbols"` against the file byte for byte
  (D20.3): the golden is one record per line as `render_index` spells it, it is the one non-`.ft`
  file a directory test may hold, and `--lint` checks its shape without a compiler, so a golden
  edited by hand into another spelling of the same records fails the lint rather than the run.
  Regenerate one with `fort --index` and `render_index`, never by hand. A compiler exit status
  other than 0 or 1 is an `ERROR`, which `xfail.txt` still covers. Discovery matches a test file's
  stem (the name without `.ft`) against `^(\d{3})_([a-z0-9_]+)$` or `^[a-z0-9_]+$`, so the corpus
  cannot host a test whose *file name* is the thing under test: `fail/structs/my.app.ft` is
  `lint: fail/structs/my.app.ft: bad test name`, and a rule about a file name (a dotted entry
  name, D9.1) is pinned by a unit test instead. A directory test (`<name>/main.ft`) may stand only
  under a `modules` area, whatever else it exercises -- elsewhere the lint is `directory test
  outside modules` -- and its name there is a bare `[a-z0-9_]+` rather than the numbered form the
  single-file areas use; a `//! link:` path is relative to `test/lang`, so a multi-module test
  reaches `ffi/helpers.c` from `run/modules` like any other. The harness and its unit tests are
  Python 3.12, standard library only, wrapped at 100 columns (the host's
  `ruff format --line-length 100` is the reference); `run_tests_test.py` scripts a fake `fort`
  with `//@` lines, extend it rather than calling the real compiler.
- A directive lint gotcha: `run_tests.py --lint` rejects any line of a test whose text holds `//!`
  after code, so a comment inside a test that quotes a directive (`the //! stderr: lines`) fails
  the lint with `only '//! error:' may follow code on a line`. Say "the stderr directives in this
  test's header" instead.
- **A `fail` test may not mix a lexical with a semantic diagnostic.** `lex_file` reports, and the
  driver then stops before the checker runs (D14.2), so a file whose lexical error is annotated
  alongside an expected type error never produces the second one and the run fails on the missing
  annotation. Split them into two files. Found by T-029 while filling the `fail/lexer` gaps.
- **A whole directory in `xfail.txt` hides a class of programs from every pass behind it.** Both
  bugs the deep review of T-015 found were at a module boundary, because `run/modules/` is
  entirely expected to fail, so no program with two modules had ever reached the emitter: an
  `extern` declared in each of two modules was emitted twice, and nothing else crossed a module
  at all. A ticket that adds a pass reads `xfail.txt` for the directories its pass now walks and
  writes one test per class they cover, rather than trusting the corpus it can see.
- A new `std/*.ft` reaches the language harness only after `tools/vm build <preset>` copies it
  into `build/<preset>/std`: running `run_tests.py` by hand against a source
  that has not been copied reports `module 'std.x' not found`. `test/lang/run/stdlib` is where a
  library module is tested.

## 4. fort module tests

- **A `test/fort` suite that checks two sources must reopen its environment between them.** A
  module set answers a path it has already loaded from the tree that load left, so a second
  `check_env.check_src` over one environment silently re-checks the first source and its
  assertions then pass or fail for the wrong reason; the first sink still holds the first check's
  diagnostics as well. `check_env.reopen` is `test/check_helpers.h`'s `begin()` and goes between
  the assertions about one source and the next check. The C helpers reset per check, so a
  translated suite that drops the reset is the failure mode to look for.
- Two corpora beside `test/lang` run through the same `run_tests.py`, which takes the corpus root
  as `--root`: ctest `lang-stage2` (label `lang`) holds the language corpus against stage2 with
  `--xfail test/lang/xfail-stage2.txt`, which started as the whole corpus (`run/`, `fail/`,
  `programs/`) and is empty as of T-038, stage2 passing every test of it;
  ctest `fort-modules` (label `lang`) runs
  `test/fort/<x>_test.ft`, the tests of the compiler's own modules. Both are commands of
  `check-lang`, so the gate runs them. A `test/fort` test is an ordinary run test in the D14.5
  directives whose header carries `//! flags: -I ../../src/fort` (the compiler's working
  directory is the corpus root, so the path has two `..`, not three). **`test/fort` holds the
  tests of `src/lsp` as well**, and a test of a server module carries `-I ../../src` beside that
  root, since a server module is `lsp.<name>` under the root `src` (T-063); the corpus needs no
  second root of its own, and a test whose name is taken by a compiler module takes the `lsp_`
  prefix (`lsp_json_test.ft`, `json_test.ft` being the compiler writer's). Its file name is
  `<module>_test.ft` or `<module>_<case>_panic_test.ft` for a test whose program must end in a
  panic, since a panic kills the program and each one needs a file; any other `.ft` at that root is
  `bad test name`, because a typo there would otherwise run nowhere and say nothing.
  **Code several of those tests share lives in `test/fort/support/*.ft`**, which they reach with a
  second include root (`//! flags: -I ../../src/fort -I support`): a `test/fort` test is a program
  rather than a translation unit, so the `#include`d helper a C suite would use
  (`test/types_helpers.h`) has to be an imported module (`support/types_env.ft`, T-032). The
  directory is invisible to the harness: `discover` walks `run`, `fail`, `programs` and the
  `*_test.ft` of the root and nothing else, so a test misfiled there would run nowhere and say
  nothing. `_report_misplaced_tests` closes that (T-079): a `*_test.ft` anywhere below the root
  outside those three directories is `test outside the root of the corpus`, which is a lint
  problem and not a test, since what belongs under `support/` is shared code and nothing else.
  `tools/lines.py` counts `test/fort/**/*.ft` as test lines and `src/fort/*.ft` and `src/lsp/*.ft`
  as source lines, `test/highlight_test.py` tokenizes them, and `tools/fort_lint.py` lints them
  with the search roots its `SOURCE_SETS` table pairs with the glob
  (`-I src -I src/fort -I test/fort/support`); a file named on its command line takes the roots of
  its own `-I` options.
  **The lint's roots are a superset of the directives' and not a copy of them**, which is the
  property to keep. `grep -h '//! flags:' test/fort/*.ft | sort | uniq -c` counts three shapes
  among the 161 tests: 87 carry `-I ../../src/fort -I support`, 53 carry `-I ../../src/fort`
  alone and 21 carry `-I ../../src/fort -I support -I ../../src`. The lint gives all 161 the same
  three roots, in another order. The two agree about which file answers an import only while no
  root shadows another, and today nothing does, because `src/` holds no `.ft` of its own: a future
  `src/<name>.ft` would be the module `<name>` under the lint's first root and something else
  under a test's, and the lint would then judge a file the harness never compiles (T-063).
  Without those roots `fort --index` reports
  `module 'containers' not found` and every name in the file goes unjudged, which it did until
  T-079. **`test/fort` is not a leak oracle**: the gate's `asan` and `ubsan`
  presets instrument the native compiler, not the x86-64 program the harness builds and runs under
  qemu, so a `del` a module forgets leaks silently through all three presets. A module that
  promises its allocations die with the value that owns them (D20.5) needs a witness of its own --
  in-band accounting the module already keeps (`containers.pool_used`), or the allocator itself:
  identical rounds are handed the same addresses again when a round frees what it took and fresh
  ones when it does not, so an address that repeats over eight rounds is the release
  (`the_blocks_a_node_owns_are_released_with_it` in `test/fort/types_table_test.ft`, T-032).
  Verify such a witness by deleting the `del` it covers and watching it go red; two of them in
  `types.ft` had no witness at all until that was measured.
  **An allocator probe needs two views, because each is blind to what the other sees** (T-034), and
  both of them are blind to a leaked block above 128 KB (section 4 of this file, T-094).
  The address a round is handed catches a leak the allocator serves out of its own free chunks --
  a 64-byte vector -- and misses a large one, because a small probe block still comes back at the
  same address while the heap has grown: with `defer analysis_free` deleted in the driver the
  address probe read 0 while the program break had climbed by megabytes, so the release it
  claimed to witness was unwitnessed. The program break (`extern fn void* sbrk(i64)`, `sbrk(0)`)
  catches exactly the other half: it did not move at all when `del(set->modules.items)` was
  deleted. So watch both, and more than one address when a round allocates several blocks -- a
  dropped `del(set->order.items)` left the address of the module vector exactly where it was, so
  `modules_closure_test.ft` watches three addresses and the break, and every one of the six
  releases was verified against the view that moves. Sample the early round against the **last
  sixteen** rounds and take the smallest distance: the allocator's small-block position runs
  through a cycle once the program's own path is long enough to change a bin -- which the
  harness's `mkdtemp` directory is, while a hand run from `/tmp/prog` is not, so a probe reads 0
  by hand and 12208 under `check-lang` -- and one late round in every cycle is in step with the
  early one whatever the period, while a leak moves all of them. Two late samples were the rule
  until T-038 measured a period of **eight** over the emitter's round and read 26512 with nothing
  leaking; the window has to cover the cycle, and sixteen covers every period seen so far.
  **A round large enough to witness a big release makes the block view useless**, which is the
  other half of the same measurement: a round that allocates a hundred kilobytes and gives it all
  back leaves free chunks of every size, so the 32-byte probe lands wherever one starts and ranged
  over 450 KB with nothing leaking. Switch the block view off for such a round and say why in the
  test, and earn it: every release that round covers must then be large enough for the *break* to
  move, which the deletion experiment confirms one release at a time.
  **Two calls that release the same vector cannot be witnessed apart.** `gen_free`'s `slots_free`
  and `gen_stmt`'s `function_begin` both release the emitter's slot vector, and deleting either
  alone leaks nothing: 15 of `gen_free`'s 16 releases turned T-038's probe red and the sixteenth
  only did so when both were deleted. When a deletion leaves a probe green, ask whether a second
  call already covers it before raising the round size, and write the answer down -- what the
  redundant call answers for there is a stale slot and not a leak, which is a correctness question
  with a witness of its own.
  **A round that does nothing witnesses nothing, and it looks exactly like a round that works**:
  T-038's first emitter round was driven by a program with a type error, so `-S` stopped at the
  front end, the emitter never ran and *all sixteen* deletions still turned the test red -- from
  the block-view noise of the four rounds before it. Check that the program the probe compiles
  actually compiles, by hand, before reading a single deletion result.
  A probe over a whole driver run belongs in a file of its own
  (`test/fort/driver_lifetime_*_test.ft`), since forty other tests in the same program fragment the
  heap for reasons that are not leaks.
  **One probe holds one mode, because `run_tests.py` kills a program at 60 s.** The six
  `test/fort/driver_lifetime_*_test.ft` files were one program until T-094. That program ran 63 s
  alone and 92 s to 98 s while three worktrees gated together, so it timed out twice for a reason
  that was not the branch under test. Do not lower the rounds to fit the clock: T-084 proved that
  a round which is too small witnesses no leak at all. Give each mode a program of its own, which
  divides the work of each program and leaves every round as it was. Measured on a 4-CPU VM: the
  longest program run went from 16.7 s to 6.7 s, the six programs together cost 15 s more CPU,
  because the harness compiles six programs and not one, and each mode gets a heap that no earlier
  mode fragmented (T-094).
  **Neither view sees a leak of a block above 128 KB while the round frees no other block of that
  size.** glibc serves an allocation above `M_MMAP_THRESHOLD`, 131072 by default, by mmap, so the
  program break does not move; and a round large enough to ask for one is a round whose block view
  is already off. The condition is the whole rule: glibc raises that threshold to the size of any
  mmap'd chunk it frees, up to 32 MB, so a round that frees one 227 KB block and leaks a second of
  the same size takes the second out of the heap and **does** move the break. A release whose call
  never runs never raises the threshold, which is the case measured here. T-094 measured it on
  `strbuf.free(&g->out)`: the emitter probe emits a module of 226998 bytes and stays green when
  that release is deleted, while the closure probe, whose module is about two kilobytes, goes red.
  A release is therefore witnessed by the mode whose round keeps it inside the heap, so run a
  deletion sweep over **every** probe and not over the one that looks right. T-094's sweep took 46
  releases, one deletion at a time, each against all six probes: 45 of them turned at least one
  probe red, and the 46th is the `slots_free` pair that two calls share. That sweep also found the
  rule above twice: `strbuf.free(&doc)` in `driver.write_document` had no witness at all until the
  width of the index probe came down from 40 to 27, which takes the document from 92003 bytes to
  62541 and its buffer from an mmap block of 131072 to a heap block of 65536 (T-094).
  **A probe also sizes its round**: a release of one small block per round is seen by neither view,
  because the allocator serves the next round's block out of the chunk the round just freed while
  the break stands still -- deleting `del(t->params)` in `types.ft` left `types_table_test.ft`
  green while every other release of that function turned it red (T-084). The round therefore
  repeats the allocation the release covers (two hundred parameter lists, a thousand child lists)
  until the leak is a round's worth rather than a block's, and the deletion test is what says it is
  enough.
  **A `//! stderr:` directive cannot see a line that should not be there**: it is a substring
  check over the whole run, so "this call wrote nothing" is asserted by capturing the descriptor
  into a file and comparing the bytes: `test/fort/support/capture.ft` does the `dup`/`dup2` and
  the flush around it, and `diag_mute_test.ft` and `driver_test.ft` call it rather than repeating
  the redirect. A mute that kept printing passed the directive form of that test and failed the
  captured form.
  **`lang-stage2` does not pass `--no-unsupported`; it passes a list of its own, and
  `xfail-stage2.txt` is empty.** It did pass the flag until T-038 measured what the flag costs.
  `bootstrap-unsupported.txt` demands that the compiler *reject* the features the C bootstrap
  lacks; stage2 is the transliteration of that compiler and rejects them for the same reasons,
  so with the flag stage2 passes 519 of the 538 tests and nineteen entries stay in
  `xfail-stage2.txt` for ever, while without it stage2
  passes all 538 and the file holds nothing. Take the stronger property. The argument the flag
  was added on -- that inheriting the list would keep twenty entries in the file for ever -- runs
  the other way: it is the flag that keeps nineteen of them. (`bootstrap-unsupported.txt` holds
  twenty entries and stage2 fails nineteen of them under the flag: it passes
  `fail/constants/002_float_to_int.ft` whatever the flag says.)
  **Two compilers, two lists** (T-043). The two subsets stopped being equal when `src/fort`
  gained the nested array and span levels of D3.6 (`i32[3][4]`, `i32[4]@`, `u8@@`, `node@[4]`)
  that `src/bootstrap` refuses: those tests must be *rejected* under stage1 and must *run* under
  stage2, which one shared list cannot say. `test/lang/unsupported-stage2.txt` is stage2's list
  and `bootstrap-unsupported.txt` stays stage1's; `lang-stage2` names the first with
  `--unsupported`. Neither run excuses a test. A ticket that gives stage2 a feature stage1 lacks
  takes the entry out of stage2's list alone, and it also raises `NESTED_FILES`, `FORM_FILES` or
  `FLOAT_FILES` in `tools/diff_ast.sh`, which skips exactly the files stage1 refuses for a nested
  level, for a `do`-`while` or a `?:`, or for a float literal, and holds each number as an
  equality: the differential compares two
  trees, and a file only one compiler parses has no second tree. The second family carries one
  guard more, since its two forms are whole constructs and not a type suffix: stage2 must not
  report the refusal stage1 reports, which is what says the divergence is the intended one.
  T-044 is the second ticket of this shape, and two of its answers are worth keeping. A `fail`
  test whose subject is not the rejection -- `fail/operators/008_mixed_mutability_ternary.ft`
  asserts that the two arms of a `?:` must have one type -- leaves stage2's list like any `run`
  test, because stage2 now reports the error the test annotates. And a **new** test of such a
  feature is added to stage1's list, never to stage2's, since stage1 must still reject it.
  T-041 is the third, and it emptied stage2's list: floats were the last of the four families
  the C bootstrap lacks, so stage2 refuses nothing the corpus holds. The file stays for the next
  divergence. Three counters now share one loop in `diff_ast.sh` and a file that holds two of the
  constructs is counted by whichever test runs first, so the three numbers are read off the
  script's own failures and never computed.
  ctest `stage-usage` (label `unit`) diffs stage1's and stage2's `--help`, `--version` and
  usage line, which is what holds the option table `src/fort/main.ft` copies from
  `src/bootstrap/driver.c` to it.

## 5. Differential oracles and the fixed point

- **The fixed point is the ctest `bootstrap` (T-039) and the gate runs it.** `tools/fixpoint.sh
  <build-dir>` compiles `src/fort` with stage1 into stage2 and `src/fort` with stage2 into stage3,
  under `<build-dir>/fixpoint/<mode>/`, and it does that twice: once in checked mode and once with
  `--release`. The two modes emit different code, because checked arithmetic traps and release
  arithmetic does not (D11.1), so a fixed point in one mode does not prove the other. In each mode
  it holds stage1's module for `src/fort` against stage2's, runs `opt-18 -passes=verify` over both
  (D19.1), and compares the stage2 and stage3 binaries byte for byte. The module comes first
  because it names the guilty program: two identical modules that link to different bytes are
  clang or the linker. It is label `lang` and a command of `check-lang`, so `check-all` and the
  gate run it. It costs 12.2 s under `debug`, 12.7 s under `asan` and 14.4 s under `ubsan`
  (measured 2026-09-12), against 259 s for the whole of `check-all` under `debug`: most of the
  work is clang, which no preset instruments, and the two stage2 runs, which qemu runs and no
  preset instruments either. `tools/vm run 'ctest --preset debug -R bootstrap'` asks in one line.
  **Its binary comparison is the artefact check `tools/diff_ir.sh` cannot make.** That script
  compares the two emitters' module for `src/fort/main.ft` among 446 other files, and it is the
  stronger oracle for the emitter, but the step from "the two modules agree" to "stage2 and stage3
  are the same bytes" needs two assumptions that nothing checked before T-039: `clang` must be
  deterministic over one input and one command line, and `diff_ir.sh` compiles `main.ft` from the
  top of the worktree with `-I src/fort -I test/fort/support`, so the module it compares is not
  byte for byte the module that built stage2. Read the two halves of the `bootstrap` test apart.
  The **binary** comparison retires both assumptions, because it compiles and compares the
  artefacts themselves. The module comparison beside it rests on neither, since it compares two
  texts and links nothing, and it answers a different question: which of the two emitters is
  wrong. The binary half is also the only check in the repository that holds stage1's clang
  command line against stage2's. A different `-O` level, a different link order or a different
  `--target` in `src/fort/driver.ft` leaves both modules byte-identical and makes the binaries
  differ, which `test/driver_test.c` and `test/fort/driver_cc_test.ft` cannot see: each holds one
  compiler against its own written text.
  **A file path reaches the binary, so the two stages of a mode differ in nothing the module
  holds.** They differ in the `-o` path, which no module holds, and in `--cc`, which `-S` never
  reads; everything else is spelled the same way. The compiler names each
  file the path it opened it by (D14.2) and writes that path into the module as a `@.file.N`
  string (D19.6). Compiling `src/fort/main.ft` by its absolute path and by its relative path from
  the top of the worktree gives two binaries that differ in 27233 of 480088 bytes, and both run.
  `tools/fixpoint.sh` builds its own stage2 for that reason. Reusing `<build-dir>/stage2/fort`,
  which the CMake target compiles by absolute path, compared two different programs and reported
  `the emitted modules agree but the binaries differ` on a compiler that is a fixed point. It is
  also why the script writes no file another test reads: `<build-dir>/stage2/fort` belongs to
  `fort_stage2`, and `lang-stage2`, `diff-ir` and `stage-usage` judge it.
  **A comparison needs the guard that its inputs exist.** The script removes each binary and each
  module before it writes it, checks that the compiler wrote the binary, and holds each module
  against the first line of D19.1 before it compares the two: two empty files compare equal and
  `opt` accepts an empty module, so a compiler that exits 0 and writes nothing would otherwise
  read as a fixed point. `tools/diff_ir.sh` carries the same guard for the same reason. A stub
  `fort` that exits 0 and writes nothing is how both were proved: it prints
  `exited 0 and wrote no ...` and exits 1, and a stub that writes empty modules prints
  `is no module to compare`.
- **Phase B has four differential oracles, and the third is the only one that can see a false
  positive.** `tools/diff_tokens.sh` and `tools/diff_ast.sh` compare stage1's and stage2's
  `--tokens` and `--ast` over every `.ft` file of the repository; `tools/diff_check.sh` compares
  `fort --check` over the files stage1 checks clean, which is where the compiler's own thirteen
  thousand lines of fort and the standard library are; `tools/diff_ir.sh` (T-038, ctest `diff-ir`,
  a command of `check-lang`) compares `fort -S` byte for byte over every `.ft` file stage1
  compiles into a module -- every `run` and `programs` test, every `test/fort` module test, and
  `src/fort/main.ft`, which is the compiler emitting itself. That last is the strongest single
  case there is, and the script is the strongest of the four, since D19.5 makes the text a
  function of the program alone, so a type the checker built differently, a constant it folded
  differently or a symbol it resolved differently all reach the text. What it cannot see is a
  construct the corpus does not spell, and whether stage1's own text is right.
  The language corpus under stage2 holds the
  diagnostics a ported pass must *report*; only diff_check holds the ones it must not, and T-035
  measured the difference: reverting T-082's `identity_only` in `named_type` left the whole
  495-test stage2 corpus green and was caught by diff_check, on
  `run/structs/008_recursive_span_first.ft`. The unit suite the same ticket added
  (`check_resolve_test.ft`) catches it too, and that is the shape to aim for -- the differential
  finds the class, a named assertion pins it -- so do not read the story as "the differential is
  enough". All four carry the same `FT_FILES` equality, so a ticket that adds or removes a `.ft`
  file changes **four** lines in the same commit, and `diff_check.sh` and `diff_ir.sh` each carry
  a second equality, `CLEAN_FILES` and `PROGRAM_FILES`, because a comparison that shrank would
  otherwise pass while seeing less. `diff_ast.sh` carries three more, one for each construct
  stage1 alone refuses: `NESTED_FILES`, `FORM_FILES` (a `do`-`while` or a `?:`) and
  `FLOAT_FILES`. So a `.ft` that uses one of the three moves two constants and not one, and
  T-042 moved **seven lines** for eleven files, four constants over five files: `FT_FILES` 842 to
  853 on four lines, `FORM_FILES` 12 to 13 for `std/math.ft`, `FLOAT_FILES` 63 to 67 for four
  tests, and `CORPUS_FILES` 604 to 615 in `test/highlight_test.py`. Count the lines and not the
  names: `git diff main...HEAD -U0 | grep -cE '^\+.*[A-Z_]+_FILES *='` prints one line for each,
  which is the number a ticket must move. Read each new value off the failure of the tool that
  owns it and never compute one: two branches that raise one constant by the same step merge with
  no conflict and are then both wrong.
  **The command anchors on the assignment and not on the start of the line, because one of these
  constants is not at the start of a line.** `test/parser_recovery_test.c` declares its
  `CORPUS_FILES` inside an `enum` line, so the earlier command,
  `grep -E '^\+[A-Z_]*FILES'`, could never see it: it printed 7 for T-045's diff where the answer
  is 8. The thing that counts the count constants was itself miscounting. It entered this file
  with T-042 (74dddb3) and stood until T-045 measured it, because it returned a number and a
  number reads as an answer. Use the command above, and check what it prints against the
  constants you know you touched.
  **A search root moves a second equality, and a new file may move none.** The two rules are the
  same rule read from each end. `-I src`, which T-063 added to `diff_check.sh` and to
  `diff_ir.sh` so that the language server resolves, took `CLEAN_FILES` from 458 to 472 and
  `PROGRAM_FILES` from 471 to 492, because a root that resolves an import moves a file out of the
  skipped bucket and into the compared one. T-042 added eleven files and moved neither, because
  stage1 refuses all eleven and the refused count rose by eleven with them. So the two second
  equalities do not follow from the number of files a ticket adds in either direction, and two
  branches that both moved them cannot add their increments: **rebase, then read all seven
  numbers again, one command per file** (T-063, rebased onto T-042).
  **A `fail` test moves two numbers a `run` test does not.** The first is
  `test/parser_recovery_test.c`'s `CORPUS_FILES`, which counts the `fail` corpus alone; the second
  is the comment on `lang_check_json-stage2` in `CMakeLists.txt`, whose three numbers are the
  corpus total, the `fail` tests and the tests `--check-json` selects, and a `fail` test carries
  diagnostics so it moves all three. A `run` test moves the total alone. So ask which kind of test
  you added before you count, and read
  `test/lang/run_tests.py --check-json --list | wc -l` and `| grep -c '^fail/'` for the second
  pair rather than adding one to the comment.
  T-045 is the worked example and moved **eight lines**, in six files, over five different
  constants. Ten `.ft` files that stage1 checks clean and one `fail` test that it refuses took
  `FT_FILES` 876 to 887 on four lines, `CLEAN_FILES` 472 to 482, `PROGRAM_FILES` 492 to 501 --
  nine of the ten hold a `main`, and `std/sort.ft` does not -- `CORPUS_FILES` 638 to 648 in
  `test/highlight_test.py`, which does not walk the `fail` directory, and `CORPUS_FILES` 233 to
  234 in `test/parser_recovery_test.c`, which walks nothing else. The `CMakeLists.txt` comment
  moved with them, 628 to 638 tests, 219 to 220 `fail` tests and 221 to 222 selected. So the
  number of lines is not the number of constants, and neither is the number of files.
- **A ported pass is judged on its diagnostics one by one, with a script and not a reading.**
  For every message the ported file builds -- each `check_error` text and each run of `msg_str`
  pieces between `check_msg_begin` and `check_msg_end` -- ask whether any suite under `test/fort`
  holds a piece of it. T-035's review ran that over five suites and found **22 diagnostics with no
  fort test, 11 of them asserted by the very C suites the ticket was porting**, because only 6 of
  its 85 test-function names matched a C `TEST` name: the suite was a re-derivation and nothing
  mechanically caught what fell out. `tools/diag_coverage.py` is that script; it reports
  candidates, and a composed message whose only distinctive piece is a shared hint is a false
  positive, so the few it leaves are verified by hand and the count is written into the ticket.
  Run it before claiming any coverage universal, and name the *measured* list rather than "every
  rule is tested".

## 6. Generated code and the cross pipeline

- The cross pipeline: `test/ir/*.ll` are hand-written LLVM 18 modules in the form
  `spec/toolchain.md` 6 specifies (D19.1); `hello.ll` and `abort.ll` are its two worked
  examples byte for byte, so a change to one changes the other, while `colons.ll` answers a
  question of its own (`test/ir/README.md` says which). `test/pipeline_test.sh`
  verifies each with `opt-18 -passes=verify`, compiles and links it with `clang
  --target=x86_64-linux-gnu` alone -- a module holds the whole program, the runtime included, so
  each file defines the handful of `std.rt` entry points it calls over libc -- runs it under
  qemu and checks stdout, stderr and the status byte-exactly; ctest `pipeline` (label `unit`).
  Adding one means
  adding its name to the `for prog in` loop of that script and a block of expectations beside
  the others; the list is not globbed, since each module's output is its own. `*.ll` is
  gitignored except `test/ir/*.ll`. `run_tests.py --verify-ir` runs the same verifier over the
  `-S` output of every language test that compiles.
- **The gen suites emit with no runtime in the closure**, so the calls the emitter writes into it
  reach a name the module neither defines nor declares, which LLVM rejects as a forward reference
  to nothing. `verified()` appends a `declare` for every row of `runtime_sig.h` to the file the
  verifier reads and to nothing else, so `ir()` stays the emitter's own text
  (`gen_runtime_declarations` in `test/gen_helpers.h`, which skips a name the module defines).
  A test that needs the runtime's own definitions -- the `#8` of item 14, the `%fort.enum_member`
  of item 2, the dependency order -- calls `emit_with_runtime`, which writes a small `std/rt.ft`
  in the sandbox and names that directory. A `fn noreturn` in such a stub needs a terminating
  statement (D8.4, D8.5): an empty body is `a noreturn function must end in a terminating
  statement`, and `while (true) { }` is the shortest one that asks for no intrinsic of its own.

## 7. What a test cannot see

- **A terminal is a test environment no pipe can stand in for.** `test/lang/run_tests.py` captures
  a program's stdout through a pipe, so a rule that only holds on an interactive descriptor --
  D11.5's line buffering -- is invisible to the whole language corpus, which stays green with the
  feature deleted. `test/tty_test.py` (ctest `tty`, label `unit`) is the shape that sees it: it
  compiles one program with the built compiler and runs it twice, under `pty.fork()` and under
  pipes, asking what has arrived while the program is still blocked in a read of stdin. Two
  traps cost T-083 an hour between them. A pty master loses whatever is still in the line
  discipline once the last slave closes, so the program blocks a second time and the parent reads
  the line **while the child lives** rather than after it exits. And a `done` predicate that stops
  at a substring (`"bye" in text`) returns before the newline that follows it arrives in the next
  chunk, which is a flake of about one run in five: wait for the whole line (`"bye\n"`), and
  prove a pty harness is not flaky with `ctest --repeat until-fail:20` rather than one green run.
  It drives one program for each runtime, `test/tty/print_then_wait.ft` and
  `rt_print_then_wait.ft`, because `std.rt` and the C runtime are both live until T-091 and each
  one holds its own buffers.
- **A blocking write never returns short, so no test here witnesses a retry loop over `write(2)`.**
  POSIX makes a blocking write to a pipe or to a file return only after it has written every byte.
  `write_all` in `std/rt.ft` therefore runs its loop a second time only after `EINTR` or on a
  non-blocking descriptor, and a fort program can arrange neither: `std/libc.ft` declares no
  `pipe`, no `mkfifo` and no `socket`, and `O_NONBLOCK` does nothing on a regular file. A
  200000-byte write to the harness's pipe arrives whole, and cutting that loop to a single `write`
  left `run/stdlib/090` green. The `EINTR` branch beside it is uncovered for the same reason, so
  name **both** branches when you record the gap. Assert that every byte arrives, which does catch
  a bypass path that drops bytes, and write in the test what it cannot see instead of claiming the
  loop.
- **A mutation audit measures what the corpus holds, which the ratio cannot.** T-077 broke, one at
  a time, one line of each of the 88 decisions `src/fort/check.ft` and `src/fort/check_stmt.ft`
  cite. It ran five oracles against each mutant, in cost order: the 219 `fail` tests under stage2
  (8 s), the 415 `run`, `programs` and `ffi` tests (35 s), the check documents (13 s), the
  `test/fort` tests (13 s for the 15 the first run selected, 120 s for all 161) and
  `tools/diff_check.sh` (39 s). A stage2 rebuild after one edit costs 5 s, so a mutant costs about
  45 s when the first oracle catches it and about 115 s when none does. **79 of the 88 went red,
  4 survived and 5 could not be broken by a mutation at all**; the five are the rows where a
  citation names a rule another pass holds, and each is a build error or a lint failure rather
  than a test.
  Three of the four survivors had one shape, and the shape is the reason to run this on a
  transliterated module: the C twin is pinned by a unit test in `test/*_test.c` that the fort port
  never got, while the language corpus, which judges both compilers, held the rule for neither.
  Mutating the three C lines names the three tests that hold them. The fourth survivor was held on
  neither side, which the same method measured.
  Four traps, each of which cost that ticket a wrong verdict or a wrong claim.
  **Select the oracle by what a test imports, not by its name.** A filter of `check` over
  `test/fort` ran 15 tests where 21 import the checker, so every "survived" verdict was a
  survival against 15 of 21 until the four were re-run against all 161.
  **Do not edit the test tree while a batch runs.** A harness that reads a file during the write
  reports a failure that belongs to no mutation.
  **Raise every file counter in the commit that adds the file.** A new `.ft` raises `FT_FILES` in
  the four `tools/diff_*.sh`; `CLEAN_FILES` in `diff_check.sh` if stage1 checks it clean;
  `PROGRAM_FILES` in `diff_ir.sh` if it compiles into a module; `CORPUS_FILES` in
  `test/parser_recovery_test.c` if it is a `fail` test and in `test/highlight_test.py` if it is
  not; and `NESTED_FILES`, `FORM_FILES` or `FLOAT_FILES` in `diff_ast.sh` if it uses a nested
  array or span level, a `do`-`while` or a `?:`, or a float. A counter left behind fails
  `diff_check.sh` for every later mutant, which then reads as caught when nothing caught it.
  **Say how strong each verdict is.** A verdict a mutant measured, a claim probed by compiling a
  program, and a claim read off the source are three things, and an audit that gives them one word
  hides which rows a reader may rely on.

## 8. The grammar and the extension

- The TextMate grammar is checked by `test/highlight_test.py` (ctest `highlight_selftest`, label
  `unit`, run from `test/`): it reads the D2.4 keyword lists and the D2.10 operator list out of
  `spec/decisions.md` and the same sets out of the grammar, so the two cannot drift. It reads them
  through `check_decisions.rule_of`, which returns the `rule` field of one entry, and
  `test/fort_lint_test.py` reads the D20.3 kind list the same way (T-100): a test that opens the
  decision log calls that one parser rather than matching the entry shape itself, and it collapses
  the whitespace of the rule before it searches for a sentence, because the rule wraps at 100
  columns and a line break must not decide whether a test passes. That only
  works while the rules keep their canonical shapes, `\b(?:a|b)\b` for keywords and `(?:\+|-)`
  for operators; a rule whose scope is in a keyword or operator family but whose pattern matches
  neither shape fails the test. Its other half is a small TextMate engine that asserts the scopes
  of `test/highlight/scopes.ft` (`//^` lines: alternating text and scope fields naming what the
  line above must produce), of the D5.3 and D17.2 marker tables, and of **every fort source the
  project writes**, which must tokenize with no `invalid.` scope and no unscoped character, so a
  new file the grammar mishandles fails here. `CORPUS_DIRS` is that list -- `test/lang/run`,
  `test/lang/programs`, `std`, `src/fort`, `src/lsp`, `test/fort` (its `support/` included) and
  `test/fort_lint` -- and `CORPUS_FILES` is the exact number of files in it, so a ticket that adds
  or removes a `.ft` under any of them reads the new number off the failure and writes it there,
  as it does for `CORPUS_FILES` in `test/parser_recovery_test.c` and `FT_FILES` in
  `tools/diff_tokens.sh`. The other `.ft` of the repository are listed in `EXCLUDED_DIRS`, each
  because it is meant to hold a lexical error (`test/lang/fail`, `test/highlight/scopes.ft`,
  `editors/vscode/test/fixtures/lexical.ft`), and a test asserts that partition, so a new
  directory of fort is a red test rather than a corpus nobody tokenizes -- which is what
  `test/fort` and `test/lang/programs` both were until T-079 measured it.
- The VS Code extension is plain JavaScript on the VS Code API, with no npm dependency and no
  build step. Its logic lives in `editors/vscode/lib/check.js`, which never `require('vscode')`, so
  Node's built-in runner tests it: `tools/vm run 'cd editors/vscode && node --test'` (ctest
  `extension_selftest`, label `unit`, run from `editors/vscode`; `node --test` with no argument
  discovers `test/*.test.js` itself, and naming the directory fails on newer Node). `extension.js`
  is the only file that may touch the API, so keep it thin and move anything with a case analysis
  into `lib/`; it is driven through `test/fake_vscode.js`, which answers its `require('vscode')`
  and its `require('child_process')` by patching `Module._load` before loading a fresh copy of it,
  so a save, a failed run, a close and two checks racing are all tested with no editor, no VM and
  no compiler. **A check answers about a closure, not about one file**, so an ordering guard keyed
  on the file that was checked is not enough: the first version dropped a superseded run of the
  same file and still let an older run of `main.ft` repaint an error in `mathx.ft` that a newer
  check of `mathx.ft` had just cleared. The generation is therefore recorded per *published* file
  as well, and a test that means to see that has both files in **one** closure -- two disjoint
  closures pass either way. What that cannot check is that VS Code calls the extension the way its
  API is documented to, which is what the manual smoke test in `editors/README.md` is for, and a
  change to `extension.js` is run through it by hand -- by the user, since VS Code runs on their
  machine and an agent cannot reach it. Its fixtures are real compiler output: regenerate them with
  `fort --check --json` over `editors/vscode/test/fixtures/` rather than by hand, and a helper that
  is not a suite, such as `test/fake_vscode.js`, defines no test of its own, since Node 18 loads
  every file under `test/`.
- **What checks `.ft` source, and what does not** (T-076). Three things do. `tools/fort_lint.py`
  (ctest `fort_lint`, target `fort-lint`) holds `std/*.ft` and `src/fort/*.ft` to the identifier
  conventions of D1.4 and to 100 columns; it reads `fort --index` (D20.3) rather than tokenizing
  fort a second time, so the kinds and types it reasons about are the checker's own answers, and a
  second tokenizer cannot drift from the language. `test/highlight_test.py` tokenizes `std/`,
  `src/fort/` and `test/lang/run` against the TextMate grammar, which is the only check that grammar
  has. `tools/lines.py` counts `std/*.ft` on the source side of the 3:1 ratio, with the compiler and
  the runtime (D14.6, amended). What still does not: **there is no formatter** -- indentation,
  spacing, brace placement and blank lines in `.ft` are review's alone, since `.clang-format` has no
  fort equivalent; the lint sees only what the checker resolved, so an unresolved name is judged by
  nothing (D20.3 gives it no record), though the names around it in a file that fails to compile are
  judged as usual and the error is reported beside them; `test/lang/**` is deliberately outside the
  lint, because a test exercises the language rather than exemplifying the conventions; and nothing
  checks import order, doc comments or dead code. Block comments need no check: `/*` is a lexical
  error in the compiler itself (D2.2). A new fort source outside `std/`, `src/fort/`
  and `src/lsp/` is checked by nothing until a glob in `fort_lint.py` names it.
  **One `fort --index` run judges every file
  of the closure it indexed**, not only the file it names: the run indexes the whole import
  closure and each record carries its own file (D20.3), so `lint_files` takes the first file it
  has not judged as the next entry and reads the records of every file of the set out of that one
  document. One run per file re-checked each closure once per member, which is O(n^2) checker
  work: the 166 files of the default set took 166 runs and 46.9 s under the debug preset, and take
  141 runs and 14.9 s this way (T-095). 127 of those 141 runs are the `test/fort` tests, which no
  module imports, so 141 is near the floor until `fort` accepts more than one entry file. Four
  rules hold the new shape to the old verdict, and a change there must keep all four. A file the
  document does not name stays pending and becomes an entry itself, so no file goes unjudged; a
  run judges a file only when that file's search roots equal the entry's, since a file keeps the
  roots its own set gives it (D9.2), and the default set loses no run to that rule; a run that
  fails gives its error to its entry alone and leaves the other files a run each; `real_path`
  memoises `os.path.realpath` per distinct name, because one document holds 33000 records and the
  uncached filter cost 0.39 s per file, more than the compiler run it filtered. The one behaviour
  that did change: a run's diagnostics go to every file that run judges, so a broken module now
  names itself once in each file of its closure rather than once in each file that imports it.

## 9. The test-to-code ratio

- Every ticket meets the 3:1 test-to-code ratio on its own diff, not on the repository average:
  `python3 tools/lines.py --since main --min 3.0` is an acceptance criterion of every
  ticket that adds **source** lines -- the compiler, `std/*.ft` and the runtime, which are what
  D14.6 counts since T-076, so a ticket writing only library or runtime code answers for its
  tests like any other -- and the implementor runs it before the gate. The repository ratio
  drifted from 3.34 to 2.50 over six tickets while every one of them passed, because a large corpus
  hides a thin diff, and it measured 2.41 on 2026-09-11 (22776 source lines against 54891 test
  lines, the first figure to count `std/` and the runtime). The two numbers answer different
  questions and both are working: the per-ticket rule is not retroactive, so the corpus figure is
  a lagging indicator of everything that landed before it and climbs only asymptotically even if
  every future ticket hits 3:1 exactly. Measure it with
  `tools/vm run 'python3 tools/lines.py'` before quoting it; never repeat the figure from this
  line. A ticket that cannot reach 3:1 says so in its log with the reason rather than lowering the
  number. Run it on the host when the worktree has a VM of its own, because git in that guest
  cannot open the main checkout (`notes/environment.md` 3, T-099); `tools/vm run 'python3
  tools/lines.py --since main --min 3.0'` works only in a VM brought up from the main checkout.
  `--since` reads `git diff main...HEAD`, so it counts **committed** work only. A branch
  whose tests are still staged or untracked reads as the ratio of the commits before them. That
  number is smaller than the truth, and it sends an implementor off to write tests the branch
  already has. Commit first, then measure.
- `tools/lines.py` does not see `editors/`. A branch that changes the VS Code extension alone
  therefore has no figure from the tool, and the criterion is met by hand. Count with `git diff
  --numstat main...HEAD` and the tool's own convention: net lines, raw `wc -l`, comments and blank
  lines counted on both sides. T-092 measured 208 net source lines against 710 net test lines,
  which is 3.41. Its first count reported 2.70, because it compared added lines with added lines
  and dropped the deletions. State the two numbers and the command; do not report a figure per
  non-comment line, which no tool here uses. Count the corpus figure on `main` and not on the
  branch. The extension measured 496 source lines against 1154 test lines there, which is 2.33.
  That figure lags for the same reason the repository figure does: the per-ticket rule is not
  retroactive. The branch figure is the one the criterion asks for. T-089 and T-092 both
  rediscovered this rule.
- The ratio is a prompt, not a verdict: what a review asks is which rules of the decisions a ticket
  cites have no test at all, unit or language, and the answer decides the ticket. T-014 measured
  0.76 and merged, because the number could not see that it took 57 entries out of xfail.txt -- a
  body of language tests that already existed and only then began to exercise the code, adding not a
  line to the diff -- while the coverage question found three rules that were untested and also
  broken (a noreturn function type that never matched, an unchecked shift count that made the two
  build modes disagree, and an index expression that handed out a pointer into a dead temporary).
  Ask a reviewer for the list of untested rules whenever a ticket misses the ratio, and merge or
  refuse on that list.


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

- **Quote a ctest label total with the tree it was measured on.** A label total is the volatile
  number, because any ticket may register a suite: T-078 added `mutate_selftest`, so `ctest -L
  unit` moved from 78 to 79 and every gate criterion of 2026-09-12 and 2026-09-13 that says "0
  tests failed out of 78" now meets a reader who finds 79. T-103 then added `knowledge_lint` and
  `knowledge_lint_selftest`, which take the label to 81. Each of those lines was true of its own
  tree, and none of them becomes false, but a reader comparing two tickets meets two numbers for
  one label. Write "78 unit tests of that tree", or name the sha. A corpus count carries its own
  guard and needs no qualifier, because it moves only when a `.ft` file is added or removed, and
  the `*_FILES` equalities make that a counted edit that fails a test (T-078).

- **A measurement that several tests share is stated once, in the module they import.** T-094
  measured 46 releases over the six `test/fort/driver_lifetime_*_test.ft` probes and wrote the
  count into a sentence that five of the six files repeat. Its review then added three releases
  and re-worded four of the five, and the fifth said 43 for two tickets, until T-118 re-ran all
  47 deletion rounds to find out which number was the measured one. The count, the command that
  produces it and the one release that no single test witnesses now stand in
  `test/fort/support/lifetime.ft`, which all six probes import, and the six say only that the
  measurement was made. A number that five files repeat needs five correct edits at the next
  measurement; a number in the module they share needs one (T-118).

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
  On voyager.local, `/bin/false` is absent. This made three inherited fixture tests error (T-143).
  Use `shutil.which("false")` for a fixture that runs on Mac and Linux.
  The host and slot-2 VM suites each pass 185 of 185 tests with this fixture.
- A directive lint gotcha: `run_tests.py --lint` rejects any line of a test whose text holds `//!`
  after code, so a comment inside a test that quotes a directive (`the //! stderr: lines`) fails
  the lint with `only '//! error:' may follow code on a line`. Say "the stderr directives in this
  test's header" instead.
- **The harness judges a line, not a count, so a `fail` test cannot hold the number of
  diagnostics.** `judge_fail` groups what the compiler reported by `(file, line)` and asks whether
  each annotated line carries a message with the expected text. A diagnostic that is **added**
  beside a right one on the same line, or **doubled**, leaves the whole corpus green. T-117
  measured it on 2026-09-13: dropping a guard added `'<<' takes an integer left operand, not
  <error>` at column 20 of line 3 and `646 tests: 646 passed` stayed green. T-124 measured it
  again on 2026-09-14: a guard that reported where a child had already reported left
  `648 tests: 648 passed`. Both times one unit test went red and nothing else did. So a ticket
  that adds, moves or removes a diagnostic asserts the count where a test can read it --
  `diag_lines()` in `test/*_test.c` or `check_env.errors(&e)` in `test/fort/*_test.ft` -- and
  uses the `fail` test for the text and the position.
  **A diagnostic that is removed is as invisible as one that is added, and neither directive can
  see it** (T-129, the first mutation measurement of a removal). `judge_fail` asks whether an
  annotated line carries a message, so a line that loses one of two keeps its annotation; and
  `_stderr_problems` (`run_tests.py:670`) reports only `stderr lacks '<s>'`, so `//! stderr:`
  asserts presence and has no negative or counting form. Measured on T-129: the guard that
  silences `the expression expects <error>, not a constant` took `fail/constants/012` from 27
  diagnostics on 13 lines to 14 and the whole corpus from 14 such diagnostics to 0, and the
  corpus read `665 tests: 665 passed` with the guard reverted in both compilers. Three of the
  five tests of `test/check_poison_test.c` went red under that mutant and `test/fort/check_test.ft`
  aborted. A removal therefore needs the count and nothing else will do.
  **The position a `//! error:` annotation pins is the line alone. `//! stderr:` pins the
  column** (T-128). The harness matches an annotation to a diagnostic by `(file, line)` and by
  substring, so a report that moves along its line changes nothing it can see; a `//! stderr:`
  line is a substring of the compiler's whole stderr, repeatable, and a `fail` test may carry
  several, so `//! stderr: :39:13: error: constant 9223372036854775808 does not fit i64` pins the
  line, the column and the text together. Reach for it when the **column** is the rule under
  test -- a diagnostic that must stand at an operand rather than at the operator around it -- and
  not otherwise, because each one names a line number and moves when the file above it grows.
  **The file is the second thing it pins, and the first thing nothing else can see** (T-112). An
  annotation lives in a file of the test, so a diagnostic that moves to another file leaves its
  annotation unmatched and the run fails for the missing annotation rather than for the rule: the
  test then reads as one that lost a diagnostic, and the reader learns nothing about where the
  diagnostic went. A `//! stderr:` line that carries the path as the compiler spells it says the
  rule instead. Measured on T-112: swapping the error and the note of `conflicting declarations
  of extern` took `fail/modules/009_extern_local_types` red both ways, but only with the
  directive did the run name `stderr lacks 'fail/modules/009_extern_local_types/main.ft:18:22:
  error: conflicting'`. So reach for it when the **column** or the **file** is the rule under
  test, and not otherwise.
  Measured on T-128: a mutant that reported the pair's constants at the operator moved the column
  from 13 to 33 and left the line and the text identical. With the two `stderr:` lines the corpus
  read `664 tests: 663 passed, 1 failed` and named only those two substrings; with them removed
  it read `664 tests: 664 passed`, the C suite exited 0 and `test/fort` read
  `173 tests: 173 passed`. `fail/constants/014_a_comparison_of_two_untyped_operands.ft` was the
  first of the 246 `fail` tests to use the directive.
- **A `fail` test may not mix a lexical or a syntax diagnostic with a semantic one.** `lex_file`
  reports, and the driver then stops before the checker runs (D14.2), so a file whose lexical
  error is annotated alongside an expected type error never produces the second one and the run
  fails on the missing annotation. Split them into two files. Found by T-029 while filling the
  `fail/lexer` gaps. A parser diagnostic does the same thing, and it is easier to write by
  accident, because a marker the parser refuses reads like a marker the type builder refuses:
  T-086 wrote five bad `void` declarations into one test and got three parser messages and
  nothing at all for the two the checker owns, `void mut v` and `void mut@ s`. Check which pass
  owns each message (`fort --check` prints them all) before annotating a second line.
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
- **Run a `test/fort` binary from a scratch directory, never from `test/fort`.**
  `check_env.open` and `modules_env.open` create `sandbox<n>/` **relative to the working
  directory** (`test/fort/support/modules_env.ft:56`) and `close` never removes it (`:64`), so a
  suite run by hand from the corpus root writes its sandboxes into the source tree. T-117 ran
  `check_test.ft` that way, `git add -A` swept 50 `test/fort/sandbox*/main.ft` into a commit, and
  the four `.ft` count constants would have gone wrong at the next measurement. `run_tests.py`
  gives each test a directory of its own under `build/`, so the harness never leaks one; only a
  hand-run does. `.gitignore` now holds `test/fort/sandbox*/`, which closes the commit half of it
  whatever anyone remembers. **The ignore line does not close the other half**: a tool that walks
  the filesystem still counts the directories, and `git status` no longer shows them. Measured on
  T-117's fix round, a second hand-run left 51 sandboxes and `python3 tools/knowledge_lint.py`
  read `citation: 407 files` against the 356 of a clean tree, with an empty `git status --short`.
  So compile with `-o` into `build/` and run from there, and read `ls -d test/fort/sandbox*`
  rather than `git status` when a count comes out wrong.
  **The working directory is any directory, and the ignore line names one** (T-127). A hand run
  from the **top of the worktree** writes `./sandbox<n>/`, which `test/fort/sandbox*/` does not
  match. One run of `check_test.bin` from there left 196 `sandbox<n>/main.ft` files in the root,
  and the next `tools/diff_ir.sh` read `found 1132 .ft files, expected exactly 936` and exited 1.
  `git status --short` did show all 196, which is the one advantage of the root over
  `test/fort/`, and it is why this line is a rule about the working directory and not a second
  `.gitignore` entry. Remove them with `rm -rf sandbox[0-9]*` and read
  `find . -name '*.ft' -not -path './build/*' | wc -l` against `FT_FILES` before you run any
  oracle.
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
  claimed to witness was unwitnessed. The program break (`extern fn sbrk(i64) void*`, `sbrk(0)`)
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

- **A test that drives a program over a pipe must size the script against the pipe, which holds
  64 KiB.** Nothing drains the pipe while the program under test reads it, so a script above the
  capacity blocks the writer -- the test itself -- and the run dies at `run_tests.py`'s 60 s
  timeout with no output to read. `test/fort/lsp_protocol_test.ft` writes its whole script and
  closes the writing end before the server starts, which is safe for the few hundred bytes of a
  recorded exchange and for the answers, which are smaller. A script of two thousand messages
  goes through a **file** instead (`io.write_file`, then `io.open_read`), which has no capacity:
  `test/fort/lsp_server_lifetime_test.ft` does that, and the harness gives each test a directory
  of its own, so the file is the test's alone (section 3). Two pipes and not one, when the
  program answers: one for its input, one for its output, and the output is drained after the
  loop ends (T-064).
- **A binary a CMake target builds needs a test that executes the binary**, and a suite of the
  modules inside it is not that test. Every `test/fort` suite of `src/lsp` drives the modules in
  one program of its own, so `src/lsp/main.ft` and the wiring around it -- stdin and stdout as
  the descriptors, the loop's status as the process status -- were covered by nothing until
  `test/lsp_binary_test.sh` fed the binary a recorded script (ctest `lsp-binary`, label lang,
  since it runs an x86-64 binary under qemu). Hold such a script against a wrong binary before
  trusting it: this one exits 1 for `/bin/cat` and for `build/<preset>/fort` (T-064).

## 5. Differential oracles and the fixed point

- **The fixed point is the ctest `bootstrap` (T-039) and the gate runs it.** `tools/fixpoint.sh
  <build-dir> --bootstrap <fort>` compiles `src/fort` three times, under
  `<build-dir>/fixpoint/<mode>/`: with the last pin into stage2, with stage2 into stage3, with
  stage3 into stage4. It does that twice: once in checked mode and once with
  `--release`. The two modes emit different code, because checked arithmetic traps and release
  arithmetic does not (D11.1), so a fixed point in one mode does not prove the other. In each mode
  it holds stage2's module for `src/fort` against stage3's, runs `opt-18 -passes=verify` over both
  (D19.1), and compares the stage3 and stage4 binaries byte for byte. The module comes first
  because it names the guilty program: two identical modules that link to different bytes are
  clang or the linker. It is label `lang` and a command of `check-lang`, so `check-all` and the
  gate run it. It costs 12.2 s under `debug`, 12.7 s under `asan` and 14.4 s under `ubsan`
  (measured 2026-09-12, before T-131 added the third compile), against 259 s for the whole of
  `check-all` under `debug`: most of the
  work is clang, which no preset instruments, and the stage2 and stage3 runs, which qemu runs and
  no preset instruments either. `tools/vm run 'ctest --preset debug -R bootstrap'` asks in one line.
  **Why the pair moved one hop along, and why D19.5 is unchanged** (T-131). The compiler that
  builds HEAD is the last pin of `tools/bootstrap.ref`, and the last pin and HEAD are different
  programs. Their `-S` texts differ on any commit that touches the emitter, so a comparison of the
  pin's module with stage2's would go red on a change that is not a defect. Both members of the
  pair D19.5 compares must embody HEAD's sources. stage2 and stage3 are the first such pair:
  HEAD's sources through two different compilers. stage3 and stage4 are the second, and their
  binaries are the artefact comparison. Two distinct stages, one input, two `-S` runs and no
  third, which is what D19.5 asks for and what its last sentence forbids.
  **Its binary comparison is the artefact check `tools/diff_ir.sh` cannot make.** That script
  compares the two emitters' module for `src/fort/main.ft` among the other files it walks --
  `grep -n 'PROGRAM_FILES=' tools/diff_ir.sh` reads 530 on 2026-09-14 -- and it is the
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
  enough".
  **T-078 measured the same shape from the other side, in the emitter, and found one rule that
  only `diff_ir.sh` held.** It broke, one at a time, the line implementing each of the 72
  decisions `src/bootstrap/gen.c`, `gen_expr.c`, `gen_stmt.c` and `gen_data.c` cite. 69 of the 72
  turned a test red, and 68 of those died in the emitted-text suites. The 69th is the one to
  learn from. `gen_int_bits` answering 8 bits for a `bool` instead of 1 left all 78 unit tests of
  that tree green. It left ten of the eleven tests of the `lang` label green. It turned only
  `diff-ir` red, on 1 of 492 compared files. A pure differential says that stage1 and stage2
  disagree. It never says which of them is wrong, so the same mistake ported into `src/fort`
  takes the last witness away. The assertion that now states the rule is
  `a_bool_widens_to_a_byte_because_its_value_is_one_bit` in `test/gen_cast_test.c`. It needs an
  **8-bit** target: a `zext i1` to `i32` reads the same whether the source is called one bit wide
  or eight. Read a differential's red as "the class is here", never as the rule's witness. The
  method, the runner and the traps of that audit are in section 7, beside T-077's.
  **The differentials use the C oracle library, not a production pin** (T-151). `diff_check.sh`
  and `diff_ir.sh` name `<build-dir>/oracle/std` on both sides. The build extracts it from
  `tools/bootstrap-oracle.ref`. This keeps the frozen C language subset independent from the
  source pins in `tools/bootstrap.ref`. Giving both compilers one directory also keeps their
  emitted paths equal. HEAD's `std/*.ft` files stay in the file list and are compared one by one
  until one uses a form the C compiler refuses. The fixed point and `fort-modules` judge HEAD's
  complete standard root and compiler sources.
  All four scripts carry the same `FT_FILES` equality, so a ticket that adds or removes a `.ft`
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
  no conflict and are then both wrong. T-107 measured that case on a rebase over T-077: five
  counters conflicted and `PROGRAM_FILES` did not, because both branches had written 502 over the
  501 they shared, and the answer is 503. **The one that does not conflict is the dangerous one**,
  since nothing in the rebase asks you to look at it. So after a rebase read every counter of the
  six off its own tool, not only the ones git stopped on.
  **The command anchors on the assignment and not on the start of the line, because one of these
  constants is not at the start of a line.** `test/parser_recovery_test.c` declares its
  `CORPUS_FILES` inside an `enum` line, so the earlier command,
  `grep -E '^\+[A-Z_]*FILES'`, could never see it: it printed 7 for T-045's diff where the answer
  is 8. The thing that counts the count constants was itself miscounting. It entered this file
  with T-042 (74dddb3) and stood until T-045 measured it, because it returned a number and a
  number reads as an answer. Use the command above, and check what it prints against the
  constants you know you touched.
  **A line can move a file from one bucket of `diff_ast.sh` to another, and the two constants
  then move in opposite directions** (T-127). The bucket is not the first construct stage1
  refuses. stage1 reports every one of them, and `diff_ast.sh` greps the whole of stage1's
  stderr in one fixed order -- `NESTED_MESSAGES`, then `FORM_MESSAGES`, then `FLOAT_MESSAGES` --
  and the first grep that matches takes the file. So a file that draws two messages counts in the
  higher bucket, wherever the two constructs stand in it.
  **A `?:` with a float arm draws no `?:` message at all**, which is what moved
  `test/lang/fail/constants/010`. `parse_ternary` (`src/bootstrap/parser.c:1250`) calls
  `unsupported(p, loc, "?:")` only after both arms parse, and `parse_primary`
  (`parser.c:1023`) returns `NULL` for a float literal, so the `n->b == NULL || n->c == NULL`
  test above it returns first. Measured on `main`: that file held four `?:`, every one with a
  float arm, and `build/debug/fort --ast` printed 12 `float literals` messages and no `?:`.
  The column plays no part. `println(n > 0 ? 1 : 2.5);` stood in that file with the `?` at
  column 19 and `2.5` at column 25, and stage1 still reported only the float, at `20:25`.
  T-127 added `println((n > 0 ? 'a' : 'b') == 1.5);`, whose two arms are char literals and parse,
  so stage1 reports `?:` at `48:20` beside 13 `float literals`, the `FORM_MESSAGES` grep matches
  first, and `FORM_FILES` went 15 to 16 while `FLOAT_FILES` went 68 to 67, on one file and with
  no file added. To predict the bucket, run `fort --ast <file>` and read every message it prints,
  not the source. Run the script twice as well: it reports one bucket at a time, `NESTED_FILES`
  first, then `FORM_FILES`, then `FLOAT_FILES`, so the second failure is invisible until the
  first is fixed.
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
  pair rather than adding one to the comment. **Take one off that `wc -l`**: the last line `--list`
  prints is its own summary (`222 tests`) and not a test, so the pipe answers 223 where the comment
  says 222. T-107 read 641 and 223 for a branch whose numbers are 640 and 222, and found the
  off-by-one only because the two moves disagreed: its two `run` tests raised the total by two and
  the selected count by none, while `wc -l` claimed both had risen by one from a base one too high.
  `| grep -c '^fail/'` counts no summary line and needs no correction.
  T-045 is the worked example and moved **eight lines**, in six files, over five different
  constants. Ten `.ft` files that stage1 checks clean and one `fail` test that it refuses took
  `FT_FILES` 876 to 887 on four lines, `CLEAN_FILES` 472 to 482, `PROGRAM_FILES` 492 to 501 --
  nine of the ten hold a `main`, and `std/sort.ft` does not -- `CORPUS_FILES` 638 to 648 in
  `test/highlight_test.py`, which does not walk the `fail` directory, and `CORPUS_FILES` 233 to
  234 in `test/parser_recovery_test.c`, which walks nothing else. The `CMakeLists.txt` comment
  moved with them, 628 to 638 tests, 219 to 220 `fail` tests and 221 to 222 selected. So the
  number of lines is not the number of constants, and neither is the number of files.
  **The four oracles and the fixed point see nothing of `?:`, by construction** (T-126). stage1
  refuses the construct and stage2 types it, so every file that spells `?:` leaves the compared
  set: `diff_ast.sh` counts it in `FORM_FILES` and skips it, `diff_check.sh` and `diff_ir.sh` drop
  it with the files stage1 refuses, and `tools/fixpoint.sh` compares two binaries and not an
  answer. stage2's answer for a `?:` is therefore held by `test/fort/*_test.ft` and by the `run`
  tests of `bootstrap-unsupported.txt`, and by nothing else. T-117 moved one such answer with
  nothing red: `println(n > 0 ? 4294967296 : 1)` was refused before it and prints 4294967296
  after it. A ticket that changes the default type, the operand rules or the arms of a conditional
  writes the assertion itself, because no differential will.
  **The same holds for `diff_ir.sh`'s `opt -passes=verify`, which T-127 added below** (T-127).
  The verify stands inside the loop that skips the files stage1 refuses, so neither module of such
  a file reaches LLVM there. The family is the one this entry names, widened by the other two
  buckets: a `?:`, a `do`-`while`, a float literal or a nested array or span level. Counted on
  2026-09-14: 409 of the 936 `.ft` files are skipped, 84 of them are programs stage2 compiles into
  a module, and all 84 stand under `test/lang`, where `lang-stage2` passes `--verify-ir` and reads
  them. So the gap is empty today and it is not closed: the first such program under `test/fort`,
  `test/tty` or `src/` would have its stage2 module verified by nothing. `CMakeLists.txt` says
  this beside `lang-stage2`, which T-043 gave `--verify-ir` for exactly this reason.
- **`diff_ir.sh` asks a second and absolute question of each module: does LLVM read it** (T-127).
  The differential half says the two emitters agree, and two modules that agree may both be
  wrong. So the script runs `opt -passes=verify` over each module before it compares them, and
  the verified count is an equality, twice `PROGRAM_FILES`, so a run cannot pass while seeing
  less. On 2026-09-14 that reads `527 programs` and `1054 modules`, and it costs 68.5 s to
  84.6 s under `debug`.
  **What it adds beyond `run_tests.py --verify-ir`, which runs the same `opt`.** CMake passes
  that flag to `lang` and `fort-modules` under stage1 and to `lang-stage2` under stage2. 350 of
  the 527 programs stand under `test/lang` and were already verified under both compilers. The
  other 177 are 173 `test/fort` module tests, 2 `test/tty` programs, `src/lsp/main.ft` and
  `src/fort/main.ft`. `fort-modules` is the only run of `test/fort` and it uses stage1, and there
  is no stage2 twin of it, so 176 of those 177 had their stage2 module verified by nothing;
  `src/fort/main.ft` is the exception, which `tools/fixpoint.sh` verifies under both compilers in
  both build modes. What that buys is 176 files, and it is not "every program in the repository":
  the script reads only the files stage1 compiles, and the bullet above gives the 409 it skips and
  the 84 of those that `lang-stage2` alone verifies.
  **Both guards of the verify were written wrong first, and both are now mutation-measured**
  (T-127, the review round). The `verified` equality read `status -eq 0 && verified -ne
  compared * 2`, and every path that skips a verify sets `status` to 1 on the way, so it could
  never fire; it now holds `verified` against `PROGRAM_FILES * 2` whatever `status` is. A stage1
  module the verifier refused used to `continue`, so stage2's module was never emitted and one
  stage1 defect hid every stage2 defect on the same file. The A/B, with an `opt` wrapper that
  refuses two calls in a row: the old script printed two refusals, both `stage1`, on two different
  files and no `verified` line; the new one prints `stage1` and `stage2` for one file and
  `verified 1052 modules, expected 1054`. **Write the equality against the constant, not against a
  counter the same run computes.**
  The mutant is T-128's own defect: `git show c2ad3e4 -- src/` reverse applied to both compilers
  leaves the modules identical and turns `diff-ir` red on
  `test/lang/run/constants/014`, with `'%t8' defined with type 'i32' but expected 'i64'`.
- **A verifier reads only the shapes the corpus spells, so a generated corpus stands beside the
  measured one** (T-127). T-127 lived because no program of its shape existed, not because no
  `opt` ran. `tools/sweep_untyped.sh` is that generated corpus: it builds one program per row of
  `tools/sweep_untyped.txt` and puts each in one of six classes -- REPORTED (the checker refused
  the row and named a reason), FRAME (it refused above the row and never read it), RAN with its
  output, TRAP with its status, CC-FAIL (the checker passed it and clang refused the module) and
  SILENT (it ran and printed nothing). CC-FAIL and SILENT fail the run, and every row carries the
  class it must reach under stage1 and under stage2, so a row that changes class or answer fails
  it as well. The 163 rows cover the four routes of `check_operands`, every context D4.1 names,
  the two positions it says are not contexts, every operator family of D6.2 with an untyped
  operand on each side, the three run-time errors of D6.13 at both widths, both ends of D4.4's
  exact range, the return position and the float clause of D4.5. On 2026-09-14 stage1 reads
  `59 reported, 16 refused at the frame, 77 ran, 11 trapped, 0 refused by clang, 0 silent` and
  stage2 reads `64 reported, 0 refused at the frame, 88 ran, 11 trapped, 0 refused by clang,
  0 silent`.
  **A generated corpus needs three guards a hand-written test gets for free** (T-127, the review
  round). Its first version had none of them. (a) **Hold the compiler to the two statuses of
  D14.1**, 0 and 1, and require an `error:` line with a refusal: reading any non-zero status as a
  diagnostic let a compiler that segfaults on every row pass 72 of 152 rows and print
  `152 reported, 0 refused by clang, 0 silent`. The classes BAD-STATUS and NO-DIAGNOSTIC name the
  two faults and no row may expect either. (b) **Make the list declare its own length**, here
  `!rows 163`, and hold it as an equality, or a deleted row passes in silence. Check the row shape
  too: a row cut down to its two class columns parsed with `body=REPORTED` and matched itself.
  (c) **Give a refusal above the row its own class.** stage1 stops at `f64 d = zf(0.5);`, the line
  the float frame writes above the row, so the 16 rows of section E read FRAME and a reader does
  not count 16 refusals as 16 measurements.
  Nothing in the gate runs it, because a generated corpus is a
  question a ticket asks and not a rule the project keeps: it costs 8.5 s under stage1 and
  21.2 s under stage2, and a ticket that touches constants, the default type or the operand
  rules runs it by hand.
  Its two command lines are in the script's header. It is not a substitute for a `run` or `fail`
  test: a row that becomes a rule of the language moves into `test/lang`, as T-127's two programs
  did.
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
  gitignored except `test/ir/*.ll` and `test/darwin/ir/*.ll`.
  `run_tests.py --verify-ir` runs the same verifier over the
  `-S` output of every language test that compiles.
- The darwin pipeline uses `test/darwin/ir/darwin.ll` as a host sample (T-143, T-152).
  This sample is outside the `test/ir/*.ll` compiler-form corpus.
  `tools/darwin pipeline` verifies, links, and runs it, then checks Mach-O PIE and SIGTRAP.
  `tools/darwin core` runs ten core library programs on a darwin arm64 host (T-144).
  It tests 004, 005, 019, 051, 053, 070, 071, 074, 094 and 095 in `test/lang/run/stdlib`.
  It compiles one C open probe to check the Apple arm64 variable-tail stack slot.
  It uses the darwin compiler and tests its standard root through an executable symlink.
  It checks the compiler's fcntl stack slot and a `--cc` build with the darwin default target.
  Only 005 links `ffi/std_libc_flags.c` to compare 13 constants with C headers.
  The other nine programs and the compiler link from LLVM IR alone.
  The linux gate excludes this host test because a linux host cannot run Mach-O programs.
  The darwin gate builds `build/darwin/stage2/fort` before this host test.
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
  non-blocking descriptor, and a fort program can arrange neither. A
  200000-byte write to the harness's pipe arrives whole, and cutting that loop to a single `write`
  left `run/stdlib/090` green. The `EINTR` branch beside it is uncovered for the same reason, so
  name **both** branches when you record the gap. Assert that every byte arrives, which does catch
  a bypass path that drops bytes, and write in the test what it cannot see instead of claiming the
  loop. **The reason changed on 2026-09-14 and the gap did not** (T-097). Until then the reason
  was that `std/libc.ft` declared no `pipe`, no `mkfifo` and no `socket`, so no short-writing
  descriptor could be opened at all. `std.net` adds `socket`, `bind`, `listen`, `accept` and
  `connect`, and six tests open a loopback connection, so the corpus now has such a descriptor.
  It still cannot make one write short. A blocking socket write returns only when every byte is
  in the send buffer, and the guest's loopback buffer measured **2612608** bytes against the
  **12288** that `run/stdlib/119` sends, a margin of 212. Filling it would need a write larger
  than the buffer, which in a one-process test deadlocks: nothing reads the other end while the
  writer blocks. `O_NONBLOCK` is still out of reach, because `std/libc.ft` declares no `fcntl`
  and `socket` takes no `SOCK_NONBLOCK` here.
- **An errno that a later call could overwrite is not witnessed by a call that succeeds.**
  `net.close_and_fail` in `std/net.ft` reads `errno`, closes the descriptor its failed call
  opened and writes `errno` back, so that a caller of `net.listen` reads the `bind(2)` error and
  not `close(2)`'s. Deleting the write-back leaves `run/stdlib/118` green, which names the exact
  errno of three failures (22, 111, 88), because `close(2)` of a valid descriptor succeeds and
  leaves `errno` alone. The branch that needs the write-back is a `close` interrupted by a
  signal, which no test here can arrange. Keep the write-back, and say in the source that nothing
  holds it (T-097).
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
  array or span level, a `do`-`while` or a `?:`, or a float. A new `.c` or `.h` under `src/` or
  `test/` raises no counter of that list, and one counter fires on all three extensions:
  `test_the_corpus_the_lint_reads_is_the_measured_one` in `test/knowledge_lint_test.py`, which
  holds the number of files `tools/knowledge_lint.py` reads at 370 (T-145). A counter left behind
  fails `diff_check.sh` for every later mutant, which then reads as caught when nothing caught it.
  **A new target code directory needs a `SOURCE_GLOBS` route or an `EXCLUDED` reason** (T-144).
  T-145 adds two darwin test programs under `test/darwin/` and routes them into the grammar corpus.
  Run `tools/darwin net` on the darwin arm64 host to compare C layout and four darwin programs.
  The pin 0 library lacks `libc.errno_slot()` (T-145).
  Stage1 refuses `std/darwin/net.ft` under pin 0.
  The pin 0 network address lacks the darwin length field, so stage1 refuses
  `test/darwin/net_layout.ft`.
  `CLEAN_FILES` and `PROGRAM_FILES` each grow by 1 for `test/darwin/net_errors.ft`.
  **Two branches that each raise one counter by one merge with no conflict and leave it wrong**:
  `CLEAN_FILES` went 393 to 394 twice and the truth was 395, which cost T-107 an hour. After the
  second merge of a branch that adds or removes a source file, run the tool that owns each
  counter and read the number off its failure rather than off the diff.
  **Say how strong each verdict is.** A verdict a mutant measured, a claim probed by compiling a
  program, and a claim read off the source are three things, and an audit that gives them one word
  hides which rows a reader may rely on.
  T-078 ran the same method over the C emitter and used those three words for its rows. Four
  things it adds, for the audit after it.
  **The runner is in the repository and the table is data**: `tools/mutate.py` and
  `tools/mutations/emitter_bootstrap.json`, 76 rows of file, anchor text and replacement, so a
  later audit writes a table and not a program. `tools/mutate.py <table> --only D3.8` re-runs one
  row. `tools/mutate.py <table> --check` builds nothing and reports every anchor that no longer
  matches its file, which is the one way a table of textual anchors rots.
  `test/mutate_test.py` (ctest `mutate_selftest`, 43 tests) holds the parts that fail silently:
  the ctest failure parser, the anchor that must match once, the stale-binary guard, the restore,
  the timeout verdict, the exit status and the round loop.
  **Order the stages by cost and stop at the first red one.** In the emitter that is one `ninja`
  and `ctest -L unit -R '^unit-(gen|driver|selfcheck|runtime_sig|types_abi|mem)'`, a median of
  6 s over a 3 s to 19 s range, against 320 s to 420 s for a round that goes on to `ctest -L unit`
  and `ctest -L lang`. 69 of 72 rounds stopped at the cheap stage.
  **A test that reads a source must not judge a round that rewrites it.** `mutate_selftest`
  asserts the table's anchors against the live `src/bootstrap/gen*.c`. A round mutates one of
  those files, so the suite went red inside the round, at the stage that runs every unit test,
  and the row read `caught` when no test of the compiler had seen the mutation. All three
  survivors reproduced wrongly. Three guards close it: the table's stage excludes the suite by
  name (`ctest -L unit -E mutate_selftest`), the class that reads the sources skips when
  `mutate.mutated_rows` finds a row the sources hold, and `--check` reads the text with the
  applied row put back, so a second row on the same lines is not called stale.
  **Measure a guard on every row, not on one.** The first version of the second and third guards
  asked whether the replacement text occurred exactly once, and the test proved them on 2 of the
  76 rows. They were dead for 3 rows whose replacement repeats text the file already had (D3.14,
  D8.5, D20.4), so `--check` exited 1 on a table that had not rotted -- the failure the third
  guard exists to stop. The test now applies **all 76** rows one at a time, in about 0.4 s, and
  the rule reads "`old` is absent and `new` is present". Any test that asserts something about a
  source file carries this trap; ask what it does while the file is broken on purpose, and ask it
  for every row rather than for a representative one.
  **Couple the row count to the sources on purpose.**
  `test_the_audit_covers_the_72_decisions_the_four_files_cite` goes red when those files gain or
  lose a `Dn.m` citation. A ticket that adds one has three ways out. It adds a row and runs it
  (`python3 tools/mutate.py tools/mutations/emitter_bootstrap.json --only D6.14`). Or it adds a
  row that records why the rule needs no mutation. Or, if the citation sits on a line that
  implements no rule, it says so in the row and moves the citation. The coupling is what stops an
  audit going stale. T-046 froze `src/bootstrap` on 2026-09-14, so from that date the count moves
  only when a bug fix adds or removes a `Dn.m` citation in one of the four files.
  **Mutate through `tools/mutate.py`, and never leave a mutated source across a tool call.** The
  runner restores in a `finally`, reads the copy it saved rather than `git checkout`, and ends a
  run by rebuilding and comparing the md5 with the baseline, except after a timeout row, where it
  restores the files, builds nothing and says it measured nothing. So a restore that did not
  compile is visible at once. A mutation by hand has none of that: on 2026-09-14 two
  implementors stopped at once on a spend limit, and T-127's worktree then held a mutation of
  `src/bootstrap/check.c` and `src/fort/check.ft` that reverted T-128's fix. Nothing in the
  repository said the tree was mutated. Three rules follow.
  Run `tools/mutate.py <table> --check` before the round, not after it rots.
  **One table exists today**, and it covers 4 files:
  `ls tools/mutations/*.json | wc -l` prints 1, and
  `grep -o 'src/bootstrap/[a-z_]*\.c' tools/mutations/emitter_bootstrap.json | sort -u` prints
  the four files of the C emitter, which is the `"sources"` list the runner saves. T-126 mutated
  `src/fort/check.ft` by hand and T-127 mutated both compilers by hand, because no table covers
  either.
  Where no table covers the file, apply and restore in **one** shell command, from a pristine copy
  that same command made.
  Prove the restore with `md5 -q <file>` against the value it read before the mutation, and with
  `git status --short`. **Never restore with `git checkout <file>`**: it throws away uncommitted
  work in the same file, which is `tools/mutate.py`'s own stated reason for saving a copy. That is
  the one reason. Restore with `git show main:<path> > tmp && mv tmp <path>`, which touches no
  other work; the coordinator restored T-127's two files that way and `md5 -q` then read
  `bbde7b5ca321f3e1834837d6474ae5e0` and `9c18ea9291e8c35bb91cfcda822a6f8f`, which are main's
  (T-134).
  `--check` needs no step of its own in the gate: `mutate_selftest` already runs `check_table`
  over the shipped table and the live sources in
  `test_check_reports_no_stale_anchor_against_the_sources`, and asserts `76 rows, 0 stale`
  (T-134).

- **An oracle is only an oracle where it derives its answer differently, so say
  for each half of one whether it is independent or shared.** T-064 swept every offset of a
  document against a hand-written oracle and the sweep stayed green over a bug in the module's
  line rule: `oracle_character` in `test/fort/lsp_text_test.ft` derived the end of a line with the
  same expression as `line_end` in `src/lsp/text.ft`, so it tested that expression against itself.
  The halves of it now read: `oracle_units` decodes RFC 3629 a second time (independent),
  `oracle_span` splits the lines forward where the module walks back from the line feed
  (independent), and the rule that an offset inside a terminator counts to the end of the line it
  ends is **shared**, because it is the ticket's own ruling and both sides state it.
  A shared rule is not an untested rule, and this is the second half of the lesson: a **round-trip
  sweep constrains what the oracle sweep cannot judge.**
  `every_offset_comes_back_from_its_position` asserts `back <= at` and that the position of the
  offset it answered is the position it started from, and each alternative to the ruling breaks
  one of those two -- rounding an offset inside a terminator forward to the next line moves `back`
  past `at`, and counting the terminator as a character of its line breaks the reconvergence. So
  an oracle sweep and a round-trip sweep answer different questions, and a rule the oracle shares
  needs the second one (T-064).

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
  `test/lang/programs`, `std`, `src/fort`, `src/lsp`, `test/fort` (its `support/` included),
  `test/fort_lint` and `test/tty` -- and `CORPUS_FILES` is the exact number of files in it, so a
  ticket that adds or removes a `.ft` under any of them reads the new number off the failure and
  writes it there, as it does for `CORPUS_FILES` in `test/parser_recovery_test.c` and `FT_FILES` in
  `tools/diff_tokens.sh`. The other `.ft` of the repository are listed in `EXCLUDED_DIRS`, each
  because it is meant to hold a lexical error (`test/lang/fail`, `test/highlight/scopes.ft`,
  `editors/vscode/test/fixtures/lexical.ft`), and a test asserts that partition, so a new
  directory of fort is a red test rather than a corpus nobody tokenizes -- which is what
  `test/fort` and `test/lang/programs` both were until T-079 measured it.
  **One test checks one directory of `CORPUS_DIRS`, and the corpus is tokenized once** (T-104).
  `test_the_corpus_is_the_size_it_says_it_is` walked the corpus to count it and then tokenized
  every file a second time, which took one run of the module to 1396 calls of `Engine.tokenize`
  over 7575844 bytes where the corpus is 685 files and 3844648 bytes. That second walk also gave
  cover to `src/lsp` and `test/tty`, which stood in `CORPUS_DIRS` with no test of their own. Each
  directory now has one test, `CORPUS_MINIMUMS` gives each its floor, and
  `test_every_corpus_directory_is_checked_by_a_test` reads the source of the class and holds the
  calls to `check_directory` against `CORPUS_DIRS`. A ticket that adds a directory adds a test
  with it, or that guard goes red.
  **The engine finds the leftmost token of a line in one search.** It called `re.search` once for
  each of the 53 top-level rules at each position, and a search scans to the end of the line, so a
  line cost `positions x rules x length` and not its bytes. `Scanner` writes the rules as one
  alternation, each inside a group of its own, and `SlowScanner` keeps the old search as the
  oracle of `ScannerTest`. The two changes take the module from 31.55 s to 4.87 s, five runs each
  under `debug`, and a run makes 773 calls of `Engine.tokenize` where it made 1396.
  **`SlowEngine` inherits `tokenize` from `Engine`**, so an instrument that wraps the method on
  both classes wraps the second one over the first and counts the 29 calls of the oracle sweep
  twice: T-075 read 802 calls over 4204178 bytes on `main` that way, which is 773 + 29 and
  4089469 + 114709. Wrap `Engine.tokenize` alone, and assert `"tokenize" not in
  SlowEngine.__dict__` beside it so the next override is seen. Corrected, the instrument
  reproduces T-104's figure exactly: 773 calls over 4089469 bytes on `main`, and 774 over 4096421
  on a branch that adds one 4388-byte test file under `CORPUS_DIRS`. A ticket that adds such a
  file raises both numbers in the docstring of `CorpusTest`, and measures them rather than
  deriving them.
  **Hold a
  rewrite of that engine to the dump and not to the assertions**: a
  scanner that reports other scopes passes the suite and silently changes what the grammar is held
  to. The dump is five lines of Python and prints 945748 lines for the 685 files of 2026-09-14;
  take it before the change and after it, and `diff` the two:

      cd test && python3 -c 'import highlight_test as h
      e = h.Engine(h.load_grammar())
      for p in sorted({p for d in h.CORPUS_DIRS for p in d.rglob("*.ft")}):
       t = p.read_text(encoding="utf-8")
       for k in e.tokenize(t):
        print(p.relative_to(h.ROOT), k.line, k.start, k.end, repr(k.text), k.scopes)'

  The path is relative to the top of the worktree, so two worktrees that hold the same scopes give
  the same md5. An absolute path gives two md5s for one answer, and the second reader then builds
  a dump of their own (T-104, its review).
  **Set `PYTHONDONTWRITEBYTECODE=1` for a hand run of a Python suite you are mutating.** T-104 ran
  two mutants of one file within a few seconds and read the same three failures for both, because
  the second run loaded the `__pycache__` of the first. CMake sets the variable for every Python
  ctest, so the gate never meets it; a hand run does.
- The VS Code extension is plain JavaScript on the VS Code API, with no npm dependency and no build
  step. Its logic lives in `editors/vscode/lib/check.js`, which never `require('vscode')`, so Node's
  built-in runner tests it: `tools/vm run 'cd editors/vscode && node --test'` (ctest
  `extension_selftest`, label `unit`, run from `editors/vscode`; `node --test` with no argument
  discovers `test/*.test.js` itself, and naming the directory fails on newer Node). `extension.js`
  is the only file that may touch the API, so keep it thin and move anything with a case analysis
  into `lib/`; it is driven through `test/fake_vscode.js`, which answers its `require('vscode')` and
  its `require('child_process')` by patching `Module._load` before loading a fresh copy of it, so a
  save, a failed run, a close and two checks racing are all tested with no editor, no VM and no
  compiler. **A check answers about a closure, not about one file**, so an ordering guard keyed on
  the file that was checked is not enough: the first version dropped a superseded run of the same
  file and still let an older run of `main.ft` repaint an error in `mathx.ft` that a newer check of
  `mathx.ft` had just cleared. The generation is therefore recorded per *published* file as well,
  and a test that means to see that has both files in **one** closure -- two disjoint closures pass
  either way. A test that means to see the ordering of a **close** needs a closure wider than one
  departed file as well, because a queue of one drains before any later event can reach it: T-106's
  first version had a duplicate re-check and a re-check that erased a live error, and both were
  invisible to a two-file closure (T-106). What that cannot check is that VS Code calls the
  extension the way its API is documented to, which is what the manual smoke test in
  `editors/README.md` is for, and a change to `extension.js` is run through it by hand -- by the
  user, since VS Code runs on their machine and an agent cannot reach it. Its fixtures are real
  compiler output: regenerate them with `fort --check --json` over `editors/vscode/test/fixtures/`
  rather than by hand, and a helper that is not a suite, such as `test/fake_vscode.js`, defines no
  test of its own, since Node 18 loads every file under `test/`.
- **What checks `.ft` source, and what does not** (T-076). Three things do. `tools/fort_lint.py`
  (ctest `fort_lint`, target `fort-lint`) holds `std/*.ft` and `src/fort/*.ft` to the identifier
  conventions of D1.4 and to 100 columns; it reads `fort --index` (D20.3) rather than tokenizing
  fort a second time, so the kinds and types it reasons about are the checker's own answers, and a
  second tokenizer cannot drift from the language. `fort_lint_darwin` uses a temporary darwin root
  to lint `std/darwin/*.ft` with stage2 (T-144). The linux root conflicts with darwin declarations.
  `test/highlight_test.py` tokenizes `std/`,
  `src/fort/` and `test/lang/run` against the TextMate grammar, which is the only check that grammar
  has. `tools/lines.py` counts `std/*.ft` and `std/darwin/*.ft` as source for the 3:1 ratio (T-144).
  It counts `test/darwin/*.c` as test. The compiler and runtime are source too (D14.6).
  What still does not: **there is no formatter** -- indentation,
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
- **`tools/lines.py` does not see a test written in shell or in Python either** (T-131). Its
  `TEST_GLOBS` are `test/*.c`, `test/*.h`, `test/**/*.ft`, `test/lang/ffi/*.c` and `test/darwin/*.c`.
  Its `SOURCE_GLOBS` include the two compilers, `std/*.ft` and `std/darwin/*.ft` (T-144).
  So a ticket whose deliverable is the build or a tool gets no useful figure:
  T-131 changed `CMakeLists.txt`, six scripts under `tools/` and
  the language harness, and the tool read `+2 source lines, +0 test lines, ratio 0.00`, the 2
  being its one edit to `std/rt.ft`. Measure such a branch by hand, as the `editors/` bullet
  above does, with `git diff --numstat main...HEAD` and the tool's convention: net lines, raw
  `wc -l`, comments and blank lines on both sides. T-131 measured 469 net lines of build, tool and
  harness against 258 net lines of test, which is 0.55, and its log says why it cannot reach 3:1:
  the part of it that a line-counted test could hold is `tools/pin.sh`, which
  `test/pin_test.sh` holds with 21 checks that break every rule of it on purpose, and the rest is
  a CMake graph that only a build can exercise. The ctest `bootstrap`, the ctest `pin-verify` and
  a cold `rm -rf build/<preset> && tools/vm workflow debug` are what hold that part.
- **The corpus is smaller than the sizing table of `spec/toolchain.md` 7.6, and T-046 measured by
  how much.** That table sizes the corpus at 565 `run` and 370 `fail` files, 935 together. On
  47f98c2 the corpus holds 643 tests: 400 under `run`, 222 under `fail` and 21 under `programs`,
  which the table counts on its `run` side. `python3 test/lang/run_tests.py --list | tail -1`
  gives the total and
  `for d in test/lang/{run,fail}/* test/lang/programs; do echo "$d $(ls "$d" | wc -l)"; done`
  splits it by area, the three sums of its rows being the 400, the 222 and the 21. Seventeen of
  the table's twenty rows are under their row and three are over: `stdlib` holds 115 `run` files
  against 45, `ownership` 24 `run` and 38 `fail` against 15 and 20, and `programs` 21 against
  20. The largest gaps are `strings` (5 of 30 `run`), `mutability` (5 of 20 and 10 of 40),
  `lexical` (5 of 30 and 15 of 30) and `control` (14 of 45). Four directories have no row in the
  table at all and hold 89 tests: `errors` (32), `modes` (30), `switch` (16) and `declarations`
  (11). The corpus figure of `tools/lines.py` moves with the same shortfall: it read 2.17 on
  47f98c2 (52163 source lines against 113082 test lines) against the 3.0 D14.6 then set. **The
  user took that decision on 2026-09-14: no backfill, and the target drops to above 2.0**, which
  the corpus meets (2.19 on 865a15c, `python3 tools/lines.py`). The per-branch criterion stays at
  `--min 3.0`, because a branch minimum above the corpus target is what holds the corpus above its
  floor; a ticket still answers for its own diff.
- The ratio is a prompt, not a verdict: what a review asks is which rules of the decisions a ticket
  cites have no test at all, unit or language, and the answer decides the ticket. T-014 measured
  0.76 and merged, because the number could not see that it took 57 entries out of xfail.txt -- a
  body of language tests that already existed and only then began to exercise the code, adding not a
  line to the diff -- while the coverage question found three rules that were untested and also
  broken (a noreturn function type that never matched, an unchecked shift count that made the two
  build modes disagree, and an index expression that handed out a pointer into a dead temporary).
  Ask a reviewer for the list of untested rules whenever a ticket misses the ratio, and merge or
  refuse on that list.

# fort testing: how a test is written and how a change is judged

This document holds every fact about the test corpora and the merge gate: what each suite is, how
to write one, what each oracle can see, and what it cannot see. `spec/toolchain.md` 7 specifies
the language test format and wins over this file; `spec/decisions.md` and `spec/grammar.md` win
over both (D1.2).

T-098 created the headings below. T-099 moved into them the test half of the `## Build and test`
section of `AGENTS.md`, and the test bullets of its `## Technical Standards` section with it, one
bullet at a time and without a rewrite.

## 1. The merge gate

- **The development targets** (2026-09-26; `cmake/dev_targets.cmake`). The build has two
  components: `fort` (the compiler, the standard library, the language server and the editor
  extension; the language server imports the compiler's modules, so one component holds both)
  and `bootstrap0` (the C bootstrap compiler). Each has six steps: `format` and `format-check`
  (clang-format over its C sources), `lint` (clang-tidy for `bootstrap0`; `tools/fort_lint.py`
  for `fort`), `unit` and `integration` (its ctests with that label), and `check`, which runs
  `format-check`, `lint`, `unit` and `integration` and stops at the first failure. The fort
  targets carry the step's name (`check`); `bootstrap0/CMakeLists.txt` defines `bootstrap0-<step>`,
  whose ctest runs over `build/<Host>/<preset>/bootstrap` and so sees bootstrap0's tests alone; and
  `<step>-all` runs the step for both components. Each test carries one component label,
  `bootstrap0`, `fort` or `lsp`, and a new test must carry one too. The targets select by
  component and step (`ctest -L '^(fort|lsp)$' -L '^unit$'`; repeated `-L` options select the
  tests that match all of them), and so does CI (T-261). The label `lsp` holds the language
  server's tests, `lsp-modules` (`test/lsp`) and `lsp-binary`, so a CI job runs them apart from
  the compiler's (T-264). Unit:
  `test/fort` (`fort-modules`), `test/lsp` (`lsp-modules`), `test/fir` (`fir`),
  `highlight_selftest`, `extension_selftest`, `panic_coverage` and `panic_coverage_selftest`;
  the 71 bootstrap0 C suites. Integration: the corpus (`lang`, `lang-json`), `fixpoint`, `tty`,
  `stack_depth` and `lsp-binary`; bootstrap0 has none, since building bootstrap-1 proves the C
  compiler. `stack_depth` gives each compiler of the build a 1 MB stack and a type of 254
  nested groups (D2.11). Before the checkers kept their suffixes on a stack of their own, that
  type needed 1792 KB in a darwin debug build. The unit
  tests of the tools (`lang_selftest`, `fort_lint_selftest`, `mutate_selftest`) went on 2026-09-26.
  On linux each target runs as `tools/vm <target>`.
- **GitHub PR CI supplies the merge gate** (2026-10-01; T-306).
  `.github/workflows/ci.yml` runs on PRs against the repository.
  The `bootstrap0` and `fort` jobs call component workflows. `ci-status` requires both to pass.
  Read those workflows for the current OS, sanitizer, format, lint, test, and fixpoint matrices.
  Record the PR head SHA and the tested revision. PR CI can test a generated merge revision.
  Record run URLs, job conclusions, and relevant test counts in the ticket.
  A missing, pending, cancelled, or failed required result does not pass the gate.
  Local tests provide development evidence. They do not replace PR CI.
  Count results with exit status 0 in each required group before a commit or handoff.
  A result row records an attempt. Its presence does not establish a pass.
  `AGENTS.md` defines the review, branch-update, and GitHub merge procedure.
- The local `gate` target remains available for diagnosis. It is not a merge requirement.
  It runs `check-all`, then the configure, build, and test steps for `asan-debug` and `ubsan-debug`.
  Run `tools/vm gate` in the VM, or build the `gate` target of the `debug` preset on Darwin.
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
  **One worktree has one `build/<Host>/<preset>`, so two gates in it collide** and the collision
  reads as a test failure rather than as contention: two ninja processes drive the same directory,
  one rewrites an object the other is linking, and the tail of the log names whichever test lost.
  The worktree belongs to whoever holds the ticket until they hand it back, so a coordinator
  re-gates only after the implementor has reported, never beside it (T-087, where a coordinator gate
  and an implementor gate ran together and the exit 1 was the collision). Two other readings cost
  the same hour there and are worth knowing as shapes: a `tail` of a log file the run has not
  finished writing reports the previous run's verdict, and piping the gate into `head` closes the
  pipe early, which kills it with SIGPIPE and yields a status that has nothing to do with the tests.
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

- The C-started build registers 69 C unit suites on linux and on darwin. Each suite has the labels
  `unit` and `bootstrap0`. These suites test the C implementation. An external-stage1 build
  registers none of these suites, because it does not add `bootstrap0/CMakeLists.txt`.
- New C unit suites use inline source or local sandbox fixtures. Only a bootstrap-library contract
  can read the bootstrap-1 standard library. `runtime_sig_test` and `check_conv_test` are the two
  contract suites. They also have the label `bootstrap-contract`.
- Unit tests: `bootstrap0/test/<component>_test.c` with `bootstrap0/test/common/test.h`; the suite
  name is the file stem and `bootstrap0/test/` already holds one per component, `runtime_test.c`
  being the C runtime's and not the compiler's, so check the name is free before writing the file (a
  shell redirection overwrites a suite silently and the gate then reports only its absence); every
  `bootstrap0/test/*_test.c` is globbed into an executable
  `build/<Host>/<preset>/test/<component>_test` linked against `fort_core`, and a ctest of the same
  name. A `TEST` body is one macro argument: a comma outside parentheses (a brace initializer, for
  example) splits it. `#val` in an assertion message is the argument after macro expansion, so
  compare through a variable when the expected text matters. Suites are ordinary C11: no
  `__VA_OPT__`, and `-Wtype-limits` (gcc) rejects assertions that are always true, such as
  `TEST_ASSERT_GE_SIZE(n, 0)`. The `TEST_ASSERT_*_INT64`/`_SIZE` operands are printed with
  `PRId64`/`%zu`, so cast plain literals and `long` values (`(int64_t)0`) or `-Wformat` fails the
  build. A suite whose literals are the test data (sample values, expected texts) wraps them in
  `// NOLINTBEGIN(readability-magic-numbers)` with a comment saying so rather than naming each.
  The sanitizer presets run the unit tests with `allocator_may_return_null=1` (ctest sets the
  environment, `cmake/sanitizers.cmake`) because the runtime's out-of-memory path is tested with
  an impossible allocation; run a suite by hand under those presets with the same variable.
- **The C unit suites run on linux and on darwin.** The first darwin run failed 6 compilations
  and 20 test cases. Four rules keep a suite portable:
  - Compare a `uint64_t` with `TEST_ASSERT_EQ_UINT64`, not `TEST_ASSERT_EQ_SIZE`. `size_t` is
    `unsigned long` on darwin and `uint64_t` is `unsigned long long`, so `%zu` fails `-Wformat`.
  - Build an expected command line from `FORT_DEFAULT_CC`, `FORT_DEFAULT_TARGET` and
    `cross_target()` in `bootstrap0/test/common/driver_helpers.h`. Do not write `clang` or
    `x86_64-linux-gnu` in the text.
  - Capture driver output with `capture_stream()`, not `tmpfile()`. Darwin's `tmpfile()` reads
    `TMPDIR`, and 2 driver tests set `TMPDIR` to an empty or a missing directory.
  - Find the running binary with `_NSGetExecutablePath` on darwin; `/proc/self/exe` is linux only.
- A test that must observe a program the compiler spawns uses a fake one:
  `bootstrap0/test/common/fake_cc.sh` is the `--cc` of the driver suites, it writes its own path and
  every argument, one per line, into `$FORT_FAKE_CC_LOG` and exits with `$FORT_FAKE_CC_STATUS`, so
  the whole clang command line is one string comparison and the failure path is a variable away.
  CMake passes its path as `FORT_FAKE_CC` to the suites that name it, and to no others (a
  `target_compile_definitions` over a list after the glob loop: `driver_test` asserts the command
  line, `driver_check_test` that `--check` spawns nothing and `driver_conformance_test` that the
  runs which stop before `--cc` spawn nothing), since a unit test has no working directory it can
  rely on. A new suite that needs it is added to that list, not left to inherit it.
- The code that two or more suites share lives in `bootstrap0/test/common/`: `test.h`, `common.h`,
  `fork.h`, the `<component>_helpers.h` files and `fake_cc.sh`. The suites are the `*_test.c` files
  of `bootstrap0/test/` and nothing else. A suite includes a shared header as `"common/<name>.h"`,
  which resolves from the directory of the suite; no include path names `common/`. Each suite links
  `fort_core` directly. The shared code is headers only today. Its one compiled member,
  `test/ast_dump.c`, became what `fort --ast` writes and moved to `bootstrap0/src/ast_dump.c`
  (T-033). A new compiled helper goes into `common/` and needs an OBJECT library in
  `bootstrap0/CMakeLists.txt`, so that `-Werror` and clang-tidy read it once. A suite links
  `fort_core`, so a helper may not take the name of a compiler function (`type_error` is types.h's
  error-type constructor, not a test helper).
- clang-tidy's `readability-function-size` caps `main` at about 60 `TEST_RUN`s (statement
  threshold 800; each `TEST_RUN` expands to about 13 statements, so 89 measured 1162): split a
  larger suite into two files with a shared `bootstrap0/test/common/<component>_helpers.h` whose
  helpers are `static inline` so that a suite using only some of them still builds under `-Werror`.
  `bootstrap0/test/common/fork.h` runs a function in a forked child and captures its stderr and exit
  status, for paths that end the process (`fatal_oom`); under asan the child runs LeakSanitizer at
  exit, so the forked function must not drop a block it allocated (blocks its still-live frames
  point to are reachable and fine).
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
  makes tests pass. The product compiler runs the complete corpus with the current standard
  library, on both targets: a test whose outcome differs between them carries the plain
  directive and its `-<os>` form (`//! signal: ILL` and `//! signal-macos: TRAP` in
  `run/ffi/009`, `//! stdout-macos:` in `run/modules/conditional_imports`, D14.5), and the
  harness selects the form of its `--target`. Until 2026-09-25 the darwin gate excluded 4
  fixtures instead. A test that prints an errno number or writes a field whose type differs
  (`sockaddr_in.family`) compares against the `std.libc` constant or writes under
  `$if ($cfg(target_os) == ...)` instead, so its expected output is the same on both targets.
  The harness has no bootstrap expectation list. `run_tests.py --lint`
  validates directives without a compiler and runs before every test run;
  `run_tests.py --check-json` is a mode of its own (ctest `lang-json`, label
  `integration`) that holds the document of `fort --check --json` against the text form on every
  fail test and ignores `xfail.txt`, since it judges the
  two forms of one run rather than the test. It also selects a test with an `index.json` beside
  it, runs that one with `--index` and holds its `"symbols"` against the file byte for byte
  (D20.3): the golden is one record per line as `render_index` spells it, it and
  `expected.stderr` (the next bullet) are the two non-`.ft` files a directory test may hold, and
  `--lint` checks its shape without a compiler, so a golden
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
  `ruff format --line-length 100` is the reference); the harness has no unit tests of its own
  since 2026-09-26.
  On voyager.local, `/bin/false` is absent. This made three inherited fixture tests error (T-143).
  Use `shutil.which("false")` for a fixture that runs on Mac and Linux.
  The host and slot-2 VM suites each pass 185 of 185 tests with this fixture.
- **Every `fail` test has a golden stderr** (T-191, D14.5). The golden rule: `NNN_name.stderr`
  stands beside `NNN_name.ft`, and `expected.stderr` stands in the directory of a directory test. It
  holds the compiler's whole stderr for the test: each header line and the rendered lines under it
  (`spec/toolchain.md` 4). `judge_fail` compares it byte for byte after the other checks, a missing
  golden is a `FAIL`, and the reason names the first line that differs. The normalization rule: the
  harness replaces the `--std-dir` prefix of a path with `<std>` and the absolute path of the corpus
  root with `<root>`, and changes nothing else. A test path is already relative to `test/lang`, but
  `fail/modules/013_same_file` quotes a real path in its note (`both name <root>/fail/...`), and
  before the second normalization its golden blessed on the Mac failed in the VM, where the root is
  `/vagrant/.worktrees/...`. A golden that quotes a line of the standard library therefore changes
  when that line moves: `fail/ffi/004` and `fail/ffi/006` quote `<std>/libc.ft`, whose lines are the
  same in `std/linux` and `std/darwin` today. Both hosts gate every merge, so an edit to one copy of
  `libc.ft` goes red on its own host; the coordinator accepted that coupling on T-191. The bless
  rule: `run_tests.py --bless [filter...]` writes the goldens of the selected `fail` tests from the
  compiler's output and then judges them; a compiler step that is an `ERROR` writes nothing. Bless
  only the tests a change touches, and read `git diff -- '*.stderr'` before the commit, because the
  diff of the goldens is the review of the new text. `--lint` rejects a golden with no `.ft` beside
  it and a golden beside a `run` test. **Select the fail corpus as `fail/`, not `fail`**: a filter
  is a substring of the path, and on 2026-09-27 `run_tests.py --list fail` printed 275 tests, 23 of
  them run tests such as `run/errors/008_assert_failure.ft`, against 252 for `--list fail/`. The
  harness matches its diagnostic patterns only against lines that start with a byte other than a
  space, so a rendered source line that spells `x.ft:1:1: error: y` is never read as a diagnostic. A
  path can start with a space (`fort --check " sp.ft"` prints such a header line), so a test file
  name must not.
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
  `diag_lines()` in `bootstrap0/test/*_test.c` or `check_env.errors(&e)` in `test/fort/*_test.ft` --
  and uses the `fail` test for the text and the position.
  **A diagnostic that is removed is as invisible as one that is added, and neither directive can
  see it** (T-129, the first mutation measurement of a removal). `judge_fail` asks whether an
  annotated line carries a message, so a line that loses one of two keeps its annotation; and
  `_stderr_problems` (`run_tests.py:670`) reports only `stderr lacks '<s>'`, so `//! stderr:`
  asserts presence and has no negative or counting form. Measured on T-129: the guard that
  silences `the expression expects <error>, not a constant` took `fail/constants/012` from 27
  diagnostics on 13 lines to 14 and the whole corpus from 14 such diagnostics to 0, and the
  corpus read `665 tests: 665 passed` with the guard reverted in both compilers. Three of the
  five tests of `bootstrap0/test/check_poison_test.c` went red under that mutant and
  `test/fort/check_test.ft` aborted. A removal therefore needs the count and nothing else will do.
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
  first `fail` test to use the directive.
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
  into `build/<Host>/<preset>/std`: running `run_tests.py` by hand against a source
  that has not been copied reports `module 'std.x' not found`. `test/lang/run/stdlib` is where a
  library module is tested.

## 4. fort module tests

- `ownership_api.valid_limits` requires positive bounds.
  Zero remaining W uses `bounds.w=1` and `used.w=1`.
  Assert `used=1`, `bound=1`, and `attempted=1`.
  `ownership_globals_history_limits_test.ft` checks this boundary and preserves caller state.
- **An exported compiler helper can have test clients outside the focused selection.**
  A range-capture CI run found an old prescan call in `fir_lower_local_panic_test.ft`.
  After changing an exported `fir_lower` helper, search its callers and run the full module
  corpus or its closure lint before handoff. A focused `fir_loan_*` run did not compile that client.
- **A `test/fort` suite that checks two sources must reopen its environment between them.** A
  module set answers a path it has already loaded from the tree that load left, so a second
  `check_env.check_src` over one environment silently re-checks the first source and its
  assertions then pass or fail for the wrong reason; the first sink still holds the first check's
  diagnostics as well. `check_env.reopen` is `bootstrap0/test/common/check_helpers.h`'s `begin()`
  and goes between the assertions about one source and the next check. The C helpers reset per
  check, so a translated suite that drops the reset is the failure mode to look for.
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
  T-117's fix round, a second hand-run left 51 sandboxes with an empty `git status --short`.
  Compile with `-o` into `build/` and run from there. Read `ls -d test/fort/sandbox*` rather than
  `git status` when a source count is wrong. T-247 met it again: hand runs from `test/fort` left
  117 sandboxes, and `highlight_selftest` counted 1000 corpus files against 790.
  **The working directory is any directory, and the ignore line names one** (T-127). A hand run
  from the **top of the worktree** writes `./sandbox<n>/`, which `test/fort/sandbox*/` does not
  match. One run of `check_test.bin` from there left 196 `sandbox<n>/main.ft` files in the root,
  and the next source count included all 196 files.
  `git status --short` did show all 196, which is the one advantage of the root over
  `test/fort/`, and it is why this line is a rule about the working directory and not a second
  `.gitignore` entry. Remove them with `rm -rf sandbox[0-9]*`. Then run the
  source counter that reported the mismatch.
- Three corpora run through `run_tests.py`, which takes their root as `--root`.
  CTest `lang` runs the product language corpus with `xfail.txt`. CTest
  `fort-modules` runs `test/fort/<x>_test.ft` against the compiler modules, and CTest
  `lsp-modules` runs `test/lsp/<x>_test.ft` against the language server's modules.
  `lang` is an integration test and the other two are unit tests. A `test/fort` test is an ordinary
  run test in the D14.5 directives whose header carries `//! flags: -I ../../src/fort` (the
  compiler's working
  directory is the corpus root, so the path has two `..`, not three). **The tests of `src/lsp`
  stand in `test/lsp`** (T-264; `test/fort` until then), in the same format, and a test of a
  server module carries `-I ../../src` beside that root, since a server module is `lsp.<name>`
  under the root `src` (T-063). A corpus of its own lets the label `lsp` run the server's tests
  apart from the compiler's. The files keep the `lsp_` prefix they took in `test/fort`
  (`lsp_json_test.ft`, `json_test.ft` being the compiler writer's). A file name is
  `<module>_test.ft` or `<module>_<case>_panic_test.ft` for a test whose program must end in a
  panic, since a panic kills the program and each one needs a file; any other `.ft` at that root is
  `bad test name`, because a typo there would otherwise run nowhere and say nothing.
  **Code several of those tests share lives in `test/fort/support/*.ft`**, which they reach with a
  second include root (`//! flags: -I ../../src/fort -I support`; `-I ../fort/support` from
  `test/lsp`, whose tests share `support/heap.ft` with the compiler's): a `test/fort` test is a
  program rather than a translation unit, so the `#include`d helper a C suite would use
  (`bootstrap0/test/common/types_helpers.h`) has to be an imported module (`support/types_env.ft`,
  T-032). The directory is invisible to the harness: `discover` walks `run`, `fail`, `programs` and
  the `*_test.ft` of the root and nothing else, so a test misfiled there would run nowhere and say
  nothing. `_report_misplaced_tests` closes that (T-079): a `*_test.ft` anywhere below the root
  outside those three directories is `test outside the root of the corpus`, which is a lint
  problem and not a test, since what belongs under `support/` is shared code and nothing else.
  `agents/lines.py` counts `test/fort/**/*.ft` and `test/lsp/*.ft` as test lines and
  `src/fort/*.ft` and `src/lsp/*.ft` as source lines, `test/highlight_test.py` tokenizes them,
  and `tools/fort_lint.py` lints them with the search roots its `SOURCE_SETS` table pairs with
  the glob (`-I src -I src/fort -I test/fort/support`); a file named on its command line takes
  the roots of its own `-I` options.
  **The lint's roots are a superset of the directives' and not a copy of them**, which is the
  property to keep. `grep -h '//! flags:' test/fort/*.ft | sort | uniq -c` counted three shapes
  among the 161 tests of T-063, before T-264 moved the server's tests to `test/lsp`: 87 carry
  `-I ../../src/fort -I support`, 53 carry `-I ../../src/fort` alone and 21 carry
  `-I ../../src/fort -I support -I ../../src`. The lint gives all 161 the same three roots, in
  another order. The two agree about which file answers an import only while no
  root shadows another, and today nothing does, because `src/` holds no `.ft` of its own: a future
  `src/<name>.ft` would be the module `<name>` under the lint's first root and something else
  under a test's, and the lint would then judge a file the harness never compiles (T-063).
  Without those roots `fort --index` reports
  `module 'containers' not found` and every name in the file goes unjudged, which it did until
  T-079. **`test/fort` is not a leak oracle**: the gate's `asan-debug` and `ubsan-debug`
  presets instrument the native compiler, not the x86-64 program the harness builds and runs under
  qemu, so a `del` a module forgets leaks silently through all three presets. A module that
  promises its allocations die with the value that owns them (D20.5) needs a witness of its own --
  in-band accounting the module already keeps (`containers.pool_used`), or the allocator itself:
  identical rounds are handed the same addresses again when a round frees what it took and fresh
  ones when it does not, so an address that repeats over eight rounds is the release
  (`the_blocks_a_node_owns_are_released_with_it` in `test/fort/types_table_test.ft`, T-032).
  Verify such a witness by deleting the `del` it covers and watching it go red; two of them in
  `types.ft` had no witness at all until that was measured.
  Clang can replace an emitted LLVM target triple during linking.
  Cross-target receipts record the host, explicit link target, executable architecture, and runner.
  An LLVM triple alone proves no executable architecture.
  Linux x86-64 receipts use explicit x86-64 linking and QEMU execution in an aarch64 guest.
  Keep native aarch64 execution evidence separate from x86-64 execution evidence.
  **An allocator probe needs two views, because each is blind to what the other sees** (T-034), and
  both of them are blind to a leaked block above 128 KB (section 4 of this file, T-094).
  The address a round is handed catches a leak the allocator serves out of its own free chunks --
  a 64-byte vector -- and misses a large one, because a small probe block still comes back at the
  same address while the heap has grown: with `defer analysis_free` deleted in the driver the
  address probe read 0 while the program break had climbed by megabytes, so the release it
  claimed to witness was unwitnessed. The program break (`sbrk(0)`, the Linux system view of
  `test/fort/support/heap.ft`, which gives macOS a view of its own)
  catches exactly the other half: it did not move at all when `del(set->modules.items)` was
  deleted. So watch both, and more than one address when a round allocates several blocks -- a
  dropped `del(set->order.items)` left the address of the module vector exactly where it was, so
  `modules_closure_test.ft` watches three addresses and the break, and every one of the six
  releases was verified against the view that moves. Sample the early round against the **last
  sixteen** rounds and take the smallest distance: the allocator's small-block position runs
  through a cycle once the program's own path is long enough to change a bin -- which the
  harness's `mkdtemp` directory is, while a hand run from `/tmp/prog` is not, so a probe reads 0
  by hand and 12208 under the harness -- and one late round in every cycle is in step with the
  early one whatever the period, while a leak moves all of them. Two late samples were the rule
  until T-038 measured a period of **eight** over the emitter's round and read 26512 with nothing
  leaking; the window has to cover the cycle, and sixteen covers every period seen so far.
  A scratch block that each mutation of the round allocates and frees can stretch the cycle past
  sixteen: when the sorted G and H counts took their index arrays from the heap, the Linux break
  stayed still over 400 rounds, and 8 of the 18 addresses of `ownership_state_cleanup_test.ft`
  never came back within sixteen rounds. Keep a small scratch array in fixed storage in code that
  an address probe measures; the counts now sort up to 64 rows in a fixed array.
  On macOS the address of a buffer is no view of the heap: the allocator serves a block from a
  per-CPU magazine, so `heap.ADDRESS_IS_A_VIEW` in `test/fort/support/heap.ft` is false there,
  `fir_env.address_moved` answers 0, and each probe that samples an address asserts on it only
  where the constant is true (T-256, T-270). A probe then reads the system view only, which on
  macOS counts live bytes exactly and reads 0 when the rounds keep nothing, so its bound must sit
  below the smallest release it watches: `types_table_test.ft` missed a lost `del(t->layout)`,
  3136 bytes over its rounds, under a bound of 8192, and takes 1024 on macOS since (T-270).
  LLVM 18 made the macOS address checks fail 9 rounds in 10 with nothing leaking (T-270).
  **A round large enough to witness a big release makes the block view useless**, which is the
  other half of the same measurement: a round that allocates a hundred kilobytes and gives it all
  back leaves free chunks of every size, so the 32-byte probe lands wherever one starts and ranged
  over 450 KB with nothing leaking. Switch the block view off for such a round and say why in the
  test, and earn it: every release that round covers must then be large enough for the *break* to
  move, which the deletion experiment confirms one release at a time.
  **Two calls that release the same vector cannot be witnessed apart.** `gen_free`'s `slots_free`
  and `gen.function_begin` both release the emitter's slot vector, and deleting either
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
  The production harness gives each subprocess a separate 60-second limit.
  Compilation includes the compiler's clang compile and link.
  IR emission, LLVM verification, helper linking, and execution each have separate limits.
  The harness kills the subprocess group when its limit expires.
  The release preset builds the product compiler with `--release`.
  Module fixtures without that flag use LLVM O1.
  Byte cleanup programs use one allocator mode per entry.
  Each mode keeps 64 warm rounds, one baseline round, and 256 measured rounds.
  Each mode retains the last-sixteen sample window and both allocator bounds.
  Owner modes discover move cost once before warm rounds.
  They retain populated source history and tentative destination history in each transaction.
  **A fixed refusal position drifts when the kernel adds work before it.** Paired R counting moved
  the end of clone pricing to charge 9157 of 21428 in the populated round and 687 of 1610 in the
  basic round. The storage positions 5068, 5468 and 6806 and the B26 positions 519 and 622 then
  refused before the clone. A deleted failure-path release stayed green in all four suites.
  Derive each position from one control run with `ownership_raw_bytes_env.watch`. Assert the
  terminal source line of each refusal.
  **A deleted shared release proves nothing about one operation's records.** The test fixture
  frees its own states through the same release functions, so the probe goes red for the
  fixture's leak too. To attribute teardown to one record class, leak that class alone in the
  release path under test: move it out to a local that is never freed, and keep each other
  release. `test/fort/ownership_raw_join_cleanup_test.ft` measures its record classes that way,
  on the refusal path and on the published path.
  **Linux `malloc_info` counts tcache chunks as live.** The metric's own `open_memstream` buffer
  fills one tcache bin of seven chunks. A round that never uses that chunk size then reads 848
  bytes more in each of the first seven measured rounds, 5936 in all, with no leak. The partition
  counter probe failed CI that way on both Linux jobs. Call the metric in every warm round too.
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
  T-255 deleted the `slots_free` pair, `scopes_free` and the release of `defers` with the direct
  path, so 43 of those releases remain; the emitter probe then emitted 155159 bytes, still an
  mmap block, and nobody measured the 43 again.
  **A probe also sizes its round**: a release of one small block per round is seen by neither view,
  because the allocator serves the next round's block out of the chunk the round just freed while
  the break stands still -- deleting `del(t->params)` in `types.ft` left `types_table_test.ft`
  green while every other release of that function turned it red (T-084). The round therefore
  repeats the allocation the release covers (two hundred parameter lists, a thousand child lists)
  until the leak is a round's worth rather than a block's, and the deletion test is what says it is
  enough. **Such a probe runs first in its program**: the free chunks that earlier tests leave
  absorb its leak. `small_rounds_leave_the_heap_where_it_was` in `fir_flow_test.ft` leaks 64
  bytes in each of 100 calls per round when `bits_free(&next)` is deleted. Run after the other
  tests of the program, its break read 0; run first, it read 675840 (T-233).
  **A `//! stderr:` directive cannot see a line that should not be there**: it is a substring
  check over the whole run, so "this call wrote nothing" is asserted by capturing the descriptor
  into a file and comparing the bytes: `test/fort/support/capture.ft` does the `dup`/`dup2` and
  the flush around it, and `diag_mute_test.ft` and `driver_test.ft` call it rather than repeating
  the redirect. A mute that kept printing passed the directive form of that test and failed the
  captured form.
  **Product compiler scope.** The `lang` test uses `xfail.txt`. It runs the complete product
  corpus with the current standard library (T-160).

- **`tools/panic_coverage.py` holds each panic site against its tests** (ctest `panic_coverage`,
  D11.4). A site is one `panic(...)` call in `src/fort/*.ft`, `src/lsp/*.ft` or `std/**/*.ft`. A
  `test/fort` or `test/lsp` panic test answers a site when one `//! stderr:` text holds the whole
  text of the site and is part of `panic: <text>`. A `test/lang` program that aborts answers a `std`
  site in the same way, and no other site. The tool fails for a site with no test, a panic test that
  answers no site or two sites, two sites with one text, and a stale declaration. Above a site,
  write `// panic-coverage: unreachable: <reason>` when no caller can reach it. Write `//
  panic-coverage: tests: <stem> ...` when its text comes from a caller, as in `panic(who)`.
  `test/panic_gaps.txt` lists the sites with no test: 119 of 248 at its first commit. When you add a
  test of a listed site, remove its line in the same commit; the tool fails until you do. A new site
  needs a test or a declaration, not a new line. The tool accepts a new line, so review holds that
  the list only gets shorter, as it does for `test/lang/xfail.txt`. A `test/fir` file is not read,
  so the `fir_verify.verify` site names its `test/fort` tests only.
  **A site that a test can call is reachable, even when no caller in the compiler breaks the
  guard.** D9.6 exports every function, so a test calls it with the precondition broken. A count
  that is a plain `u64` or `u32` field, such as `f.locals.len` beside its `own` span, is a value
  that a test sets: 6 `fir` and 11 `ownership_state` tests set one to its limit. Three of the
  first five unreachable declarations were wrong in this way, and a probe of each one exited 134.
  The other two guarded the length of a span, which the forged length below reaches.
  A struct value is a value that a test builds, so an argument that needs a well-formed value
  fails: `flt.round_ratio` assumed trimmed bignums, and a divisor with a zero top limb reached
  its guard. Begin each unreachable reason with `probed:` (a program ran and stopped before the
  site) or `read:` (an argument from the code). One argument is sound for a whole class: a panic
  after a `switch` that names every member of an enum is dead, because D7.7 gives that switch a
  trapping `default`. `cast(250, types.type_kind)` stopped with `enum value 250 is not a member of
  type_kind`. **A length can be forged, so a guard on the `.len` of a span is reachable.**
  `p[lo..hi]` on a pointer makes a view with no runtime check (D6.9). A pointer cast (D3.14) and a
  same-size reinterpretation (D10.7) can write the pointer or the length of an `own` span:
  `cast(&tt.nodes, raw mut*)->len = n`, with `raw` a struct of two `u64`, reached
  `types.reserve`. D6.9 and D10.7 leave the extent to the ownership proof, which runs by default
  only after the ownership series. No test forges a length or a pointer, so such a site stays
  listed. A site that needs more memory or time than a test can use stays listed too. The comment
  lines above an entry or a group give the reason. The list holds 26 of 248 sites after the first
  sweep: 22 that need a forged length and 4 that need memory or time.
- **`test/fir` holds the FIR tests**, one `.fir` file each, in the format that `test/fir/README.md`
  gives (`spec/fir.md` 16.3). The header is the leading run of `//!` and `//|` lines: one or more
  `//! pass: <name>`, an optional `//! prelude:` block of fort declarations, and one outcome,
  `//! expect:` with its `//|` lines, `//! error: <text>` or `//! panic: <text>`. The FIR text of
  the function follows the header, and the FIR parser reads the header as comments.
  `test/fir/run_tests.py` runs `fort --fir-test <file>` from `test/fir` for each file: the
  compiler checks the prelude as the module `main`, parses the text against it, runs the passes in
  order and writes the module. The harness compares stdout with the `expect:` block and shows a
  unified diff for a difference; `--bless` rewrites the block. Exit 2 from the compiler is an
  `ERROR`. The pass `build-mode` runs no verifier, so each of the 20 tests of
  `test/fir/build_mode/` names `verify` in a second `pass:` directive (T-247). The ctest `fir` is
  a unit test and runs the compiler natively, with the environment of the sanitizer presets. Name
  a test `test/fir/<pass>/NNN_name.fir`. `agents/lines.py` counts no `.fir` file as a test line, and
  `test/highlight_test.py` counts no `.fir` file either (T-224).
  **The pass `verify` ends the compiler with SIGABRT at the first violation**, which `//! panic:`
  matches, so each red case of a rule is a file of its own, `test/fir/verify/vNN_name.fir`. Two
  violations have no text: the grammar gives every block a terminator (V1), and the parser
  refuses a block that the function does not have (V2). Their `.fir` files hold the parser's
  diagnostic, and `test/fort/fir_verify_v01_panic_test.ft` and `fir_verify_v02_panic_test.ft`
  build the violation with the constructors. `test/fort/fir_verify_test.ft` reaches every clause
  in one program through `fir_verify.find_violation`, which returns the violation and does not
  panic. It parses each case against a prelude and a sandbox `std/rt.ft` that declares the eleven
  runtime entries, because a `check_env` sandbox has an empty runtime (T-229).
  **The flow rules V9, V11, V12 and V13 run after every local rule**, so a green case of a local
  rule may break a flow rule: 92 of them read a temporary that nothing assigns (V13). The helpers
  `green` and `green_func` accept a flow violation, and `flow_green` accepts none. A green `.fir`
  file keeps every rule, because the pass `verify` runs them all. `//! panic:` matches a
  substring, so a red file whose text passes 100 columns drops the prefix `fir.verify: ` (T-233).
- **A test of the FIR lowering compares a whole function, and derives it before the run**
  (T-236). `test/fort/support/lower_env.ft` checks a source in a sandbox whose `std/rt.ft`
  declares the eleven entries of the check kinds and `str_eq`, lowers one function, runs
  `fir_verify.find_violation`, and gives the printed text. A location in FIR is where the parser
  starts the node: the operator of a binary or unary node, the `(` of a call, the `[` of an
  index, the `.` of a field, the `?` of `?:`, and the keyword of a statement or a `cast`. T-236
  took one location of an arm `move(p)` at the name and not at its `(`, and the run showed it.
  Count the column of that token, or compute it from the source text with a script. The sandbox
  runtime also declares the nine print functions and `enum_member` (T-239). A line of FIR can pass
  the 100 columns of a source line, such as a `print_enum` call with its member table, so
  `lower_env.expect` joins a wanted line that ends with `\` to the next entry; no line of FIR ends
  with `\`. Three of T-239's hand-derived texts failed at their first run, and the lowering was
  right each time: a column of `&` read for that of `(`, the `\"` that the printer writes in a
  `bytes` text, and the lengths of `i32[2][3]`, which D3.6 reads like C, two arrays of three.
  Settle a column with a script that prints the column of each token, and a length with D3.6.
  **A change that adds statements to most expected texts re-derives them with a bless run that
  names the kinds of change it accepts** (T-244). The `dead` markers of T-244 changed 61 texts in
  8 suites. For one run, `lower_env.expect` printed the wanted and the lowered text and did not
  panic. A script then wrote a text back only when each difference was an added `dead` line or
  a store whose location alone moved, and it printed each moved store for review. Any other
  difference stops the script and writes nothing. The texts of new behavior are still derived by
  hand before their first run.
- **A test of the build-mode pass runs the verifier on both sides of the pass** (T-247).
  `test/fort/support/pass_env.ft` parses a FIR text against a prelude, or lowers a source, in the
  sandbox of `lower_env`. It runs the verifier, `fir_passes.build_mode` and the verifier again,
  and holds that each block has the number of its position. Give each `check` of a hand-written
  text a continuation of its own: a continuation that a `switch` or a `goto` names too breaks V11,
  and two of the texts of T-247 did so before their first run.
- **A `test/fort` program cannot open `std/rt.ft`, so the compiler holds a runtime signature
  when it compiles the test.** `run_tests.py` runs the program in a `mkdtemp` directory that
  holds only the program, and `check_env` sandboxes get an empty runtime. Write
  `import std.rt;` and bind the entry to a local whose declared type spells the whole signature:
  `fn (i64, u64, char*, u32, u32) noreturn bounds = rt.fail_bounds;`. A declaration that differs
  is a compile error, and the run then holds the module's table against the same spelling.
  `test/fort/fir_terminator_test.ft` holds its eleven check rows this way (T-213).
- **A test that drives a program over a pipe must size the script against the pipe, which holds
  64 KiB.** Nothing drains the pipe while the program under test reads it, so a script above the
  capacity blocks the writer -- the test itself -- and the run dies at `run_tests.py`'s 60 s
  timeout with no output to read. `test/lsp/lsp_protocol_test.ft` writes its whole script and
  closes the writing end before the server starts, which is safe for the few hundred bytes of a
  recorded exchange and for the answers, which are smaller. A script of two thousand messages
  goes through a **file** instead (`io.write_file`, then `io.open_read`), which has no capacity:
  `test/lsp/lsp_server_lifetime_test.ft` does that, and the harness gives each test a directory
  of its own, so the file is the test's alone (section 3). Two pipes and not one, when the
  program answers: one for its input, one for its output, and the output is drained after the
  loop ends (T-064).
- **A binary a CMake target builds needs a test that executes the binary**, and a suite of the
  modules inside it is not that test. Every `test/lsp` suite of `src/lsp` drives the modules in
  one program of its own, so `src/lsp/main.ft` and the wiring around it -- stdin and stdout as
  the descriptors, the loop's status as the process status -- were covered by nothing until
  `test/lsp_binary_test.sh` fed the binary a recorded script (ctest `lsp-binary`, labels `lsp`
  and `integration`). Hold such a script against a wrong binary before
  trusting it: this one exits 1 for `/bin/cat` and for `build/<Host>/<preset>/fort` (T-064).

## 5. Product tests and the fixed point

- Product tests use `build/<Host>/<preset>/fort` and `build/<Host>/<preset>/std`. They do not
  compare the C compiler with the fort compiler.
- The ctest `fixpoint` compiles HEAD twice. It compares the two LLVM modules and the two compiler
  binaries. It also verifies both modules with LLVM. It does that in four build modes: checked,
  `--release`, and each with `--no-bounds-check` (T-261). CI runs it on the Linux and the Darwin
  runners (`notes/environment.md` 7).
- A successful native build proves the bootstrap edge. The C compiler builds bootstrap-1.
  Bootstrap-0 builds the next compiler in the C-started chain.
- External-stage1 mode uses an external compiler to build HEAD. It registers no bootstrap tests
  and no C unit suites (T-160).

## 6. Generated code

- `run_tests.py --verify-ir` runs `opt -passes=verify` over the `-S` output of every language
  test that compiles, and every emitter suite verifies each module it emits. The worked examples
  of `spec/toolchain.md` 6 are held by the goldens of `bootstrap0/test/gen_module_test.c`. Until
  2026-09-25 hand-written modules under `test/ir/` and `test/pipeline_test.sh` ran the pipeline
  over them, and four host scripts (`core`, `net-layout`, `allocation-failure`, `abi-probe`)
  compared the std with the C headers and read clang's assembly; each fact they held that the
  corpus did not is a corpus test now (`run/ffi/013` writes the BSD `sin_len`, `run/errors/034`
  and `run/modes/031` request a block nothing reads), and the rest was covered by `run/ffi`,
  `run/stdlib`, `test/fort/gen_*_test.ft` and `fixpoint`.
- **The gen suites emit with no runtime in the closure**, so the calls the emitter writes into it
  reach a name the module neither defines nor declares, which LLVM rejects as a forward reference
  to nothing. `verified()` appends a `declare` for every row of `runtime_sig.h` to the file the
  verifier reads and to nothing else, so `ir()` stays the emitter's own text
  (`gen_runtime_declarations` in `bootstrap0/test/common/gen_helpers.h`, which skips a name the
  module defines). A test that needs the runtime's own definitions -- the `#8` of item 14, the
  `%fort.enum_member` of item 2, the dependency order -- calls `emit_with_runtime`, which writes a
  small `std/rt.ft` in the sandbox and names that directory. A `fn noreturn` in such a stub needs a
  terminating statement (D8.4, D8.5): an empty body is `a noreturn function must end in a
  terminating statement`, and `while (true) { }` is the shortest one that asks for no intrinsic of
  its own. **The fort gen suites need a runtime in the closure.** `test/fort/gen_*_test.ft` emit
  through FIR, which refuses a check or a print whose runtime function the closure lacks, so
  `gen_env.open_body`, `open_src` and `open_module_rt` write `lower_env.RUNTIME` as the
  `std/rt.ft` of the sandbox, and `open_module` keeps the empty one for a case that holds the
  refusal (`gen_fir_test.ft`). A suite of one expression reads the FIR text of the whole `main`
  (`gen_env.main_text`, which runs the block check), so its expected lines carry the numbers
  that they have in that definition (T-255).
- **`tools/ir_snapshot.sh <fort> <std-dir> <out-dir>` writes the `-S` output of the run
  tests of `run_tests.py --list run/`** in three modes: `default`, `release` and `nobounds`
  (T-192). `<std-dir>` is the staged `build/<Host>/debug/std`, not `std/`, which the compiler
  refuses. A ticket that must not change the emitted IR uses it as its identity check (T-193).
  Take one snapshot with `main` and one with the branch, then run `diff -r`; an empty diff is the
  evidence.
  T-255 measured that the deletion of the direct path changed no module: the snapshots of
  414d8b7c and of its branch gave an empty `diff -r` over 1293 modules of 431 tests. Give both
  snapshots the same
  `<std-dir>` path, because every module holds the paths of the standard modules. A test that the
  compiler refuses goes into `<out-dir>/skipped.txt`. Measured on T-192: two snapshots of one
  compiler gave an empty `diff -r` on both hosts, and a mutant that wrote `align 2` for each
  `align 1` store in `gen_store` changed 1278 of 1278 modules.
  `tools/ir_snapshot.sh --fir-stats <fort> <std-dir> <out-dir>` also passes `--fir-stats`
  (D14.1) and writes the line `<mode> <test> lowered N of M` of each module to
  `<out-dir>/stats.txt`, with the sums of each mode at the end (T-253).
- **`tools/ownership_ir_compare.py --fort <fort> --std-dir <std> <entry.ft>...` holds that the
  ownership proof changes no IR** (D17.14, D19.8). For each entry and each of four modes
  (`default`, `release`, `nobounds`, `release-nobounds`) it runs two `-S` builds that differ
  only in `--ownership-check`, and it prints `equal`, `differ` with a diff, or `rejected`. A
  `--flag` that selects the analysis or names its report is a usage error. It exits 0 only
  when every case is `equal`. A selected build writes IR only at exit 0, so a `rejected` case
  measures nothing and is no equality. On 2026-10-07 no selected build exits 0, so the tool
  can give `equal` only once the proof accepts a program
  (`test/ownership_ir_compare_test.py` holds each verdict with a stand-in compiler).
- **The oracle of the FIR migration is `tools/fir_diff.py <before> <after> <out>`, and not
  `llvm-diff` alone** (T-253, `spec/fir.md` 16.2). It compares a snapshot of a compiler before
  T-253, whose direct path wrote every function, with a snapshot of a later compiler. Since T-255
  every compiler writes each function through FIR, so the snapshots of two such compilers compare
  with `diff -r` as above, and the script stays for a comparison with an old snapshot.
  `llvm-diff-18` reported no difference for a dropped `zeroext`, a doubled `align` or a dropped
  `sret`, at a call or on a definition: three
  mutants of `fir_llvm.ft` and five edits by hand of one module left its report empty. Its report
  is also no record of instructions: for an added `%tmp1` slot it printed
  `> %fd.0 = alloca i32`. The script runs `llvm-diff` over each pair of modules of two
  snapshots (`--llvm-diff llvm-diff-18` in the VM,
  `/opt/homebrew/opt/llvm@18/bin/llvm-diff` on the Mac), then compares every definition whose
  text differs and exits 1 when a difference is unclassified; its docstring gives the model and
  the rule of each item. **What a block of the model holds**: its writes and its terminator in
  order, under a label that a walk from `entry` gives, so two swapped branch targets change it.
  A load carries the writes that can reach it and write its storage, so a load may move past a
  write into other storage and not past one into its own. Two storages are one unless both are
  named objects (allocas, `%tmp` slots, parameters, `%ret.sret`, globals) or a local whose
  address has not escaped at the load meets a pointer that a load gave. A call writes every
  storage but a local that has not escaped and that it does not name; a print entry of `std.rt`
  writes no global of another module. The first review of T-253 found four holes in a first
  model that compared events in any order: swapped `br i1` targets (mutant M4 printed `false`
  for `true` and was classified), a memcpy of the wrong size or alignment, a load moved past a
  store into its own storage, and an extra store of zero. Measured on T-253 with main at
  2574dc63 as `<before>` and the branch as `<after>`, one `<std-dir>` for both: 108819
  definitions of 1281 modules differ, `llvm-diff` names 101827 of them, and none is
  unclassified, on both hosts. Of the definitions that each mutant changed, none is classified:
  50556 (M4, the targets of a `bool` switch swapped), 50990 (M5, the targets of a check
  swapped), 11190 (`zeroext`), 116943 (`align`) and 606 (`sret`); so are the three edits
  (`.tickets/evidence/T253/mutant_check.py`). The rules accept differences in text, and two rest
  on facts about `std.rt` and not on the text (a print entry writes no global of the program;
  the entry of a check kind, whose pointer arguments are global constants, reads no storage of
  the function and no aggregate result; `panic` is no such entry, because its message can point
  into a local), so the run corpus and `fixpoint` stay the witnesses of behavior. Review round 2
  found four more holes by hand edit (a load inside a run named by a count of writes, a memcpy
  source with no read tag, the fills of two exclusive branches moved to one memcpy, a store
  moved across a `panic` that reads it) and one real difference of the FIR path (the padding of
  a designated literal, which the oracle's `zero` rule accepted); the oracle refuses each now.
  Review round 3 found three more by hand edit (a read inside a run named by its path and count
  and not by its value, a memcpy source written on the back edge of a loop, a memset of a part
  against a memset of the whole) and one more real difference (stale padding in a member of a
  designated literal that a `%tmp` slot held), so the effect of a run now holds the padding of
  each struct that it splits, and the FIR side may zero padding but never leave it stale
  (`padding`). Over the run corpus the oracle exits 1 with three unclassified definitions,
  `reads` of `run/structs/014_literal_reads_old_value` in each mode: that is the one difference
  in behavior of `spec/fir.md` 12.5 (D6.3), and the test holds the output of the FIR path.
- **`fort --fir-verify-report` runs rule V9 over a corpus without a panic** (T-253, D14.1): it
  prints one line for each function that breaks a rule of `spec/fir.md` 10, its first
  violation, and exits 0.
  `.tickets/evidence/T253/v9_report.sh` runs it and `-S --fir-stats` over `test/lang`,
  `src/fort`, `src/lsp` and `std` in the three modes.

## 7. What a test cannot see

- **A terminal is a test environment no pipe can stand in for.** `test/lang/run_tests.py` captures
  a program's stdout through a pipe, so a rule that only holds on an interactive descriptor --
  D11.5's line buffering -- is invisible to the whole language corpus, which stays green with the
  feature deleted. `test/tty_test.py` (ctest `tty`, label `integration`) is the shape that sees it:
  it compiles one program with the built compiler and runs it twice, under `pty.fork()` and under
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
  writer blocks. `O_NONBLOCK` is still out of reach, because `std.libc` declares `fcntl` but no
  `F_SETFL` or `O_NONBLOCK`, and `socket` takes no `SOCK_NONBLOCK` here.
- **An errno that a later call could overwrite is not witnessed by a call that succeeds.**
  `net.close_and_fail` in `std/linux/net.ft` reads `errno`, closes the descriptor its failed call
  opened and writes `errno` back, so that a caller of `net.listen` reads the `bind(2)` error and
  not `close(2)`'s. Deleting the write-back leaves `run/stdlib/118` green, which names the exact
  errno of three failures (22, 111, 88), because `close(2)` of a valid descriptor succeeds and
  leaves `errno` alone. The branch that needs the write-back is a `close` interrupted by a
  signal, which no test here can arrange. Keep the write-back, and say in the source that nothing
  holds it (T-097).
- **A mutation audit measures what the corpus holds, which the ratio cannot.** T-077 broke, one at
  a time, one line of each of the 88 decisions `src/fort/check.ft` and `src/fort/check_stmt.ft`
  cite. It ran the fail tests, run tests, check documents, and fort module tests against each
  mutant. **79 of the 88 went red,
  4 survived and 5 could not be broken by a mutation at all**; the five are the rows where a
  citation names a rule another pass holds, and each is a build error or a lint failure rather
  than a test.
  Three of the four survivors had one shape, and the shape is the reason to run this on a
  transliterated module: the C twin is pinned by a unit test in `bootstrap0/test/*_test.c` that the
  fort port never got, while the language corpus, which judges both compilers, held the rule for
  neither. Mutating the three C lines names the three tests that hold them. The fourth survivor was
  held on neither side, which the same method measured.
  Four traps, each of which cost that ticket a wrong verdict or a wrong claim.
  **Select the oracle by what a test imports, not by its name.** A filter of `check` over
  `test/fort` ran 15 tests where 21 import the checker, so every "survived" verdict was a
  survival against 15 of 21 until the four were re-run against all 161.
  **Do not edit the test tree while a batch runs.** A harness that reads a file during the write
  reports a failure that belongs to no mutation.
  **Raise each active file counter in the commit that adds the file.** `CORPUS_FILES` in
  `bootstrap0/test/parser_recovery_test.c` counts fail tests. The same name in
  `test/highlight_test.py` counts its source corpus.
  T-145 added two net test programs and routed them into the grammar corpus; since 2026-09-25
  `run/ffi/013` and `run/stdlib/118` hold the C layout and the errno values on both targets.
  **Say how strong each verdict is.** A verdict a mutant measured, a claim probed by compiling a
  program, and a claim read off the source are three things, and an audit that gives them one word
  hides which rows a reader may rely on.
  T-078 ran the same method over the C emitter and used those three words for its rows. Four
  things it adds, for the audit after it.
  **The runner is in the repository and the table is data**: `tools/mutate.py` and
  `tools/mutations/emitter_bootstrap.json`, 76 rows of file, anchor text and replacement, so a
  later audit writes a table and not a program. T-154 adds 12 output-branch rows and 1
  cited decision, for 88 rows over 73 decisions now. `tools/mutate.py <table> --only D3.8` re-runs
  one row. The `--check` option builds
  nothing and reports every anchor that no longer
  matches its file, which is the one way a table of textual anchors rots.
  The runner has no unit tests since 2026-09-26 (`test/mutate_test.py` held its parser, anchors,
  guards, restore and timeout verdict); run `--check` before a round.
  **Order the stages by cost and stop at the first red one.** In the emitter that is one `ninja`
  and `ctest -L unit -R '^(gen|driver|selfcheck|runtime_sig|types_abi|mem)[a-z_]*_test$'`, a
  median of 6 s over a 3 s to 19 s range, against 320 s to 420 s for a round that goes on to
  `ctest -L unit` and `ctest -L integration` (`lang` before 2026-09-26). 69 of 72 rounds stopped at
  the cheap stage.
  **A test that reads a source must not judge a round that rewrites it.** The runner's own
  selftest once asserted the table's anchors against the live `bootstrap0/src/gen*.c`, went red
  inside a round and made a survivor read `caught`. `--check` reads the text with the applied row
  put back, so a second row on the same lines is not called stale.
  **Measure a guard on every row, not on one.** The first version of the second and third guards
  asked whether the replacement text occurred exactly once, and the test proved them on 2 of the
  76 rows. They were dead for 3 rows whose replacement repeats text the file already had (D3.14,
  D8.5, D20.4), so `--check` exited 1 on a table that had not rotted -- the failure the third
  guard exists to stop. The test now applies **all 88** rows one at a time, in about 0.4 s, and
  the rule reads "`old` is absent and `new` is present". Any test that asserts something about a
  source file carries this trap; ask what it does while the file is broken on purpose, and ask it
  for every row rather than for a representative one.
  **Couple the row count to the sources on purpose.** A ticket that adds or removes a `Dn.m`
  citation in the four files has three ways out. It adds a row and runs it
  (`python3 tools/mutate.py tools/mutations/emitter_bootstrap.json --only D6.14`). Or it adds a
  row that records why the rule needs no mutation. Or, if the citation sits on a line that
  implements no rule, it says so in the row and moves the citation. The coupling is what stops an
  audit going stale. T-046 froze `bootstrap0/src` on 2026-09-14, so from that date the count moves
  only when a bug fix adds or removes a `Dn.m` citation in one of the four files.
  **Mutate through `tools/mutate.py`, and never leave a mutated source across a tool call.** The
  runner restores in a `finally`, reads the copy it saved rather than `git checkout`, and ends a
  run by rebuilding and comparing the md5 with the baseline, except after a timeout row, where it
  restores the files, builds nothing and says it measured nothing. So a restore that did not
  compile is visible at once. A mutation by hand has none of that: on 2026-09-14 two
  implementors stopped at once on a spend limit, and T-127's worktree then held a mutation of
  `bootstrap0/src/check.c` and `src/fort/check.ft` that reverted T-128's fix. Nothing in the
  repository said the tree was mutated. Three rules follow.
  Run `tools/mutate.py <table> --check` before the round, not after it rots.
  **Five tables exist today**, and they cover 17 files:
  `ls tools/mutations/*.json | wc -l` prints 5.
  `grep -o 'bootstrap0/src/[a-z_]*\.c' tools/mutations/emitter_bootstrap.json | sort -u` prints
  the four files of the C emitter, and the same command over
  `tools/mutations/checker_bootstrap.json` prints `check.c` and `check_stmt.c`.
  `grep -o 'src/fort/[a-z_]*\.ft' tools/mutations/emitter_fort.json | sort -u` prints the six
  files of the fort emitter: `fir.ft`, `fir_llvm.ft`, `fir_lower.ft`, `gen.ft`, `gen_data.ft`
  and `gen_fir.ft`. The same command over `tools/mutations/ownership_local.json` prints the four
  ownership files of the local increment: `ownership_flow.ft`, `ownership_report.ft`,
  `ownership_source_local.ft` and `ownership_transfer.ft`, with one row for each local check.
  `tools/mutations/ownership_driver.json` adds `driver.ft` and covers `fir_lower.ft` and
  `ownership_report.ft` again, with one row for each behavior of the selected driver groundwork:
  the failed report close, the selected build, the C compiler, the brace range, the imported
  uncalled bodies and the FIR ownership pass. Its one stage runs every targeted test, so the log
  of a row names each test that goes red, and not only the first.
  Each list is the `"sources"` list the runner saves. Two earlier changes
  mutated `src/fort/check.ft`, and then both compilers, by hand, because no table covered either
  file. No table covers the fort checker today.
  **The first row of a VM run can read `stale`.** The host writes the mutant, and the guest
  `ninja` compares its mtime with an output that the baseline build wrote in the same second.
  The row `D17.14-residual` read `stale` as the first row of two runs and `caught` as the second
  row of a third run (md5 `e317a12eab36bd2ea481fd6eccdc18ce` against the baseline
  `01afd107d70cd091e469ea64c1c5b8d2`). Re-run a stale first row behind another row.
  **The checker table reads its rows from history.** Commit 39a58dd8 deleted every `Dn.m`
  citation of `bootstrap0/src/check.c` and `check_stmt.c` and kept the code. The table has one
  row for each of the 78 decisions those files cited at `39a58dd8^`, one row for each of the 6
  spec sections they cited with a rule that no decision row breaks (`grammar.md 4`,
  `module-system.md 8.1`, `module-system.md 8.3`, `toolchain.md 7.3`, `toolchain.md 9.2`,
  `toolchain.md 6 item 3`), and eight second-facet rows: 92 rows.
  `grep -cE 'D[0-9]+\.[0-9]+' bootstrap0/src/*.c` prints 0 for each file, so the coupling rule
  above has no citation to read in `bootstrap0/src`. A bug fix that moves a rule edits the row's
  anchor, and `--check` finds the row that it breaks. No gate runs `--check`: run it by hand
  after a change to either file.
  **The checker table runs on a darwin host, without the VM.** Run
  `python3 tools/mutate.py tools/mutations/checker_bootstrap.json --runner 'bash -c'` after
  `cmake --preset debug`. A round costs a median of 6.9 s (3.6 s to 60 s), and 85 rounds of one
  run cost 735 s. Its `build` command sets `ZERO_AR_DATE=1`, because the darwin linker
  writes the modification time of each object into the binary. Without it two builds of one
  source gave two md5 values, so the last line of the runner would read `MISMATCH`. With it,
  two builds gave one value.
  **The fort emitter table reads its citations from history and its lines from today.** Commit
  c9b834a7 deleted the citations of `src/fort/gen.ft`, `gen_expr.ft`, `gen_stmt.ft` and
  `gen_data.ft`, and b0606efa then replaced the direct path by FIR. At `a3e0c95d` those files
  cited 86 decisions, the bare `D16` (5 times), 19 `item N` and 2 sections of `toolchain.md`;
  `gen_fir.ft` cites `toolchain.md 1` today. The table has a row for each at the current line that
  decides the rule, two for D16, and 8 further rows, except four rules: 114 rows. These four rules
  have no row and no mutation-measured verdict. D2.4 has no emitter line: the lexer refuses a
  reserved word (`fail/lexical/002_reserved_word`). D9.5 has no emitter line either: `modules.ft`
  refuses an import cycle before the checker runs (`fail/modules/001_circular`). The emitter line of
  D4.4 is a panic that `tools/panic_coverage.py` holds as unreachable: `consts.cv_to_float` reaches
  it only for an integer constant, and an integer of less than 2^64 in magnitude is finite in `f32`
  (`consts_float_test`). The emitter line of D17.8 is a panic that
  `fir_lower_builtin_value_panic_test` reaches with a forged statement: `lower_builtin` meets no
  builtin but `move` there, and the checker refuses a discarded `move`
  (`fail/ownership/018_discard_own_rvalue`). No compile reaches either panic, so a mutant of either
  line measures no rule of the emitter.
  **A mutant of `src/fort` reaches two places.** bootstrap-1 builds the product compiler from it,
  and each `test/fort` suite compiles the mutated module itself. So a row that turns every suite
  red usually means that the compiler stopped on `std.rt`, often with a panic of `fir_verify`.
  It does not mean that every suite names the rule.
  **Its stages are `run_tests.py` runs, so the runner names no test.** `failing_tests` reads the
  summary of ctest. Read the `FAIL` and `ERROR` lines of the round's log under `--log-dir`.
  Keep the stages targeted: the FIR corpus, the `gen_` and `fir_` suites, the `driver_` suites
  and four areas of the language corpus. PR CI runs the full suites. Run it with
  `--runner 'bash -c'` after `cmake --build --preset debug`. A round cost a median of 70 s (5 s to
  205 s) over 31 rounds of the final run.
  A mutant must still compile under `-Werror`. A replacement that makes a static function, a
  parameter or a variable unused stops the build (`build-failed`, 8 of the first 81 mutants).
  Keep the name in the expression: `if (!cv_fits(v, underlying) && false)`, not `if (false)`.
  Where no table covers the file, apply and restore in **one** shell command, from a pristine copy
  that same command made.
  Prove the restore with `md5 -q <file>` against the value it read before the mutation, and with
  `git status --short`. **Never restore with `git checkout <file>`**: it throws away uncommitted
  work in the same file, which is `tools/mutate.py`'s own stated reason for saving a copy. That is
  the one reason. Restore with `git show main:<path> > tmp && mv tmp <path>`, which touches no
  other work; the coordinator restored T-127's two files that way and `md5 -q` then read
  `bbde7b5ca321f3e1834837d6474ae5e0` and `9c18ea9291e8c35bb91cfcda822a6f8f`, which are main's
  (T-134).
  `--check` is not part of the gate since 2026-09-26; run it by hand (`88 rows, 0 stale` on
  T-134's tree).

- **An oracle is only an oracle where it derives its answer differently, so say
  for each half of one whether it is independent or shared.** T-064 swept every offset of a
  document against a hand-written oracle and the sweep stayed green over a bug in the module's
  line rule: `oracle_character` in `test/lsp/lsp_text_test.ft` derived the end of a line with the
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
  through `decisions.rule_of` (`test/decisions.py`), which returns the `rule` field of one entry,
  (T-100): a test that opens the
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
  `test/lsp`, `test/tty` and `test/ownership/approved` (T-308).
  Approved ownership examples receive lexical coverage during fixture preparation.
  T-292 later qualifies their ownership verdicts.
  `CORPUS_FILES` is the exact number of files in this list, so a
  ticket that adds or removes a `.ft` under any of them reads the new number off the failure and
  writes it there, as it does for `CORPUS_FILES` in `bootstrap0/test/parser_recovery_test.c`.
  The other `.ft` files are listed in `EXCLUDED_DIRS`, each
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
  **Set `PYTHONDONTWRITEBYTECODE=1` to stop cache writes during a Python test run.** CMake sets
  this variable for Python ctests. This variable and `-B` do not stop cache reads.
  T-104 reads the first mutant's cache after changing the source within a few seconds.
  T-309 measures cached `CORPUS_FILES = 817` while the current source says 831.
  Both have modification time 1790941933 and source size 35741 bytes.
  Python accepts the timestamp-based cache because these header values match.
  Load current source with `compile` and `exec` for count measurements.
  Keep cache files that another process can use.
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
  (target `lint`) holds `std/*.ft`, `std/<target>/*.ft` and
  `src/fort/*.ft` to the identifier conventions of D1.4 and to 100 columns;
  it reads `fort --index` (D20.3) rather than tokenizing
  fort a second time, so the kinds and types it reasons about are the checker's own answers, and a
  second tokenizer cannot drift from the language. `lint` passes `--target`, so each host lints
  its own `std/<target>/*.ft` under its own root: one root declares one libc, and the other
  target's modules are linted on the other host (the `cross-target` test that linted them under an
  assembled root was removed on 2026-09-26).
  `test/highlight_test.py` tokenizes `std/`,
  `src/fort/` and `test/lang/run` against the TextMate grammar, which is the only check that grammar
  has. `agents/lines.py` counts `std/*.ft`, `std/linux/*.ft` and `std/darwin/*.ft` as source for the
  test-to-code ratio (T-144).
  The compiler and runtime are source too (D14.6).
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
  `python3 agents/lines.py --since main --min 3.0` is an acceptance criterion of every
  ticket that adds **source** lines -- the compiler, `std/*.ft` and the runtime, which are what
  D14.6 counts since T-076, so a ticket writing only library or runtime code answers for its
  tests like any other -- and the implementor runs it before the gate. The repository ratio
  drifted from 3.34 to 2.50 over six tickets while every one of them passed, because a large corpus
  hides a thin diff, and it measured 2.41 on 2026-09-11 (22776 source lines against 54891 test
  lines, the first figure to count `std/` and the runtime). The two numbers answer different
  questions and both are working: the per-ticket rule is not retroactive, so the corpus figure is
  a lagging indicator of everything that landed before it and climbs only asymptotically even if
  every future ticket hits 3:1 exactly. Measure it with
  `tools/vm run 'python3 agents/lines.py'` before quoting it; never repeat the figure from this
  line. A ticket that cannot reach 3:1 says so in its log with the reason rather than lowering the
  number. Run it on the host when the worktree has a VM of its own, because git in that guest
  cannot open the main checkout (`notes/environment.md` 3, T-099); `tools/vm run 'python3
  agents/lines.py --since main --min 3.0'` works only in a VM brought up from the main checkout.
  `--since` reads `git diff main...HEAD`, so it counts **committed** work only. A branch
  whose tests are still staged or untracked reads as the ratio of the commits before them. That
  number is smaller than the truth, and it sends an implementor off to write tests the branch
  already has. Commit first, then measure. Run from a worktree, it needs
  `--root <worktree>`: the default root is the parent of the script's own directory, the main
  checkout, and there it printed `no source lines added` for a branch with 100 source lines
  (T-257, 2026-09-29). Write `python3 agents/lines.py --root "$PWD" --since main --min 3.0`.
- `agents/lines.py` does not see `editors/`. A branch that changes the VS Code extension alone
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
- **`agents/lines.py` does not see a test written in shell or in Python either** (T-131). Its
  `TEST_GLOBS` are `bootstrap0/test/*.c`, `bootstrap0/test/common/*.h`, `test/**/*.ft`,
  and `test/lang/ffi/*.c`. Its `SOURCE_GLOBS` include the two compilers,
  `std/*.ft`, `std/linux/*.ft` and `std/darwin/*.ft` (T-144). So a ticket whose deliverable is
  the build or a tool gets no useful figure: T-131 changed `CMakeLists.txt`, six scripts under
  `tools/` and the language harness, and the tool read
  `+2 source lines, +0 test lines, ratio 0.00`, the 2
  being its one edit to `std/rt.ft`. Measure such a branch by hand, as the `editors/` bullet
  above does, with `git diff --numstat main...HEAD` and the tool's convention: net lines, raw
  `wc -l`, comments and blank lines on both sides. T-131 measured 469 net lines of build, tool and
  harness against 258 net lines of test, which is 0.55. Its log says why it cannot reach 3:1.
  A CMake graph needs a real build. A native workflow and the `fixpoint` test hold that graph.
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
  (11). The corpus figure of `agents/lines.py` moves with the same shortfall: it read 2.17 on
  47f98c2 (52163 source lines against 113082 test lines) against the 3.0 D14.6 then set. **The
  user took that decision on 2026-09-14: no backfill, and the target drops to above 2.0**, which
  the corpus meets (2.19 on 865a15c, `python3 agents/lines.py`). The per-branch criterion stays at
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

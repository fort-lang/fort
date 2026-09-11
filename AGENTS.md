# fort
A safe(r) C-like systems programming language.

## Naming
- The language is `fort`. Source files use the extension `.ft`. The compiler binary is `fort`.

## Project Layout
- `notes/`: the language specification. `notes/decisions.md` (numbered decision log) and
  `notes/grammar.md` are normative and win over every other document. Start at
  `notes/project-overview.md`.
- `test/`: `test/test.h` is the C macro framework for the compiler's unit tests
  (`test/common.h` provides `TEST_UNUSED`); `test/lang/` holds language tests in the directive
  format defined in `notes/toolchain.md`.
- `src/bootstrap/`: the C bootstrap compiler (stage1), frozen once the compiler is self-hosted.
  `src/fort/`: the compiler written in fort (stage2 and stage3). `runtime/`: the C runtime
  linked into every program. `std/`: the standard library in fort. `tools/`: `vm`,
  `provision.sh`, `lines.py`, `bootstrap.sh`.
- `editors/`: `editors/vscode/` is the VS Code extension (`package.json`,
  `language-configuration.json`, `syntaxes/fort.tmLanguage.json`, and `extension.js` with the pure
  modules it is tested through in `lib/`) and `editors/README.md` is its install guide, its manual
  smoke test and its list of limitations.
- `CMakeLists.txt`, `CMakePresets.json` and `cmake/sanitizers.cmake` are the build;
  `.clang-format` and `.clang-tidy` (clang 18) are the C11 lint configuration.
- `.tickets/` (gitignored, main checkout only) is the ticket board; `.claude/agents/` holds the
  `implementor` and `reviewer` agent definitions.

## Environment
- Everything builds and runs inside the Ubuntu 24.04 arm64 Vagrant VM defined by `Vagrantfile`
  (VirtualBox, `bento/ubuntu-24.04`); nothing is built on the host. Run vagrant only through
  `tools/vm`: `up` creates, provisions and starts the VM and caches its ssh config; `halt` stops
  it; `destroy [-f]` removes it (the box stays installed); `provision` re-runs
  `tools/provision.sh`; `status` and `ssh` do what they say.
- The VM directory (whose `Vagrantfile` and `.vagrant/` are used) is `$FORT_VM_DIR` if set,
  otherwise the main checkout of the current repository, so every worktree shares one VM. It is
  `/vagrant` in the guest and worktrees are `/vagrant/.worktrees/<name>`. The VirtualBox machine
  is named `fort-dev-<directory name>` (`fort-dev-fort` for the main checkout), so a VM brought
  up from another directory does not collide with it; destroy one before bringing up the other
  if memory is tight. `FORT_VM_CPUS` (default 6) and `FORT_VM_MEMORY` (MiB, default 8192) size
  the VM at `up`.
- `tools/vm run <cmd>` executes in the guest directory matching the host cwd, which must lie
  inside the VM directory (`ssh` and the cmake subcommands too; the lifecycle subcommands work
  from anywhere inside the repository, or anywhere with `FORT_VM_DIR` set). The cmake
  subcommands (`configure`, `build`, `test`, `workflow`, the targets below and `gate`) run at
  the top of the host git worktree containing the cwd and default to the `debug` preset. Every
  guest command sources `/etc/profile.d/fort.sh` and disables core dumps.
- When the Mac sleeps, VirtualBox pauses the VM ("paused due to host power management") and
  `tools/vm status` shows `paused`; guest commands then fail after the 10 s ssh timeout and
  `tools/vm up` cannot resume it. Recover with `VBoxManage controlvm fort-dev-<name> savestate`
  followed by `tools/vm up`.
- Only the VM directory is shared: `/tmp` in the guest is not the host's `/tmp`. A scratch file
  a host command writes there is invisible to `tools/vm run`, which is worse than an error,
  because the guest may hold an unrelated file of that name from an earlier run and the command
  then answers about it -- an experiment on a hand-written `.ll` verified a module that was not
  the one being tested. Put scratch files under the worktree (`build/` is gitignored) so both
  sides see the same bytes.
- The shared folder can serve stale pages to tools that `mmap` a file the host rewrote (seen
  with `clang-format` reporting a line past the end of a shrunk file while `md5sum` read the
  right bytes). The other face of it is the compiler reading the tail of a file the host has just
  rewritten as NUL bytes, `error: null character ignored [-Werror,-Wnull-character]` at a line
  past the end, while `git diff` on the host shows a clean edit. Recover with
  `tools/vm run 'sync; sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"'`, and delete that target's
  object as well, since ninja has already recorded the failed compile.
- The same folder can hand ninja a stale mtime, so a rebuild after an edit prints "no work to do"
  and the suite keeps failing on text the file no longer holds; `md5sum` in the guest reads the
  new bytes and dropping the caches does not help, because it is the timestamp and not the
  content that is stale. Delete that target's object
  (`build/<preset>/CMakeFiles/<target>.dir/<path>.o`) and build again. A mutation experiment --
  break a rule in the compiler, watch the test go red, restore it, watch it go green -- runs into
  this more than anything else, because every step rewrites a file the last step just built from,
  and a stale mtime makes the next step report the previous binary's colours. Edit and restore
  from the host, `touch` the sources there, and prove the restore by comparing the rebuilt
  binary's `md5sum` with the baseline's: a green suite after the restore does not prove the
  restore was compiled, and an md5 that differs from the baseline says some earlier step built
  nothing. That check is also how a build that silently skipped a source is caught, which is
  worth one `md5sum` before any measurement that will be quoted as evidence. Undo the mutation
  from a saved copy of the file, never with `git checkout <file>`: the file usually also holds the
  ticket's own uncommitted work, which that command throws away silently, and the suite stays
  green afterwards because the deleted work was the part with no test of its own yet.
- The target is x86-64 Linux. The compiler runs natively on arm64, emits LLVM IR and runs `clang
  --target=x86_64-linux-gnu` over it, so `--cc` names a clang (the guest `cc` is a native gcc and
  would build for aarch64); generated programs run under `qemu-x86_64` transparently. The verified
  line is in `notes/toolchain.md` 2 (D14.3), and `--target` names the triple (D14.1). Provisioning
  sets `QEMU_LD_PREFIX`; the test harness sets it itself. When a cross program dies by a signal,
  qemu-user appends `qemu: uncaught target signal 6 (Aborted) - core dumped` to the program's
  stderr; native execution prints nothing, so a harness comparing stderr drops that line
  (`test/pipeline_test.sh` and `test/lang/run_tests.py` do). The number and the name vary with
  the signal, so the filter matches the shape (`qemu: uncaught target signal`) and not one line.
  **No language test can go red from a leaked notice**, and the same holds for anything else a
  harness claims to strip from stderr: `//! stderr:` is a containment check, so an extra line
  only enlarges the text the substrings are sought in and every expectation still matches.
  Deleting the `drop_qemu_notice` call leaves the whole corpus green. The witness for a
  stripping rule is therefore a unit test that asserts the negative -- that a substring taken
  from the stripped line is *not* found -- which is `run_tests_test.py`'s
  `test_judge_run_qemu_notice_is_not_stderr` and `test_judge_run_signal_ignores_the_qemu_notice`;
  a `run` test that merely passes witnesses nothing here (T-071's review).
- Provisioning installs `nodejs` (Node 18) for the extension's unit tests and fails loudly when it
  is older or has no built-in test runner.
- An ssh `ControlPath` under `os.tmpdir()` does not work on macOS: the host's temporary directory
  is `/var/folders/<...>/T`, ssh binds the socket under a temporary name of its own, and the total
  passes the 104-byte Unix domain socket limit, so ssh exits 255 with `unix_listener: path ... too
  long` -- indistinguishable, to a caller, from a VM that is down. Anything multiplexing ssh from
  the host measures the path and falls back to `/tmp/<something short>`
  (`editors/vscode/lib/command.js`).
- Provisioning disables apport and sets `kernel.core_pattern=core`: Ubuntu's piped core pattern
  ignores `ulimit -c 0` and made every SIGABRT cost about a second. A VM provisioned before that
  change needs `tools/vm provision` once (or the same two commands by hand).
- git runs on the host; it also works in the guest: provisioning symlinks the host path of the
  VM directory to `/vagrant`, so worktree `.git` files (absolute host paths) resolve there.

## Build and test
- Presets (`CMakePresets.json`, Ninja, clang unless noted): `debug`, `release` (RelWithDebInfo),
  `gcc`, `asan`, `msan`, `tsan`, `ubsan` (the last four set `FORT_SANITIZER` for
  `cmake/sanitizers.cmake`, which instruments every native target but never the cross-compiled
  runtime object). `tools/vm workflow <preset>` configures, builds and runs ctest; build
  directories are `build/<preset>` inside the worktree. `-Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wvla -Wstrict-prototypes -Wmissing-prototypes -Wundef` apply to every C target.
- Targets: `fort_core` (static library, `src/bootstrap/*.c` except `main.c`, globbed), `fort`
  (`build/<preset>/fort`), `fort_rt` (`runtime/fort_rt.c` cross-compiled by `FORT_TARGET_CC`,
  a clang (default `clang`) with `--target=${FORT_TARGET_TRIPLE}` (default
  `x86_64-linux-gnu`), into `build/<preset>/std/fort_rt.o` next to a copy of
  `std/*.ft`), `fort_rt_native` (the runtime compiled natively with `-DFORT_RT_NO_MAIN` for the
  unit tests and tidy), `lang_ffi_helpers` (`test/lang/ffi/*.c` built natively so `-Werror` and
  tidy cover them), `check` (ctest label `unit`, including `lang_lint` and `lang_selftest`),
  `check-lang` (`test/lang/run_tests.py` with the built compiler; the same command is the ctest
  `lang`, label `lang`), `check-all` (both), `format` and `format-check` (clang-format
  over `src`, `runtime`, `test`), `check-comments` (`tools/check_comments.py`, which
  `format-check` depends on: it rejects a `/* */` in the same sources, D2.2, and its own unit
  tests are the ctest `check_comments_selftest`), `tidy` (`run-clang-tidy` over the same),
  `fort-lint` (`tools/fort_lint.py`: the identifier conventions of D1.4 over `std/*.ft` and
  `src/fort/*.ft`, read off `fort --index`; the ctests are `fort_lint` and `fort_lint_selftest`),
  `lines` (`tools/lines.py`: test lines per source line, source being the compiler, `std/*.ft`
  and the runtime (D14.6, amended by T-076), target 3:1, `--min RATIO` fails
  below it; `--since REF` measures a branch's own diff instead of the whole repository, which
  is how a ticket answers for the code it introduces rather than hiding behind the corpus
  already there; its own tests are the ctest `lines_selftest`).
  `tools/vm <target> [preset]` runs one.
- A CMake variable derived from a cache variable must not be cached itself: `find_program`
  caches by default, so `FORT_TARGET_CC_PATH` kept resolving to the old program after
  `FORT_TARGET_CC` changed in an existing build directory, and the build then ran gcc with
  clang's arguments. It uses `NO_CACHE`; check for the same trap before adding a `find_program`
  or `find_file` whose `NAMES` come from a cache variable, or delete `build/<preset>` after such
  a change.
- `tools/vm gate` is the merge gate: `format-check`, `tidy`, and `check-all` under `debug`,
  `asan` and `ubsan` (it configures `debug` first, then configures and builds each preset before
  its `check-all`).
- The `gcc` preset is the project's only cross-compiler check and it is **not** part of the gate,
  whose three presets are all clang, so nothing runs it unless someone does: run
  `tools/vm workflow gcc` by hand whenever compiler or test-helper code changes. It went unbuilt
  long enough to accumulate errors in shared test helpers (`-Wformat-truncation` on every sandbox
  path join, `-Warray-compare` on a pointer comparison written as an array one), which clang does
  not diagnose at all. `tools/vm build gcc` on its own, in a worktree that never configured the
  preset, reports "not a directory": that is the missing build directory, not the failure, and
  `workflow` does the configure itself. gcc's truncation warning is level 1, so using the
  `snprintf` result silences it: check it against the capacity and fail the test with a message
  (`join_sandbox_path`, `gen_join_path`), never widen the buffer or cast the result away. A guard
  only gcc enforces is a guard no test holds, so assert each one -- the call sites too, not only
  the helper -- as `test/modules_test.c` and `test/gen_test.c` do.
- A run test's program executes in a temporary directory (`tempfile.mkdtemp`) holding only the
  compiled program itself, so it cannot open a **pre-existing** file that ships beside the test. It
  may freely create a file there and read it back, which `run/stdlib/051`, `054` and `055` do. A
  program that must read a file it did not write is given `/dev/stdin` as its argument and fed by a
  `//! stdin:` block -- the harness gives the child a pipe on fd 0 and opening `/dev/stdin` re-opens
  it -- which is how `programs/wc.ft`, `cat.ft` and `wordfreq.ft` run. `programs/wc.ft` is
  additionally the fenced block of `notes/stdlib.md` 4 byte for byte below its directives, so an
  edit to either must be made to both; strip the `//!`, `//|` and `//<` lines and diff to check.
- Language tests: `test/lang/run_tests.py [filter]` (decisions D14.4, D14.5; toolchain.md 7.3
  describes every option and verdict). `test/lang/xfail.txt` lists tests the compiler cannot
  pass yet; a listed test that passes fails the run, so shrink the list in the same commit that
  makes tests pass. `test/lang/bootstrap-unsupported.txt` lists tests that use features the C
  bootstrap deliberately lacks (floats, multi-dimensional arrays, do-while, `?:`; function
  pointers are in its subset, D3.10); keep such features out of core tests, or split them into
  their own test, so the core tests exercise stage1. `run_tests.py --lint` validates directives
  without a compiler and runs before every test run; `run_tests.py --check-json` is a mode of its
  own (ctest `lang_check_json`, also run by check-lang) that holds the document of `fort --check
  --json` against the text form on every fail test and ignores `xfail.txt`, since it judges the
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
- Unit tests: `test/<component>_test.c` with `test/test.h`; the suite name is the file stem and
  `test/` already holds one per component, `runtime_test.c` being the C runtime's and not the
  compiler's, so check the name is free before writing the file (a shell redirection overwrites a
  suite silently and the gate then reports only its absence); every `test/*_test.c` is globbed
  into an executable `build/<preset>/test/<component>_test` linked against `fort_core` and
  `fort_rt_native`, and a ctest `unit-<component>`. A `TEST` body is one macro argument: a comma
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
- The TextMate grammar is checked by `test/highlight_test.py` (ctest `highlight_selftest`, label
  `unit`, run from `test/`): it reads the D2.4 keyword lists and the D2.10 operator list out of
  `notes/decisions.md` and the same sets out of the grammar, so the two cannot drift. That only
  works while the rules keep their canonical shapes, `\b(?:a|b)\b` for keywords and `(?:\+|-)`
  for operators; a rule whose scope is in a keyword or operator family but whose pattern matches
  neither shape fails the test. Its other half is a small TextMate engine that asserts the scopes
  of `test/highlight/scopes.ft` (`//^` lines: alternating text and scope fields naming what the
  line above must produce), of the D5.3 and D17.2 marker tables, and of every `test/lang/run`
  test, which must tokenize with no `invalid.` scope and no unscoped character, so a new language
  test that the grammar mishandles fails here.
- The VS Code extension's sources are plain JavaScript wrapped at 100 columns, and no gate target
  lints them, so the conventions are here: `'use strict'` at the top of every file, CommonJS
  (`require`/`module.exports`, no ESM and no bundler), `//` comments only as in C and fort (D2.2),
  two-space indentation, single quotes, semicolons, `const` unless a binding is reassigned, no npm
  dependency and no devDependency, and no API beyond Node's standard library and `vscode` (which
  only `extension.js` may require). A file is tested by `node --test` or it is `extension.js`.
- The VS Code extension is plain JavaScript on the VS Code API, with no npm dependency and no
  build step. Its logic lives in `editors/vscode/lib/*.js`, which never `require('vscode')`, so
  Node's built-in runner tests it: `tools/vm run 'cd editors/vscode && node --test'` (ctest
  `extension_selftest`, label `unit`, run from `editors/vscode`; `node --test` with no argument
  discovers `test/*.test.js` itself, and naming the directory fails on newer Node). `extension.js`
  is the only file that may touch the API, so keep it thin and move anything with a case analysis
  into `lib/`; it is driven through `test/fake_vscode.js`, which answers its `require('vscode')`
  and its `require('child_process')` by patching `Module._load` before loading a fresh copy of it,
  so a save, a crash, a hover and a definition are all tested with no editor and no ssh. What that
  cannot check is that VS Code calls the extension the way its API is documented to, which is what
  the manual smoke test in `editors/README.md` is for, and a change to `extension.js` is run
  through it by hand. Its fixtures are real compiler output: regenerate them with
  `fort --check --json --index` over `editors/vscode/test/fixtures/` rather than by hand, and a
  helper that is not a suite, such as `test/fake_vscode.js`, defines no test of its own, since
  Node 18 loads every file under `test/`.
- The cross pipeline: `test/ir/*.ll` are hand-written LLVM 18 modules in the form
  `notes/toolchain.md` 6 specifies (D19.1); `hello.ll` and `abort.ll` are its two worked
  examples byte for byte, so a change to one changes the other, while `floats.ll` and
  `colons.ll` answer questions of their own (`test/ir/README.md` says which).
  `test/pipeline_test.sh <build-dir>`
  verifies each with `opt-18 -passes=verify`, compiles and links it with `clang
  --target=x86_64-linux-gnu` and `<build-dir>/std/fort_rt.o`, runs it under qemu and checks
  stdout, stderr and the status byte-exactly; ctest `pipeline` (label `unit`). Adding one means
  adding its name to the `for prog in` loop of that script and a block of expectations beside
  the others; the list is not globbed, since each module's output is its own. `*.ll` is
  gitignored except `test/ir/*.ll`. `run_tests.py --verify-ir` runs the same verifier over the
  `-S` output of every language test that compiles.
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
- clang-tidy's `readability-inconsistent-declaration-parameter-name` fires only under
  `tools/vm tidy`, late in the loop: when a definition renames a parameter, for instance to stop
  it shadowing a new file-scope static, rename it in the header too, in the same edit.
- Running `run-clang-tidy` by hand: its positional arguments are regexes matched against the
  absolute paths in `compile_commands.json`, so a relative path such as `../../test` silently
  selects nothing and reports success. Use `tools/vm tidy` or absolute guest paths.
- Binaries: `build/<preset>/fort` is stage1 (the C compiler); `build/<preset>/stage2/fort` and
  `stage3/fort` are the self-hosted compiler built by stage1 and by stage2. The `fort_stage2`
  target builds stage2 at every build (stage1 over `src/fort/main.ft`, whose imports pull the rest
  of `src/fort` in), so a module stage1 rejects fails the build rather than the test run; stage2 is
  an x86-64 binary and runs under qemu like every program the compiler builds.
  `tools/bootstrap.sh [--preset <preset>] [--stage3]` drives the same steps by hand in the guest
  and, with `--stage3`, compiles `src/fort` with stage2 and compares the two binaries byte for
  byte -- the fixed point self-hosting means. It is not in the gate, and `--stage3` fails until
  stage2 can compile `src/fort`.
- Two corpora beside `test/lang` run through the same `run_tests.py`, which takes the corpus root
  as `--root`: ctest `lang-stage2` (label `lang`) holds the language corpus against stage2 with
  `--xfail test/lang/xfail-stage2.txt`, which starts as the whole corpus (`run/`, `fail/`,
  `programs/`) and shrinks as Phase B lands passes; ctest `fort-modules` (label `lang`) runs
  `test/fort/<x>_test.ft`, the tests of the compiler's own modules. Both are commands of
  `check-lang`, so the gate runs them. A `test/fort` test is an ordinary run test in the D14.5
  directives whose header carries `//! flags: -I ../../src/fort` (the compiler's working
  directory is the corpus root, so the path has two `..`, not three); its file name is
  `<module>_test.ft` or `<module>_<case>_panic_test.ft` for a test whose program must end in a
  panic, since a panic kills the program and each one needs a file; any other `.ft` at that root is
  `bad test name`, because a typo there would otherwise run nowhere and say nothing.
  **Code several of those tests share lives in `test/fort/support/*.ft`**, which they reach with a
  second include root (`//! flags: -I ../../src/fort -I support`): a `test/fort` test is a program
  rather than a translation unit, so the `#include`d helper a C suite would use
  (`test/types_helpers.h`) has to be an imported module (`support/types_env.ft`, T-032). The
  directory is invisible to the harness, and that cuts both ways: `discover` walks `run`, `fail`,
  `programs` and the `*_test.ft` of the root and nothing else, so **a test misfiled under
  `support/` runs nowhere and says nothing** -- `support/stray_test.ft` leaves
  `run_tests.py --lint` reporting `no problems` while the same file at the root is
  `lint: bad test name`. `fort_lint.py` globs `std/*.ft` and `src/fort/*.ft` only, so D1.4 is
  unchecked there as well (T-079 owns both holes). Put a test at the root and only shared code
  under `support/`.
  `tools/lines.py` counts `test/fort/**/*.ft` as test lines and `src/fort/*.ft` as compiler lines,
  and `test/highlight_test.py` does **not** tokenize them, so the TextMate grammar has no witness
  over `test/fort` (T-079). **`test/fort` is not a leak oracle**: the gate's `asan` and `ubsan`
  presets instrument the native compiler, not the x86-64 program the harness builds and runs under
  qemu, so a `del` a module forgets leaks silently through all three presets. A module that
  promises its allocations die with the value that owns them (D20.5) needs a witness of its own --
  in-band accounting the module already keeps (`containers.pool_used`), or the allocator itself:
  identical rounds are handed the same addresses again when a round frees what it took and fresh
  ones when it does not, so an address that repeats over eight rounds is the release
  (`the_blocks_a_node_owns_are_released_with_it` in `test/fort/types_table_test.ft`, T-032).
  Verify such a witness by deleting the `del` it covers and watching it go red; two of them in
  `types.ft` had no witness at all until that was measured.
  **An allocator probe needs two views, because each is blind to what the other sees** (T-034).
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
  two** rounds and take the smaller distance: the allocator alternates between two positions from
  one round to the next once the program's own path is long enough to change a bin -- which the
  harness's `mkdtemp` directory is, while a hand run from `/tmp/prog` is not, so a probe reads 0
  by hand and 12208 under `check-lang` -- and one of the two late rounds is in step whatever the
  period, while a leak moves both. A probe over a whole driver run belongs in a file of its own
  (`test/fort/driver_lifetime_test.ft`), since forty other tests in the same program fragment the
  heap for reasons that are not leaks.
  **A `//! stderr:` directive cannot see a line that should not be there**: it is a substring
  check over the whole run, so "this call wrote nothing" is asserted by capturing the descriptor
  into a file and comparing the bytes: `test/fort/support/capture.ft` does the `dup`/`dup2` and
  the flush around it, and `diag_mute_test.ft` and `driver_test.ft` call it rather than repeating
  the redirect. A mute that kept printing passed the directive form of that test and failed the
  captured form.
  `lang-stage2` passes `--no-unsupported`: `bootstrap-unsupported.txt`
  demands that the compiler *reject* the features the C bootstrap lacks, which stage2 is under no
  such obligation to do, and inheriting it would keep twenty entries in `xfail-stage2.txt` for
  ever. ctest `stage-usage` (label `unit`) diffs stage1's and stage2's `--help`, `--version` and
  usage line, which is what holds the option table `src/fort/main.ft` copies from
  `src/bootstrap/driver.c` to it.
- A directive lint gotcha: `run_tests.py --lint` rejects any line of a test whose text holds `//!`
  after code, so a comment inside a test that quotes a directive (`the //! stderr: lines`) fails
  the lint with `only '//! error:' may follow code on a line`. Say "the stderr directives in this
  test's header" instead.

## Technical Standards
- **Markdown**: Line-wrap at 100 characters, including tables and code blocks. Check with
  `awk 'length > 100 {print FILENAME": "FNR}' <files>`. Code fences use `fort`, `c`, `sh`,
  `llvm`, `json` or `ebnf` as the language tag.
- **Language changes**: any change to the language is recorded in `notes/decisions.md` first
  (new decision number or amended decision with a note), then in the specification document that
  owns the topic, then in the tests under `test/lang/`. Specification text never contains "TBD",
  "pending" or "not finalized"; deferred features live only in decision D15.
- **Renaming a term**: sweep the stem, not the word. `grep -rni slice` never matches "Slicing",
  so a rename of `slice` to `span` must sweep `slic` (and any other inflected stem) with
  `grep -rni` before the criterion is ticked; markdown width is not covered by the gate, so run the
  `awk 'length > 100'` check over every file touched. The same holds for a change of syntax, and
  its hard half runs the other way: the decisions and sections that *define* the old form are the
  ones a sweep finds and the author rewrites, while the ones that merely *use* it -- an example, a
  table row, a history note quoting the wording it replaced -- keep the dead spelling and read as
  normative ever after. So sweep for the old form's *shape*, not for the name of the thing that
  changed, which the uses never mention: for the east-const amendment that search is
  `grep -rnE '\b(mut|own) ([iuf][0-9]+|bool|char|string)\b' notes/`, a qualifier standing before
  a base type. Read every hit, including those in decisions already marked amended -- T-069 found
  its leftovers inside decisions the original sweep had rewritten.
- **"span" names the fort type `T@`** (D3.5), so a byte or source extent is a **range** everywhere
  in the compiler: `str_from_range`, token byte ranges, `ASSERT_TOK_RANGE`, and `loc_t` ranges in
  the editor-support work. Never call an extent a span.
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
- **An emitter that copies a value and then clears its source needs an intermediate.** `move(lv)`
  writes into a destination the emitter cannot prove distinct from the operand (`s = move(s)`,
  `*p = move(*q)`, `v[i] = move(v[j])`), so it reads into a register or a `%tmpK` slot first and
  zeroes the operand only after. The first version copied straight to the destination and zeroed
  after, which destroyed the value in three of six build-mode/shape cells and was invisible to
  every language test, because the two aliasing forms it did not cover are not statically
  comparable. Fix the general case rather than banning the syntax that exposes it.
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
- **A `fail` test may not mix a lexical with a semantic diagnostic.** `lex_file` reports, and the
  driver then stops before the checker runs (D14.2), so a file whose lexical error is annotated
  alongside an expected type error never produces the second one and the run fails on the missing
  annotation. Split them into two files. Found by T-029 while filling the `fail/lexer` gaps.
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
  **A runtime entry point is described in three places, and a test now holds all three together**:
  the C prototype in `runtime/fort_rt.h` and `.c`, the table in `toolchain.md` 5.1 with the
  declaration list of item 8, and one row per entry point in `src/bootstrap/runtime_sig.c`
  (`RT_SIG`, indexed by the `rt_entry_t` of `runtime_sig.h`), which carries the C name, the result
  form, the `_Noreturn` mark and the parameter forms. The emitter renders its `declare` lines and
  its call-site result types from that row, and the checker holds an `extern fn` naming an entry
  point against the same row (D9.8, T-072), so the IR and the diagnostic cannot disagree. The
  witness is `test/runtime_sig_test.c`'s `RT_ENTRIES` X-macro: each row is expanded once into a
  `_Static_assert(_Generic(&name, cresult (*) cparams: 1, default: 0), #name)`, which pins the row's
  C column to the real header, and once into runtime assertions that every form of `RT_SIG` is
  `IR_OF` of the C type beside it, which pins the table to that column. Both halves are needed --
  T-072's review showed that the `_Generic` assertion alone witnesses the header against the
  assertion's own text and would miss a wrong row -- and the per-parameter half must be runtime
  assertions, since `rt_entry_param` is a function call. `IR_OF`'s `default:` is `IR_NONE` and no
  row may hold one: a `default: IR_PTR` would silently swallow a future scalar, C's `char` being a
  type distinct from `signed char` and `unsigned char` under `_Generic`. The one column no construct
  can witness is `_Noreturn`, which is a function specifier and not part of the function's type (C11
  6.7.4), so `_Generic`, `__builtin_types_compatible_p` and everything else are blind to it; it is
  checked by reading 5.1. Before that witness existed, giving `fort_rt_print_f64` an `i64` parameter
  or `fort_rt_fail_div_zero` a 64-bit line number left the whole gate green. A narrow result carries
  its extension attribute in the declaration *and* at the call site (`declare zeroext i8
  @fort_rt_str_eq(...)`, `%t = call zeroext i8 @...`), which is why a form in `RT_SIG` is a type
  text with its attribute and not a type. `opt` accepts a call site whose attributes differ from the
  callee's and LLVM falls back to the callee's, so *dropping* one at a call site cannot change the
  assumption while *adding* one the declaration lacks can: T-021's review dropped the `zeroext` from
  the `fort_rt_str_eq` call site and the entire language corpus stayed green, only the emitted-text
  assertion failing. The attribute is not decorative -- on a return it licenses eliding the `movzbl`
  -- and it stops being invisible the moment a lowering compares or widens the narrow result instead
  of truncating it straight to `i1`.
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
  `call void @fort_rt_fail_div_zero(ptr @.file.0, i32 4, i32 14)` against
  `declare void @fort_rt_fail_div_zero(ptr, i64, i32)` verifies, links and then reads a register
  the caller never set (T-072's review). Together with the attribute fact above and the terminator
  fact below, this is why the emitter suites are a weak oracle for a *declaration*: they check the
  text they assert, and `only_referenced_declarations_are_emitted` means most runtime declarations
  appear in no gen test at all. A declaration is pinned against the C header that defines it, not
  against the IR a tool accepts.
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
  **Under opaque pointers a field's type in a named struct type is observable only through the
  offsets it moves.** A substitution that leaves every later offset and the struct's size and
  alignment unchanged is invisible to `opt`, to every run test and to C interop: same-size swaps
  (`i32` for an enum, `i64` for a `ptr`), and any widening that fits in padding the field already
  had (`u16` as `i32` or as `i64` in `{ u8; i32; u16; i64 }`). So exactly one assertion pins a
  field's IR type, the string comparison on `%struct.<name> = type { ... }`, and a struct that
  appears in no such assertion has its field types unchecked. The bootstrap cannot *produce* that
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
- **A whole directory in `xfail.txt` hides a class of programs from every pass behind it.** Both
  bugs the deep review of T-015 found were at a module boundary, because `run/modules/` is
  entirely expected to fail, so no program with two modules had ever reached the emitter: an
  `extern` declared in each of two modules was emitted twice, and nothing else crossed a module
  at all. A ticket that adds a pass reads `xfail.txt` for the directories its pass now walks and
  writes one test per class they cover, rather than trusting the corpus it can see.
- **Citing decisions in code**: a citation goes on the line or function that implements the
  rule, with a phrase stating the rule (`// pointers print as 0x + lowercase hex, 0x0 for null
  (D11.7)`), so a reader learns the rule without opening the log. A bare tag list at file or
  section level (`// Printing (D11.5, D11.7, D12.2).`) is not a citation.
- **Writing specification text**: cite the decision each rule implements as `(Dn.m)`. An agent
  that needs a rule the decision log does not settle uses the most conservative reading, marks it,
  and reports it to the lead for ratification; it never invents syntax or semantics. Every
  amendment to `notes/decisions.md` is relayed to agents still writing against the old text, and
  a separate audit pass reconciles the documents afterwards.
- **C sources**: C11 (`-std=c11`, `_POSIX_C_SOURCE=200809L`), no third-party code, warnings are
  errors under both clang (default) and gcc (`gcc` preset). `_POSIX_C_SOURCE` alone does not
  make glibc declare `environ`: `<unistd.h>` guards it with `#ifdef __USE_GNU`, so a file that
  passes an environment to `posix_spawn` declares `extern char** environ;` itself, as POSIX
  allows. `realpath` is the same case (`<stdlib.h>` guards it with `__USE_XOPEN_EXTENDED`, which
  `_POSIX_C_SOURCE` does not set), and `src/bootstrap/modules.c` declares it the same way;
  check for that guard before calling any POSIX function the headers seem to be missing, rather
  than widening the feature macros. Names: functions, variables, parameters, fields and
  struct/union/enum tags lower_case;
  typedefs lower_case with a `_t` suffix; enum constants, file-scope constants (static or not),
  function-scope static constants and macros UPPER_CASE (`enum { BYTE_MASK = 0xFFU }`); local
  constants lower_case; macros private to a header end with an underscore (`TEST_LOG_`). Every
  non-void call result is used or discarded with `(void)` (`TEST_UNUSED` in tests); no magic
  numbers (0 to 4, powers of two, `1.0` and `100.0` are allowed); uppercase literal suffixes;
  comments are `//` only, never `/* */`, as in fort (D2.2), so a region is commented out line by
  line; includes grouped as the file's own header, `<x.h>`, `<sys/x.h>`, project `"x.h"`, then
  `"test.h"`/`"common.h"`.
  `.clang-format` and `.clang-tidy` (clang 18) are the reference, and clang 18 in the guest is the
  only authority: the host's clang is newer and its editor diagnostics report checks the gate does
  not, so a warning that appears in an IDE and nowhere in `tools/vm tidy` is a version difference
  and not a finding. Seen with `bugprone-narrowing-conversions` on an `int`-to-`char` ternary in
  `test/lang/ffi/layout.c`, flagged on the host and silent under `clang-tidy-18` in the guest, where
  `WarningsAsErrors: '*'` means a real one would have failed the gate. Check in the guest before
  acting on an editor's warning, and never edit code to satisfy a check the project does not run.
  `tools/vm format` reformats,
  and `tools/check_comments.py` rejects a block comment (`tools/vm format-check` runs it; it
  skips a `/*` inside a string literal, a character literal or a `//` comment). This applies to
  test helpers under `test/` too. Two gaps of clang-tidy 18 are covered by review:
  `bugprone-unused-return-value` takes function names, not patterns (patterns arrive in
  clang-tidy 19), so `.clang-tidy` lists the C library and POSIX functions and the project's own
  functions are unchecked; and **clang-tidy 18's macro-argument blind spot covers
  `readability-identifier-naming` as well as `readability-magic-numbers`**, so nothing inside a
  `TEST(name, { ... })` body is checked for either -- a `static const char program[]` there draws
  no `invalid case style for static constant` while the same declaration at ordinary source
  location does. An experiment that renames an identifier *inside* a `TEST` body and sees no
  complaint has measured nothing; move the declaration out, or read the convention off a
  comparable one in a `main` (`SOURCE` in `test/gen_control_test.c`).
  The bootstrap must also stay transliterable into fort: no unions, no macro tricks, and no
  compiler builtin fort lacks (function-pointer tables are fine, the bootstrap subset has
  function pointers). `__builtin_clzll` was removed for that reason: `mag > (UINT64_MAX >> n)`
  says the same thing. The whole list, and what replaces each construct, is **Transliterating the
  bootstrap into fort** below; consult it before writing a C file the port will have to carry.
- **fort sources**: identifier conventions per decision D1.4: everything is lower_case with
  underscores, struct and enum type names and enum members included; only module constants are
  UPPER_CASE. A variable never takes its type's name (`point p`, `box bx`, `list mut* mut l`); a
  field may (`node mut* own node`), since fields are not variables and are outside the module
  namespace (D7.9). `tools/fort_lint.py` enforces exactly that, and the 100-column wrap, over
  `std/` and `src/fort/`; nothing formats `.ft`, so indentation and spacing are still written by
  hand and read by review. Run it with `tools/vm fort-lint`, or by hand as
  `tools/vm run 'python3 tools/fort_lint.py --fort build/debug/fort <file.ft>'`.
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
  not in-process recovery: `fort_rt_panic` aborts like every other failure (D11.4), so a server that
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
  `f64`, float literals), no multi-dimensional arrays, no `do { } while` and no `?:`. Function
  pointers are inside the subset (D3.10), so a dispatch table is fine. These are the constructs a
  C file may hold that have no fort spelling, with what replaces each; the rules the bootstrap
  already follows so that it stays portable are the first four.
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
    so every C prototype the file carried for ordering disappears.
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
  Nine more facts the first ports paid for: five from T-032 (`prim.ft`, `consts.ft`,
  `types.ft`) -- keywords, `new(T, n)`, `==`, enum ordering, the forked tests -- and four from
  T-033 (`ast.ft`, `parser.ft`, `test/fort/support/parse_env.ft`) -- mutually recursive structs,
  joining strings, the NULL-for-no-message parameter, and the fixture's token vector.
  - **Keywords take the names first.** `type`, `const`, `match` and the rest of D2.4's reserved
    list, and every type keyword, are not identifiers, so `type_t` cannot be `type`, a field
    cannot be `mut`, `own` or `noreturn`, and an enum member cannot be `i8`, `bool` or `null`.
    The port keeps the C function names verbatim (`types.type_ptr`, `consts.cv_add`,
    `prim.prim_is_integer`), stutter and all, because the module answers to its C original name
    by name; it renames a field to `is_mut`/`is_own`/`is_noreturn` and gives an enum member the
    C constant's prefix (`cv_int` for `CV_INT`), or a short one where the C name is already a
    function's (`k_ptr` for `TYPE_PTR`, beside the constructor `type_ptr`).
  - **`new(T, n)` gives its result `mut` at every level** (D5.8), so `new(node*, n)` is
    `node mut* mut@ own` and storing it in a `node* mut@ own` field is refused: dropping the
    pointee's `mut` behind a mutable span is D5.4's `T** -> const T**` hole. A `cast` is the
    sanctioned escape and the only one; write it once, at the allocation, with the reason.
  - **`==` does not drop `mut`.** Operands lend `own` (D17.4) and nothing else, so comparing a
    `node mut*` with a `node*` is a type error: give the test a `node*` binding rather than
    casting.
  - **Two mutually recursive structs must be declared with the one that holds the other by value
    first.** stage1 lays a struct out in declaration order and resolves a field's struct even
    behind a pointer, so `struct vec { node mut* mut@ own items; }` before `struct node { vec
    list; }` is `struct vec has infinite size`, while the same two in the other order compile
    (T-033, `src/fort/ast.ft`). The bug is stage1's; the port works around it by ordering the
    declarations, which costs nothing, and a comment at the site says why.
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
  error in the compiler itself (D2.2). A new fort source outside `std/` and `src/fort/` is checked
  by nothing until a glob in `fort_lint.py` names it. One `fort --index` per file re-checks that
  file's whole import closure, so linting *n* modules costs O(n^2) checker work under each of the
  gate's three presets: 0.1 s for the eight `std/` modules, and worth rewriting as one run per
  root entry (`--index` indexes the closure and `same_file` already attributes each record) before
  `src/fort/` holds forty of them.
- A new `std/*.ft` reaches the language harness only after `tools/vm build <preset>` copies it
  into `build/<preset>/std`, next to `fort_rt.o`: running `run_tests.py` by hand against a source
  that has not been copied reports `module 'std.x' not found`. `test/lang/run/stdlib` is where a
  library module is tested.
- **Writing a library module against C** (`stdlib.md` 1.4, D13.4): a span is not a pointer, so
  `cast(u8 mut@ own, void* own)` is rejected (D3.14 lists pointer-to-pointer, not span-to-pointer).
  The `libc.free(cast(move(p), void* own))` idiom of `stdlib.md` 2.2 therefore applies to a
  `T mut* own` from `new(T)` or `libc.malloc`; a `T mut@ own` from `new(T, n)` is released with
  `del`, and what crosses to C is its `.ptr`, a view.
- **Shell scripts**: bash with `set -eu`, clean under shellcheck at its default severity; the
  host has no shellcheck, run it in the guest: `tools/vm run 'shellcheck tools/vm
  tools/provision.sh'`.
- **Commit messages**: a title of about 50 characters (72 at most), a blank line, then a body
  wrapped at 72 columns that says what changed and why, then a single `Co-Authored-By:` trailer.
  No `Claude-Session:` trailer: the session URL is useless to anyone reading the history later
  and it is the only line in a commit that no reader can act on.

## Mandatory Process Rules
The following rules MUST be followed by each process/agent for each change being made. There are no
exceptions.

### Lifecycle of a Change
#### Codebase Exploration
Each process/agent MUST explore the relevant portions of the codebase as indicated by the task at
hand.

#### Worktree Isolation
Each Claude process/agent MUST work in a separate git worktree and associated branch. Create the
worktree as a directory (`fort-<name>`) in `.worktrees`, and prefix the branch name with `bug/`,
`feat/`, etc. as you see appropriate. The coordinator deletes the worktree once the change is merged
into the target branch.

#### Change Implementation Loop
Always implement a change in small incremental commits. A commit MUST be composed of a
self-contained unit of logic that improves the overall system. No commit MUST break _any_ test in
the repository. Before committing a change to `git`, make sure all tests pertinent to the component
you are working on run successfully, and make sure that the code format and lint checks pass. Rebase
on top of `main` frequently to reduce the chances of merge conflicts.

Once done with a change that is a single unit of work, squash all commits on the branch into one
via interactive rebase (`git rebase -i origin/main`, mark all but the first as `squash`). Write a
meaningful commit message that describes _what_ and _why_ -- do not just collate the individual
commit messages. Agents that cannot run an interactive rebase use the equivalent
`git reset --soft $(git merge-base main HEAD) && git commit` (not `git reset --soft main`:
if `main` moved since the branch was cut, that commits the branch's old tree on top of the
new `main` and silently reverts its newer commits). A feature branch made of several
self-contained units of work (for example the language design, or a compiler pass plus its tests
plus its documentation) keeps its individual commits and is merged into `main` with a merge commit
(`git merge --no-ff`) whose message describes the whole feature. Until a remote exists, `main`
plays the role of `origin/main`. Worktrees live in `.worktrees/`, which is gitignored.

### Tickets
- One markdown ticket per deliverable in `.tickets/` in the main checkout, never in a worktree;
  state is the directory: `todo/`, `inprogress/`, `done/`. Template and numbering rule in
  `.tickets/README.md`; fields: id, title, size, critical-path, depends-on, impl, review,
  deliverable, spec, branch, worktree, assignee, acceptance criteria (checkboxes), notes, log.
- A ticket is assigned only when every ticket in its `depends-on` is in `done/`. Independent
  tickets are assigned concurrently, one implementor each.
- Acceptance criteria are verifiable inside the VM; the log records every hand-off with its
  evidence (commands run, results, review rounds, merge sha). Evidence must outlive the agent that
  produced it: a command anyone can re-run, a commit sha, a file in the repository. A criterion
  ticked against "the report" is ticked against prose that exists nowhere once the agent returns,
  and nobody can ever re-check it -- T-069's audit of all 86 amended decisions is gone for exactly
  that reason, leaving only the findings that reached a commit message. An agent whose deliverable
  is an analysis rather than code writes it into the ticket or into `notes/`, and cites that.
- **A criterion that claims "every" must be backed by a count, not by a representative case.** Three
  tickets have ticked a universal they had not measured: T-020 and T-023 each claimed every emitter
  test called `verified()` when 23 of 50 and 9 of 25 did not, and T-073 claimed three guards were
  mutation-proved when two of the sites were held only by a compiler warning outside the gate. In
  every case the implementor had checked one instance and generalised, and in every case a reviewer
  found it by counting. So: write the count and the command that produced it into the log
  (`grep -c 'verified()' test/gen_*_test.c` against the number of tests, and so on), and phrase the
  criterion as the number rather than as "every". A criterion that overstates is worse than one that
  admits a gap, because it stops the next reader looking.
- Every ticket meets the 3:1 test-to-code ratio on its own diff, not on the repository average:
  `tools/vm run 'python3 tools/lines.py --since main --min 3.0'` is an acceptance criterion of every
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
  number.
- The ratio is a prompt, not a verdict: what a review asks is which rules of the decisions a ticket
  cites have no test at all, unit or language, and the answer decides the ticket. T-014 measured
  0.76 and merged, because the number could not see that it took 57 entries out of xfail.txt -- a
  body of language tests that already existed and only then began to exercise the code, adding not a
  line to the diff -- while the coverage question found three rules that were untested and also
  broken (a noreturn function type that never matched, an unchecked shift count that made the two
  build modes disagree, and an index expression that handed out a pointer into a dead temporary).
  Ask a reviewer for the list of untested rules whenever a ticket misses the ratio, and merge or
  refuse on that list.

### Agents
- The coordinator is the main session. Every other role is an agent definition in
  `.claude/agents/`, all on Opus and differing only in reasoning effort, because effort is fixed
  per definition and cannot be overridden per call.
- Implementors (full tools, no `Agent` tool): `impl-mech` (medium) for work the specification
  pins completely, transcription and coverage; `impl-std` (high) for ordinary tickets that need
  data-structure design; `impl-hard` (xhigh) for cross-cutting invariants, the calling
  convention, memory layout, ownership and codegen; `impl-port` (medium) for transliterating a
  tested C module into fort against an oracle.
- Reviewers (read-only, the `code-review` skill): `rev-quick` (medium, skill at low) screens a
  low-risk diff for conventions, tests and scope; `rev-std` (high, skill at high) reviews an
  ordinary change; `rev-deep` (xhigh, skill at max) re-derives the invariants independently for
  ABI, memory, ownership, exact arithmetic, unsafe casts and generated code.
- Each ticket names its tiers in its `impl:` and `review:` fields. Review depth follows the risk
  of the change, not the effort it took to write: a ticket can be `impl-std` and `rev-deep`.
- A tier is a default, not a verdict. If a ticket run at `impl-mech` or `impl-port` fails the
  gate twice or comes back with a must-fix finding, the coordinator re-runs it one tier up and
  records the promotion in the ticket log; two promotions out of one tier means the mapping is
  wrong, so change the tickets' `impl:` field rather than promoting case by case.
- Agent definitions in `.claude/agents/` are loaded when a session starts; restart the session
  after adding or changing one.

### Review Workflow
- Coordinator: picks a ticket whose dependencies are done, creates the worktree and branch
  (`.worktrees/fort-<id>`, `feat/<id>-<slug>`), fills branch/worktree/assignee, moves the ticket
  to `inprogress/`, spawns the implementor tier the ticket's `impl:` field names, with the ticket
  path and the worktree.
- Implementor: reads the ticket and only the specification sections it cites; codes and tests in
  the worktree with small commits, each green under `tools/vm check`; runs the full `tools/vm
  gate` once, at the end; squashes if the ticket is a single unit of work; ticks every criterion
  with its evidence; moves the ticket to `done/`; reports in at most 40 lines. It does not spawn
  a reviewer.
- Coordinator: spawns the reviewer tier the ticket's `review:` field names (raising it when the
  diff turned out riskier than the ticket looked), and relays the findings to the implementor,
  which fixes or explicitly declines each one in the ticket log. A second review round happens
  only when the fixes changed behaviour.
- Reviewer: read-only; runs the `code-review` skill on the branch against `main` at its tier's
  effort; also checks spec citations, tests added, `xfail.txt` updates, AGENTS.md updates and
  commit hygiene; returns findings with file:line and severity; never edits, commits or merges.
- Coordinator: re-runs the gate on the branch, **reads the diff's file list**, merges per the
  Change Implementation Loop (squash for a single unit, `--no-ff` for a multi-unit feature),
  deletes the worktree and branch, appends the merge sha and the agents' token counts to the
  ticket log, and assigns the tickets it unblocked. The file list is a separate job from the gate,
  which has no opinion about a file that should not exist: T-022 merged five scratch `.ft` probes
  into the repository root behind a green gate, and the review that called its scope clean had read
  the commit before the fix round that added them. `git diff --stat main...HEAD` before every
  merge, and look for what is new rather than what changed.

### Self-Updating Context (AGENTS.md Auto-Amendment)
AGENTS.md MUST be amended whenever a learning or course correction occurs. This applies in two
cases:
- **Autonomous**: When any process/agent discovers something important during development (e.g., a
  new convention, a gotcha, a pattern that works or fails), they MUST update the relevant section of
  AGENTS.md.
- **User-directed**: When the user gives an instruction that changes how the project works (e.g.,
  new tooling, changed workflow, updated conventions), the receiving agent MUST update AGENTS.md to
  reflect the change immediately.



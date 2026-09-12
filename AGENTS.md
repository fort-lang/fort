# fort
A safe(r) C-like systems programming language.

## Writing: ASD-STE100 everywhere
- **Write in ASD-STE100**, the ASD Simplified Technical English specification. Name it in
  full: the standard is ASD-STE100, not "ASD-STE". The user asked for this on 2026-09-11 and
  then said "use it everywhere", so it governs **all** the text this project writes: replies to
  the user, code comments, commit messages, ticket text, the prompts that go to agents, and the
  specification documents in `notes/`. There is no exempt category.
- The rules:
  - Write short sentences. Put one idea in each. Keep an instruction to 20 words and a
    description to 25.
  - Use the active voice. Say who does the action: "the agent refused the instruction", not
    "the instruction was refused".
  - Use the simple present where it works.
  - Give a word one meaning, and use the same word for that thing every time. A gate is a gate.
    Do not call it "the check" in the next sentence.
  - Use plain words: *use*, not *utilise*; *start*, not *initiate*; *before*, not *prior to*;
    *about*, not *approximately*.
  - Do not use idiom or metaphor. "The test went red" is jargon this project keeps, because it
    names one thing exactly; "bitten by", "load-bearing" and "throat-clearing" are not.
  - Cut the opening noise: "it is worth noting that", "as we can see", "interestingly".
  - Write a number, a command or a file path instead of an adjective. "The gate exited 1" says
    more than "the gate had a problem". "395 of 666 files" says more than "most files".
- **Exactness wins over simplicity.** A rule in `notes/` must stay exact. If a short sentence
  would change what a decision means, write two short sentences. Do not drop a condition to make
  a sentence shorter.
- The rule applies to text you write or change. It does not ask anyone to rewrite the documents
  that are already there; that sweep is its own ticket if the user wants it.

## Naming
- The language is `fort`. Source files use the extension `.ft`. The compiler binary is `fort`.

## Project Layout
- `notes/`: the language specification and the engineering knowledge, indexed by
  `notes/README.md`. `notes/decisions.md` (numbered decision log) and `notes/grammar.md` are
  normative and win over every other document. Start at `notes/project-overview.md`.
  `notes/environment.md`, `notes/testing.md`, `notes/compiler.md` and `notes/style.md` take the
  facts the routing rule at the end of this file sends them.
- `test/`: `test/test.h` is the C macro framework for the compiler's unit tests
  (`test/common.h` provides `TEST_UNUSED`); `test/lang/` holds language tests in the directive
  format defined in `notes/toolchain.md`.
- `src/bootstrap/`: the C bootstrap compiler (stage1), frozen once the compiler is self-hosted.
  `src/fort/`: the compiler written in fort (stage2 and stage3). `std/`: the standard library in
  fort, the runtime (`std/rt.ft`) among its modules; there is no C runtime and no object linked
  beside the program (T-091). `tools/`: `vm`, `provision.sh`, `lines.py`, `bootstrap.sh`.
- `editors/`: `editors/vscode/` is the VS Code extension -- `package.json`,
  `language-configuration.json`, `syntaxes/fort.tmLanguage.json`, `extension.js`, and the one pure
  module it is tested through, `lib/check.js` -- and `editors/README.md` is its install guide, its
  manual smoke test and its list of limitations. It highlights fort and shows the compiler's
  diagnostics, and that is all it does (T-089): no hover, no go-to-definition, no `--index`, no
  cache, no settings. It is installed on the **host**, where VS Code runs.
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
- **Give a ticket its own VM.** `$FORT_VM_DIR` selects the VM directory and the VirtualBox machine
  is named `fort-dev-<directory name>`, so a worktree that exports `FORT_VM_DIR="$PWD"` gets a
  machine of its own that cannot collide with the main checkout's. Measured on 2026-09-12, on a
  host with 10 CPUs and 32 GiB: `FORT_VM_DIR="$PWD" FORT_VM_CPUS=4 FORT_VM_MEMORY=8192
  tools/vm up` creates, provisions and boots in **99 s**, and one preset from cold -- configure,
  build and ctest -- takes **6 m 22 s** on 4 CPUs, so a three-preset gate is about 19 minutes.
  A shared 6-CPU VM ran the same gate in 22 to 30 minutes **because four agents were queuing on
  it**. So a dedicated smaller machine is both faster and predictable, and provisioning is cheap
  enough to do per ticket.
  Two VMs at 4 CPUs and 8 GiB leave the host 2 CPUs and 16 GiB. Three at 3 CPUs fit the arithmetic
  and are the wrong shape: `test/fort/driver_lifetime_test.ft` is one qemu program that takes 63 s
  of a 60 s budget, the harness runs `-j 6`, and a 3-CPU machine oversubscribes and can time the
  probe out with no other agent present.
- **A worktree chooses its VM before it configures, and cannot change its mind cheaply.**
  `/vagrant` is the main checkout on the shared machine and the worktree root on its own, so every
  absolute path in the CMake cache is bound to that choice. Switching later means deleting
  `build/` and rebuilding from scratch. Free for a new worktree; wasteful for one mid-ticket.
- **What one shared VM cost on 2026-09-12**, so the trade is on the record: four agents gating at
  once drove the load to 19 on 6 CPUs, produced two false red gates that each cost an hour of
  diagnosis, and made one agent run `pkill -f ctest` in a machine three other worktrees were
  using. Every one of those is a contention failure and not a code failure.
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
  long` -- indistinguishable, to a caller, from a VM that is down. Anything that multiplexes ssh
  from the host must measure the path and fall back to `/tmp/<something short>`. Nothing in the
  repository does any more: T-089 deleted the extension's own transport, and `tools/vm` is now the
  only thing that crosses into the guest.
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
  (`build/<preset>/fort`), `fort_std` (a copy of `std/*.ft` in `build/<preset>/std`, which is
  what the compiler reads as `--std-dir`; `FORT_TARGET_CC`, a clang (default `clang`) with
  `--target=${FORT_TARGET_TRIPLE}` (default `x86_64-linux-gnu`), is what the driver runs over the
  emitted module), `lang_ffi_helpers` (`test/lang/ffi/*.c` built natively so `-Werror` and
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
  already there; **`--since` reads the commits, not the working tree**, so a file that is only
  written or only staged counts as 0 lines and the ratio answers about the last commit: commit
  first, then measure (T-093); its own tests are the ctest `lines_selftest`).
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
  **Never kill a guest process by pattern.** The VM is shared by every worktree, so
  `tools/vm run 'pkill -f ctest'` or `pkill -f run_tests.py` ends the runs of the other agents as
  well, and each of them reads the kill as a test failure in their own branch. T-043 did it to
  stop its own gate and had to report the damage it could not undo. To stop a run of your own,
  kill the host process you started (`pkill -f 'tools/vm gate'` matches only host shells, and even
  that matches another agent's monitor loop, so prefer the pid the shell gave you); a guest
  command then dies with its ssh session.
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
- **A `test/fort` suite that checks two sources must reopen its environment between them.** A
  module set answers a path it has already loaded from the tree that load left, so a second
  `check_env.check_src` over one environment silently re-checks the first source and its
  assertions then pass or fail for the wrong reason; the first sink still holds the first check's
  diagnostics as well. `check_env.reopen` is `test/check_helpers.h`'s `begin()` and goes between
  the assertions about one source and the next check. The C helpers reset per check, so a
  translated suite that drops the reset is the failure mode to look for.
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
- The TextMate grammar is checked by `test/highlight_test.py` (ctest `highlight_selftest`, label
  `unit`, run from `test/`): it reads the D2.4 keyword lists and the D2.10 operator list out of
  `notes/decisions.md` and the same sets out of the grammar, so the two cannot drift. That only
  works while the rules keep their canonical shapes, `\b(?:a|b)\b` for keywords and `(?:\+|-)`
  for operators; a rule whose scope is in a keyword or operator family but whose pattern matches
  neither shape fails the test. Its other half is a small TextMate engine that asserts the scopes
  of `test/highlight/scopes.ft` (`//^` lines: alternating text and scope fields naming what the
  line above must produce), of the D5.3 and D17.2 marker tables, and of **every fort source the
  project writes**, which must tokenize with no `invalid.` scope and no unscoped character, so a
  new file the grammar mishandles fails here. `CORPUS_DIRS` is that list -- `test/lang/run`,
  `test/lang/programs`, `std`, `src/fort`, `test/fort` (its `support/` included) and
  `test/fort_lint` -- and `CORPUS_FILES` is the exact number of files in it, so a ticket that adds
  or removes a `.ft` under any of them reads the new number off the failure and writes it there,
  as it does for `CORPUS_FILES` in `test/parser_recovery_test.c` and `FT_FILES` in
  `tools/diff_tokens.sh`. The other `.ft` of the repository are listed in `EXCLUDED_DIRS`, each
  because it is meant to hold a lexical error (`test/lang/fail`, `test/highlight/scopes.ft`,
  `editors/vscode/test/fixtures/lexical.ft`), and a test asserts that partition, so a new
  directory of fort is a red test rather than a corpus nobody tokenizes -- which is what
  `test/fort` and `test/lang/programs` both were until T-079 measured it.
- The VS Code extension's sources are plain JavaScript wrapped at 100 columns, and no gate target
  lints them, so the conventions are here: `'use strict'` at the top of every file, CommonJS
  (`require`/`module.exports`, no ESM and no bundler), `//` comments only as in C and fort (D2.2),
  two-space indentation, single quotes, semicolons, `const` unless a binding is reassigned, no npm
  dependency and no devDependency, and no API beyond Node's standard library and `vscode` (which
  only `extension.js` may require). A file is tested by `node --test` or it is `extension.js`.
  `test/package.test.js` asserts the last two by reading the sources, so a second
  `require('vscode')` or a second module under `lib/` is a red test.
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
- **`tools/vm run` is how anything on the host reaches the guest, and that includes the editor.**
  VS Code runs on the host, there is no `~/.vscode-server` in the guest, and the extension crosses
  by spawning `<workspace>/tools/vm run '<command>'` with its working directory set to the
  workspace folder: `tools/vm run` resolves the VM directory and the cached ssh configuration
  itself and runs the command in the guest directory matching the host's. It costs about 0.1 s
  (five samples of a real check over this repository: 0.09, 0.13, 0.10, 0.10, 0.09 s), which is
  well inside a save. The path needs no host-to-guest mapping in either direction, because the
  compiler names each file the path it opened it by, the entry file as given on the command line
  (`toolchain.md` 2, D14.2): pass a path relative to the workspace folder and the document names it
  the same way, so it resolves against that folder on the host. That is the premise the whole
  crossing rests on, and `a_relative_entry_is_named_in_the_document_exactly_as_it_was_given` in
  `test/driver_check_test.c` is what pins it: an absolute path there would put every record outside
  the workspace and the extension would go **silent** rather than wrong, which is the worst failure
  shape an editor has. What comes back absolute is what the compiler found for itself -- the
  standard library under `/vagrant/build/release/std` -- and those files are the guest's, so an
  editor drops them rather than painting a path the host cannot open. The argument of `run` is
  handed to a shell in the guest, so a path is quoted before it goes in.
- The cross pipeline: `test/ir/*.ll` are hand-written LLVM 18 modules in the form
  `notes/toolchain.md` 6 specifies (D19.1); `hello.ll` and `abort.ll` are its two worked
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
- **A system call added to a print path must give errno back.** `sys.errno()` hands a program the
  errno of its own last library call (`stdlib.md` 2.4), and the print family runs between the two:
  the `isatty` of D11.5 fails with `ENOTTY` on every pipe, so the first `print` after a failed
  `open` replaced the program's `ENOENT` with it and `run/stdlib/053_io_open_errors.ft` printed
  `-1 false`. That test found it because the arguments of one `println` are evaluated left to
  right (D11.7), which puts the buffer's creation before the `sys.errno()` beside it. The runtime
  saves and restores errno around the call; anything else it grows on that path does the same.
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
- Binaries: `build/<preset>/fort` is stage1 (the C compiler); `build/<preset>/stage2/fort` is the
  self-hosted compiler, which stage1 builds from `src/fort`. The `fort_stage2` target builds it at
  every build (stage1 over `src/fort/main.ft`, whose imports pull the rest of `src/fort` in), so a
  module stage1 rejects fails the build rather than the test run; stage2 is an x86-64 binary and
  runs under qemu like every program the compiler builds.
  `tools/bootstrap.sh [--preset <preset>] [--stage3]` builds stage1 and that stage2 by hand in the
  guest. With `--stage3` it builds stage1 and hands the fixed point to `tools/fixpoint.sh`, and it
  writes no stage2 of its own, since that script builds one per build mode.
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
- Two corpora beside `test/lang` run through the same `run_tests.py`, which takes the corpus root
  as `--root`: ctest `lang-stage2` (label `lang`) holds the language corpus against stage2 with
  `--xfail test/lang/xfail-stage2.txt`, which started as the whole corpus (`run/`, `fail/`,
  `programs/`) and is empty as of T-038, stage2 passing every test of it;
  ctest `fort-modules` (label `lang`) runs
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
  directory is invisible to the harness: `discover` walks `run`, `fail`, `programs` and the
  `*_test.ft` of the root and nothing else, so a test misfiled there would run nowhere and say
  nothing. `_report_misplaced_tests` closes that (T-079): a `*_test.ft` anywhere below the root
  outside those three directories is `test outside the root of the corpus`, which is a lint
  problem and not a test, since what belongs under `support/` is shared code and nothing else.
  `tools/lines.py` counts `test/fort/**/*.ft` as test lines and `src/fort/*.ft` as compiler lines,
  `test/highlight_test.py` tokenizes them, and `tools/fort_lint.py` lints them with the search
  roots its `SOURCE_SETS` table pairs with the glob (`-I src/fort -I test/fort/support`, the
  `-I ../../src/fort -I support` of the directives spelled from the repository root); a file named
  on its command line takes the roots of its own `-I` options. Without them `fort --index` reports
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
  (`test/fort/driver_lifetime_test.ft`), since forty other tests in the same program fragment the
  heap for reasons that are not leaks.
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
  otherwise pass while seeing less.
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
- **A whole directory in `xfail.txt` hides a class of programs from every pass behind it.** Both
  bugs the deep review of T-015 found were at a module boundary, because `run/modules/` is
  entirely expected to fail, so no program with two modules had ever reached the emitter: an
  `extern` declared in each of two modules was emitted twice, and nothing else crossed a module
  at all. A ticket that adds a pass reads `xfail.txt` for the directories its pass now walks and
  writes one test per class they cover, rather than trusting the corpus it can see.
- **The runtime is `std.rt` and every closure holds it** (D9.10, D13.1, T-091). Three consequences
  a ticket meets before it meets anything else. A module set with a standard library directory
  loads `<std-dir>/rt.ft` as a root **before** the entry file, so `std.libc` rides in behind it
  and D9.8 holds a program's own `extern` declaration of a libc symbol against the library's,
  `own` included: `run/ownership/015` and `019` gained the `own` `std.libc` carries. A set with no
  such directory loads no runtime, which is what every in-process unit suite is, so a suite that
  drives the whole driver writes an **empty** `std/rt.ft` in its sandbox (`test/driver_helpers.h`,
  `test/modules_helpers.h`, `test/fort/support/modules_env.ft` and the three `test/fort/driver*`
  suites) rather than the real one: the driver needs a file that parses and the assertions stay
  short. And the `"files"` of D20.2 and the `"symbols"` of D20.3 now hold the library's records
  too, whose file names are the `--std-dir` the run was given, so `run_tests.py`'s golden index
  compares the records of the test's own directory alone (`index_of_the_test`) -- a byte-for-byte
  golden of the whole index would name a build directory and could not be checked out on another
  machine.
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
- **Citing decisions in code**: **superseded by `notes/style.md` 1, the comment policy (T-098).**
  The old rule asked for "a phrase stating the rule" (`// pointers print as 0x + lowercase hex,
  0x0 for null (D11.7)`) and refused a bare tag list; it produced 2962 tagged comment lines in
  `src/`, of which 0 match the shape in force now. T-101 rewrites them. The rule in force: a
  citation is a tag on the line or above the line it governs, `// D17.5`, and it takes one clause
  after a colon only where the tag alone leaves the rule unclear, `// D17.5: an own lvalue moves`.
  Write every new comment to `notes/style.md` 1.
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
  **`src/fort` stays inside that subset until T-046**, the ticket that freezes stage1. stage1
  compiles stage2 at every build and the ctest `bootstrap` compiles it twice more, so a `src/fort`
  file that uses a construct stage1 lacks breaks the build and the fixed point on the same
  commit. A feature leaves `test/lang/bootstrap-unsupported.txt` when stage1 gains it, which it
  never will now, and leaves `test/lang/unsupported-stage2.txt` when stage2 gains it. A compiler
  that accepts a construct its own source may not hold is `notes/compiler.md` 8 (T-041, floats).
  **`src/lsp` is under no such rule**: stage2 compiles it, so it may use anything `src/fort`
  implements (D20.5). T-039 and not T-046 is the gate for the language server, because the server
  needs a self-hosted compiler that reproduces itself and not the frozen bootstrap. The ctest
  `bootstrap` is what says the compiler reproduces itself.
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
  - **`new(T, n)` gives its result `mut` at every level** (D5.8), so `new(node*, n)` is
    `node mut* mut@ own` and storing it in a `node* mut@ own` field is refused: dropping the
    pointee's `mut` behind a mutable span is D5.4's `T** -> const T**` hole. A `cast` is the
    sanctioned escape and the only one; write it once, at the allocation, with the reason.
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
  by nothing until a glob in `fort_lint.py` names it. **One `fort --index` run judges every file
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
- A new `std/*.ft` reaches the language harness only after `tools/vm build <preset>` copies it
  into `build/<preset>/std`: running `run_tests.py` by hand against a source
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
  number. `--since` reads `git diff main...HEAD`, so it counts **committed** work only. A branch
  whose tests are still staged or untracked reads as the ratio of the commits before them. That
  number is smaller than the truth, and it sends an implementor off to write tests the branch
  already has. Commit first, then measure.
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
  effort; also checks spec citations, tests added, `xfail.txt` updates, commit hygiene, and the
  knowledge the ticket wrote down: the routing rule at the end of this file says which file takes
  it, so a learning lands in `notes/` or in `editors/README.md` more often than in `AGENTS.md`;
  returns findings with file:line and severity; never edits, commits or merges.
- Coordinator: re-runs the gate on the branch, **reads the diff's file list**, merges per the
  Change Implementation Loop (squash for a single unit, `--no-ff` for a multi-unit feature),
  deletes the worktree and branch, appends the merge sha and the agents' token counts to the
  ticket log, and assigns the tickets it unblocked. The file list is a separate job from the gate,
  which has no opinion about a file that should not exist: T-022 merged five scratch `.ft` probes
  into the repository root behind a green gate, and the review that called its scope clean had read
  the commit before the fix round that added them. `git diff --stat main...HEAD` before every
  merge, and look for what is new rather than what changed.

### Self-Updating Context (the routing rule)

An agent MUST write down a learning or a course correction at once. Two cases start an amendment:
- **Autonomous**: an agent finds something during development: a convention, a trap, a pattern
  that works or one that fails.
- **User-directed**: the user gives an instruction that changes how the project works: new
  tooling, a changed workflow, an updated convention.

**One home for each kind of knowledge.** `AGENTS.md` is not the default home: before T-098 it
measured 1491 lines and 128 KB, and the cause was a rule that sent every learning here. The table
says where a fact goes. **The first row that fits wins**, so a fact that two rows accept goes to
the higher row: the `pkill` rule is the VM's, `tools/lines.py` and the 3:1 ratio are the tests',
and "What checks `.ft` source" is the tests' as well.

| what you learned | where it goes |
|---|---|
| a rule of the language | `notes/decisions.md`, then the specification document that owns it |
| an invariant of a compiler pass | `notes/compiler.md` |
| the runtime or the standard library | `notes/compiler.md` |
| how a test is written or judged | `notes/testing.md` |
| the VM, the build, the shared folder | `notes/environment.md` |
| the VS Code extension | `editors/README.md` |
| a convention for code, text or commits | `notes/style.md` |
| process: tickets, agents, review, the change loop | `AGENTS.md` |
| a fact that one ticket needs | that ticket's Notes |

`AGENTS.md` keeps three kinds of knowledge -- the project layout, the process rules, and this
routing table -- and links to the rest. Its budget is 150 lines. It is over that budget until
T-099 moves the `## Environment`, `## Build and test` and `## Technical Standards` sections into
the `notes/` files above; those three sections split across `environment.md`, `testing.md`,
`compiler.md` and `style.md`, and T-099 routes each bullet by this table.

Five rules for an entry:
- Say what happened in one sentence, with a number, a command or a file path.
- Say what to do in one sentence or two.
- Cite the ticket, as `T-098`.
- Match the shape of the neighbours in that section.
- **Read the section before you append.** Extend the entry that is already there rather than add
  a second entry on the same subject.

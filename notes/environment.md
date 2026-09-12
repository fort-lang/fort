# fort environment: the VM, the build and the shared folder

This document holds every fact about the machine the project builds on: the Vagrant VM, the
`tools/vm` wrapper, the VirtualBox shared folder, the cross toolchain, and the CMake presets and
targets. It is not normative about the language; `spec/decisions.md` and `spec/grammar.md` win
over it (D1.2).

T-098 created the headings below. T-099 moved into them the `## Environment` section of
`AGENTS.md` and the build half of its `## Build and test` section, one bullet at a time and
without a rewrite.

## 1. The VM

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

## 2. The shared folder

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

## 3. Provisioning

- Provisioning installs `nodejs` (Node 18) for the extension's unit tests and fails loudly when it
  is older or has no built-in test runner.
- Provisioning disables apport and sets `kernel.core_pattern=core`: Ubuntu's piped core pattern
  ignores `ulimit -c 0` and made every SIGABRT cost about a second. A VM provisioned before that
  change needs `tools/vm provision` once (or the same two commands by hand).
- git runs on the host; it also works in the guest: provisioning symlinks the host path of the
  VM directory to `/vagrant`, so worktree `.git` files (absolute host paths) resolve there.
- **git in the guest reads one repository: the VM directory.** `tools/provision.sh` symlinks that
  directory alone (`ln -s /vagrant "$FORT_HOST_REPO"`, line 63). A worktree that runs its own VM
  with `FORT_VM_DIR="$PWD"` is therefore `/vagrant`, and the main checkout is not there, so the
  `gitdir:` line of the worktree's `.git` file points at a path the guest cannot open. Every git
  command in the guest then exits 128, and `tools/vm run 'python3 tools/lines.py --since main'`
  fails inside `git diff main...HEAD` rather than reporting a ratio. Run a measurement that needs
  git history on the host, which has the whole repository, or share the main checkout's VM
  (T-099).

## 4. The cross toolchain

- The target is x86-64 Linux. The compiler runs natively on arm64, emits LLVM IR and runs `clang
  --target=x86_64-linux-gnu` over it, so `--cc` names a clang (the guest `cc` is a native gcc and
  would build for aarch64); generated programs run under `qemu-x86_64` transparently. The verified
  line is in `spec/toolchain.md` 2 (D14.3), and `--target` names the triple (D14.1). Provisioning
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

## 5. The build

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
- Binaries: `build/<preset>/fort` is stage1 (the C compiler); `build/<preset>/stage2/fort` is the
  self-hosted compiler, which stage1 builds from `src/fort`. The `fort_stage2` target builds it at
  every build (stage1 over `src/fort/main.ft`, whose imports pull the rest of `src/fort` in), so a
  module stage1 rejects fails the build rather than the test run; stage2 is an x86-64 binary and
  runs under qemu like every program the compiler builds.
  `tools/bootstrap.sh [--preset <preset>] [--stage3]` builds stage1 and that stage2 by hand in the
  guest. With `--stage3` it builds stage1 and hands the fixed point to `tools/fixpoint.sh`, and it
  writes no stage2 of its own, since that script builds one per build mode.

## 6. The host side

- An ssh `ControlPath` under `os.tmpdir()` does not work on macOS: the host's temporary directory
  is `/var/folders/<...>/T`, ssh binds the socket under a temporary name of its own, and the total
  passes the 104-byte Unix domain socket limit, so ssh exits 255 with `unix_listener: path ... too
  long` -- indistinguishable, to a caller, from a VM that is down. Anything that multiplexes ssh
  from the host must measure the path and fall back to `/tmp/<something short>`. Nothing in the
  repository does any more: T-089 deleted the extension's own transport, and `tools/vm` is now the
  only thing that crosses into the guest.
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

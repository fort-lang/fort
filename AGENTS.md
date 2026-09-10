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
- The target is x86-64 Linux. The compiler runs natively on arm64; generated programs run under
  `qemu-x86_64` transparently. Always pass `--cc x86_64-linux-gnu-gcc` to `fort` (the guest `cc`
  is aarch64). Provisioning sets `QEMU_LD_PREFIX`; the test harness sets it itself.
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
  default `x86_64-linux-gnu-gcc`, into `build/<preset>/std/fort_rt.o` next to a copy of
  `std/*.ft`), `fort_rt_native` (the runtime compiled natively with `-DFORT_RT_NO_MAIN` for the
  unit tests and tidy), `lang_ffi_helpers` (`test/lang/ffi/*.c` built natively so `-Werror` and
  tidy cover them), `check` (ctest label `unit`), `check-lang` (language tests; prints
  `no harness yet` until T-005), `check-all` (both), `format` and `format-check` (clang-format
  over `src`, `runtime`, `test`), `tidy` (`run-clang-tidy` over the same), `lines`
  (`tools/lines.py`: test lines per compiler line, target 3:1, `--min RATIO` fails below it).
  `tools/vm <target> [preset]` runs one.
- `tools/vm gate` is the merge gate: `format-check`, `tidy`, and `check-all` under `debug`,
  `asan` and `ubsan` (it configures `debug` first, then configures and builds each preset before
  its `check-all`).
- Language tests: `test/lang/run_tests.py [filter]` (decisions D14.4, D14.5). `test/lang/xfail.txt`
  lists tests the compiler cannot pass yet; a listed test that passes fails the run, so shrink
  the list in the same commit that makes tests pass. `test/lang/bootstrap-unsupported.txt` lists
  tests that use features the C bootstrap deliberately lacks. `run_tests.py --lint` validates
  directives without a compiler.
- Unit tests: `test/<component>_test.c` with `test/test.h`; every `test/*_test.c` is globbed
  into an executable `build/<preset>/test/<component>_test` linked against `fort_core` and
  `fort_rt_native`, and a ctest `unit-<component>`. A `TEST` body is one macro argument: a comma
  outside parentheses (a brace initializer, for example) splits it. `#val` in an assertion
  message is the argument after macro expansion, so compare through a variable when the
  expected text matters. Suites are ordinary C11: no `__VA_OPT__`, and `-Wtype-limits` (gcc)
  rejects assertions that are always true, such as `TEST_ASSERT_GE_SIZE(n, 0)`.
- Binaries: `build/<preset>/fort` is stage1 (the C compiler); `build/<preset>/stage2/fort` and
  `stage3/fort` are the self-hosted compiler built by stage1 and by stage2.

## Technical Standards
- **Markdown**: Line-wrap at 100 characters, including tables and code blocks. Check with
  `awk 'length > 100 {print FILENAME": "FNR}' <files>`. Code fences use `fort`, `c`, `sh`,
  `asm` or `ebnf` as the language tag.
- **Language changes**: any change to the language is recorded in `notes/decisions.md` first
  (new decision number or amended decision with a note), then in the specification document that
  owns the topic, then in the tests under `test/lang/`. Specification text never contains "TBD",
  "pending" or "not finalized"; deferred features live only in decision D15.
- **Writing specification text**: cite the decision each rule implements as `(Dn.m)`. An agent
  that needs a rule the decision log does not settle uses the most conservative reading, marks it,
  and reports it to the lead for ratification; it never invents syntax or semantics. Every
  amendment to `notes/decisions.md` is relayed to agents still writing against the old text, and
  a separate audit pass reconciles the documents afterwards.
- **C sources**: C11 (`-std=c11`, `_POSIX_C_SOURCE=200809L`), no third-party code, warnings are
  errors under both clang (default) and gcc (`gcc` preset). Names: functions, variables,
  parameters, fields and struct/union/enum tags lower_case; typedefs lower_case with a `_t`
  suffix; enum constants, file-scope constants (static or not), function-scope static constants
  and macros UPPER_CASE (`enum { BYTE_MASK = 0xFFU }`); local constants lower_case; macros
  private to a header end with an underscore (`TEST_LOG_`). Every non-void call result is used
  or discarded with `(void)` (`TEST_UNUSED` in tests); no magic numbers (0 to 4, powers of two,
  `1.0` and `100.0` are allowed); uppercase literal suffixes; includes grouped as the file's own
  header, `<x.h>`, `<sys/x.h>`, project `"x.h"`, then `"test.h"`/`"common.h"`. `.clang-format`
  and `.clang-tidy` (clang 18) are the reference; `tools/vm format` reformats. This applies to
  test helpers under `test/` too. Two gaps of clang-tidy 18 are covered by review:
  `bugprone-unused-return-value` takes function names, not patterns (patterns arrive in
  clang-tidy 19), so `.clang-tidy` lists the C library and POSIX functions and the project's own
  functions are unchecked; and `readability-magic-numbers` skips macro arguments, so
  `TEST(name, { ... })` bodies are unchecked.
- **fort sources**: identifier conventions per decision D1.4: everything is lower_case with
  underscores, struct and enum type names and enum members included; only module constants are
  UPPER_CASE. A variable never takes its type's name (`point p`, `box bx`, `mut list* l`); a
  field may (`own mut node* node`), since fields are not variables and are outside the module
  namespace (D7.9).
- **Shell scripts**: bash with `set -eu`, clean under shellcheck at its default severity; the
  host has no shellcheck, run it in the guest: `tools/vm run 'shellcheck tools/vm
  tools/provision.sh'`.
- **Commit messages**: a title of about 50 characters (72 at most), a blank line, then a body
  wrapped at 72 columns that says what changed and why, then the attribution trailers.

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
self-contained units of work (for example the language design, or a compiler pass plus its tests plus its documentation)
keeps its individual commits and is merged into `main` with a merge commit
(`git merge --no-ff`) whose message describes the whole feature. Until a remote exists, `main`
plays the role of `origin/main`. Worktrees live in `.worktrees/`, which is gitignored.

### Tickets
- One markdown ticket per deliverable in `.tickets/` in the main checkout, never in a worktree;
  state is the directory: `todo/`, `inprogress/`, `done/`. Template and numbering rule in
  `.tickets/README.md`; fields: id, title, size, depends-on, deliverable, spec, branch, worktree,
  assignee, acceptance criteria (checkboxes), notes, log.
- A ticket is assigned only when every ticket in its `depends-on` is in `done/`. Independent
  tickets are assigned concurrently, one implementor each.
- Acceptance criteria are verifiable inside the VM; the log records every hand-off with its
  evidence (commands run, results, review rounds, merge sha).

### Agents
- `.claude/agents/implementor.md` (effort high, full tools) implements one ticket;
  `.claude/agents/reviewer.md` (effort xhigh, read-only tools, the `code-review` skill) reviews
  one branch. The coordinator is the main session. Reasoning effort is fixed per definition.
- Agent definitions in `.claude/agents/` are loaded when a session starts; restart the session
  after adding or changing one.

### Review Workflow
- Coordinator: picks a ticket whose dependencies are done, creates the worktree and branch
  (`.worktrees/fort-<id>`, `feat/<id>-<slug>`), fills branch/worktree/assignee, moves the ticket
  to `inprogress/`, spawns an `implementor` with the ticket path and worktree.
- Implementor: reads the ticket and the cited spec; codes and tests in the worktree with small
  green commits; runs `tools/vm gate`; spawns a `reviewer` with the branch, worktree and ticket;
  fixes or explicitly declines each finding in the ticket log; re-runs the gate; squashes if the
  ticket is a single unit of work; ticks every criterion with evidence; moves the ticket to
  `done/`; reports the branch to the coordinator.
- Reviewer: read-only; runs the `code-review` skill on the branch against `main`; also checks
  spec citations, tests added, `xfail.txt` updates, AGENTS.md updates and commit hygiene;
  returns findings with file:line and severity.
- Coordinator: re-runs the gate on the branch, merges per the Change Implementation Loop
  (squash for a single unit, `--no-ff` for a multi-unit feature), deletes the worktree and
  branch, appends the merge sha to the ticket log, and assigns the tickets it unblocked.

### Self-Updating Context (AGENTS.md Auto-Amendment)
AGENTS.md MUST be amended whenever a learning or course correction occurs. This applies in two
cases:
- **Autonomous**: When any process/agent discovers something important during development (e.g., a
  new convention, a gotcha, a pattern that works or fails), they MUST update the relevant section of
  AGENTS.md.
- **User-directed**: When the user gives an instruction that changes how the project works (e.g.,
  new tooling, changed workflow, updated conventions), the receiving agent MUST update AGENTS.md to
  reflect the change immediately.



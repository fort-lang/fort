# fort
A safe(r) C-like systems programming language.

## Naming
- The language is `fort`. Source files use the extension `.ft`. The compiler binary is `fort`.

## Project Layout
- `notes/`: the language specification. `notes/decisions.md` (numbered decision log) and
  `notes/grammar.md` are normative and win over every other document. Start at
  `notes/project-overview.md`.
- `test/`: `test/test.h` is the C macro framework for the compiler's unit tests (it expects a
  `common.h` providing `TALLY_UNUSED`); `test/lang/` holds language tests in the directive format
  defined in `notes/toolchain.md`.
- `src/`, `std/`, `runtime/`: compiler (C), standard library (fort) and C runtime; created in the
  implementation phase.
- `CMakeLists.txt`, `.clang-format` and `.clang-tidy` were copied from another project (`axle`,
  C++) as templates. They do not describe this repository yet and must be replaced when the
  compiler is scaffolded; do not try to build with them.

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
- **C sources**: every `.c`/`.h` file in the repository, including test helpers under `test/`,
  must pass `cc -std=c11 -Wall -Wextra -Wpedantic -Werror` and the repository's `.clang-tidy`
  with warnings as errors (no magic numbers, uppercase literal suffixes such as `0xFFU`).

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
`git reset --soft main && git commit`. A feature branch made of several self-contained units of
work (for example the language design, or a compiler pass plus its tests plus its documentation)
keeps its individual commits and is merged into `main` with a merge commit
(`git merge --no-ff`) whose message describes the whole feature. Until a remote exists, `main`
plays the role of `origin/main`. Worktrees live in `.worktrees/`, which is gitignored.

### Self-Updating Context (AGENTS.md Auto-Amendment)
AGENTS.md MUST be amended whenever a learning or course correction occurs. This applies in two
cases:
- **Autonomous**: When any process/agent discovers something important during development (e.g., a
  new convention, a gotcha, a pattern that works or fails), they MUST update the relevant section of
  AGENTS.md.
- **User-directed**: When the user gives an instruction that changes how the project works (e.g.,
  new tooling, changed workflow, updated conventions), the receiving agent MUST update AGENTS.md to
  reflect the change immediately.



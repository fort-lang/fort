# fort
A safe(r) C-like systems programming language.

## Writing: ASD-STE100 everywhere
- **Write in ASD-STE100**, the ASD Simplified Technical English specification. Name it in
  full: the standard is ASD-STE100, not "ASD-STE". The user asked for this on 2026-09-11 and
  then said "use it everywhere", so it governs **all** the text this project writes: replies to
  the user, code comments, commit messages, ticket text, the prompts that go to agents, and the
  specification documents in `spec/`. There is no exempt category.
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
- **Exactness wins over simplicity.** A rule in `spec/` must stay exact. If a short sentence
  would change what a decision means, write two short sentences. Do not drop a condition to make
  a sentence shorter.
- The rule applies to text you write or change. It does not ask anyone to rewrite the documents
  that are already there; that sweep is its own ticket if the user wants it.

## Naming
- The language is `fort`. Source files use the extension `.ft`. The compiler binary is `fort`.

## Project Layout
- `spec/`: the language specification; `notes/README.md` indexes it and `notes/` both.
  `spec/decisions.md` (the numbered decision log) and `spec/grammar.md` are normative and win
  over every other document. Start at `spec/project-overview.md`.
- `notes/`: the engineering knowledge. `notes/environment.md`, `notes/testing.md`,
  `notes/compiler.md` and `notes/style.md` take the facts the routing rule at the end of this
  file sends them.
- `test/`: `test/test.h` is the C macro framework for the compiler's unit tests
  (`test/common.h` provides `TEST_UNUSED`); `test/lang/` holds language tests in the directive
  format defined in `spec/toolchain.md`.
- `src/bootstrap/`: the C bootstrap compiler (stage1). It builds `bootstrap-0` and stays the
  second implementation. `tools/bootstrap.ref` pins the remaining chain. `notes/compiler.md` 8
  holds the five invariants. `src/fort/`: the
  compiler in fort (stage2 and stage3). `src/lsp/`: the language server in fort, whose modules
  are reached as `lsp.<name>` and never `<name>` through the search root `src` (T-063,
  `notes/compiler.md` 8). `std/`: the standard library in fort, the runtime (`std/rt.ft`) among
  its modules; there is no C runtime and no object linked beside the program (T-091). `tools/`:
  `vm`, `provision.sh`, and `lines.py`.
- `editors/`: `editors/vscode/` is the VS Code extension -- `package.json`,
  `language-configuration.json`, `syntaxes/fort.tmLanguage.json`, `extension.js`, and the one pure
  module it is tested through, `lib/check.js` -- and `editors/README.md` is its install guide, its
  manual smoke test and its list of limitations. It highlights fort and shows the compiler's
  diagnostics, and that is all it does (T-089). It is installed on the **host**, where VS Code runs.
- `CMakeLists.txt`, `CMakePresets.json` and `cmake/sanitizers.cmake` are the build;
  `.clang-format` and `.clang-tidy` (clang 18) are the C11 lint configuration.
- `.tickets/` (gitignored, main checkout only) is the ticket board; `.codex/agents/` holds seven
  project agent roles. `.codex/config.toml` limits Codex to three child threads (T-139).

## Where the rest of this knowledge lives

The `## Environment`, `## Build and test` and `## Technical Standards` sections that stood here
moved into `notes/` (T-099). The routing table at the end of this file says which file takes a
new fact. Read the file for your work before you start it.

| file | holds |
|---|---|
| `notes/environment.md` | the VM, `tools/vm`, the shared folder, the cross toolchain, the build |
| `notes/testing.md` | the merge gate, each test corpus, how to write a test, what a test misses |
| `notes/compiler.md` | the invariant of each pass, the runtime, the port to fort |
| `notes/style.md` | comments, the C and fort conventions, markdown, shell, commit messages |
| `editors/README.md` | the VS Code extension and the conventions of its sources |

## Mandatory Process Rules
The following rules MUST be followed by each process/agent for each change being made. There are no
exceptions.

### Lifecycle of a Change
#### Codebase Exploration
Each process/agent MUST explore the relevant portions of the codebase as indicated by the task at
hand.

#### Worktree Isolation
Each Codex implementor MUST work in a separate git worktree and associated branch. Create the
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
- Keep one markdown ticket per deliverable in `.tickets/` in the main checkout, not a worktree.
  States: `todo/`, `inprogress/`, `inreview/`, `done/`. See `.tickets/README.md` for rules.
  It gives the template and number rule. Fields: id, title, size, critical-path, depends-on, impl,
  review, deliverable, spec, branch, worktree, assignee, acceptance criteria, notes, log.
  `Notes` holds four subsections in this order: `Design`, `Open questions`, `Deviations` and
  `Review findings`. An open question gives the question in one line, an `owner:` line and a
  `resolution:` block whose first line is the ruling and whose following lines give the reason; a
  one-line resolution loses the measurement that makes a ruling checkable (T-102, measured on
  T-099 and T-086). A deviation cites its decision as `(Dn.m)` and names who ratified it. A review
  finding carries `file:line`, a severity and either `fixed: <evidence>` or `declined: <reason>`.
  `Log` holds one line for each event, in one shape: `- YYYY-MM-DD role: event; evidence`. A
  ruling, a measurement or a finding goes into the matching Notes subsection and the Log line
  points at it. **`.tickets/` is gitignored and `.tickets/README.md` is untracked**, so the
  template reaches no commit and a reader verifies it by reading the file in the main checkout.
- Assign a ticket only when each dependency is in `done/` after its merge into `main`. Assign
  independent tickets concurrently, one implementor each.
- Acceptance criteria are verifiable inside the VM or on a named native host. The ticket log names
  that host and records each hand-off with
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
- **Say how a claim was established, not only what it claims.** An audit whose verdicts all read
  the same hides which rows a reader may lean on. T-077 marks each row `mutation-measured` (a
  mutant was built and a suite was run), `probed` (programs were compiled and their output is
  quoted) or `read` (code was read and nothing ran). Lean on the first two as on a test and on the
  third as an argument. Of T-077's six further claims, one is mutation-measured, one probed and
  four read, and those four are exactly where a later ticket has work to do (T-077).

### Agents
- The coordinator is the main Codex session. Seven TOML roles live in `.codex/agents/` (T-139).
  Each role inherits the coordinator model and sets effort. Give its text and effort to a built-in
  worker until named loading is proved. Restart Codex after a role changes.
- Implementors use full tools: `impl-mech` (medium) for work the specification
  pins completely, transcription and coverage; `impl-std` (high) for ordinary tickets that need
  data-structure design; `impl-hard` (xhigh) for cross-cutting invariants, the calling
  convention, memory layout, ownership and codegen; `impl-port` (medium) for transliterating a
  tested C module into fort against an oracle.
- Reviewers run in a separate read-only Codex session; parent live settings can override a role's
  sandbox (T-139). `rev-quick` (medium) screens a low-risk diff for conventions,
  tests and scope; `rev-std` (high) reviews an
  ordinary change; `rev-deep` (xhigh) re-derives the invariants independently for
  ABI, memory, ownership, exact arithmetic, unsafe casts and generated code.
- Each ticket names its tiers in its `impl:` and `review:` fields. Review depth follows the risk
  of the change, not the effort it took to write: a ticket can be `impl-std` and `rev-deep`.
- A tier is a default, not a verdict. If a ticket run at `impl-mech` or `impl-port` fails the
  gate twice or comes back with a must-fix finding, the coordinator re-runs it one tier up and
  records the promotion in the ticket log; two promotions out of one tier means the mapping is
  wrong, so change the tickets' `impl:` field rather than promoting case by case.

### Review Workflow
- Coordinator: picks an unblocked ticket. Create `.worktrees/fort-<id>` and branch
  `feat/<id>-<slug>`. Fill its branch, worktree, and assignee; move it to `inprogress/`.
  Give a worker role text, effort, ticket path, and worktree.
  Use named roles after Codex proves that it loads them.
- Implementor: reads only the ticket and its cited specification sections; codes and tests there.
  Keep each commit green under `tools/vm check`. Squash a single-unit ticket first.
  Hand off a clean SHA and `tools/vm check` evidence in 40 lines. Do not start the gate or review.
- Coordinator: records identity. A worker starts one ticket-worktree `tools/vm gate` and waits.
  The worker messages at exit or for help. Start read-only review on that SHA; move the ticket to
  `inreview/`. For `rev-std`, use:

      codex review --strict-config -c 'sandbox_mode="read-only"' \
        -c 'model_reasoning_effort="high"' \
        -c 'developer_instructions="Read and follow .codex/agents/rev-std.toml."' --base main
  Add the ticket's absolute path to `developer_instructions`. Use the selected tier and effort.
  Never pass `[PROMPT]` with `--base`. Keep the ticket in `inreview/` through fixes. Relay findings.
  After a source fix, discard its gate. Gate the fixed SHA; review if behavior changes.
- Reviewer: follows its TOML. Effort differs from depth. Check tests, citations, commits, and scope.
  Check xfail and the ticket. Route learning below. A live gate is pending; never edit or merge.
- Coordinator: uses gate-worker messages for live state. Do not poll a responsive worker.
  Make at most one fallback poll per 20 message-free minutes, only when the worker appears stuck.
  Require `tools/vm gate: green`. Log counts, source SHA, log SHA, and freshness.
  After gate exit and final review, measure identity under `notes/environment.md` 1.
  Reuse a green gate only if identity and log SHA match and preset count is zero. Log reuse.
  If proof differs or is missing, rebase when `main` changed, then rerun the gate.
  Review again if rebase changes behavior. Read `git diff --stat main...HEAD` and new files.
  Merge per the Change Implementation Loop. Record the merge sha and agent tokens in the Log.
  Tick merge criteria. Move the ticket to `done/`. Remove its worktree and branch. Assign tickets.
  Before worktree removal, write gate numbers into the ticket log, not the gate-log path.
  The log dies with the worktree. Record exit status, the self-hosted count, and the ctest line.
  Re-read each count constant that two branches changed after a rebase or merge.
  Equal increments can merge silently and leave a wrong total. T-097 found two wrong totals.
  Run the tool that owns each count and read its failure.

### Self-Updating Context (the routing rule)

An agent MUST write down a learning or a course correction at once. Two cases start an amendment:
- **Autonomous**: an agent finds something during development: a convention, a trap, a pattern
  that works or one that fails.
- **User-directed**: the user gives an instruction that changes how the project works: new
  tooling, a changed workflow, an updated convention.

**One home for each kind of knowledge.** `AGENTS.md` is not the default home: before T-098 it
measured 1491 lines and 128 KB, and the cause was a rule that sent every learning here. The table
says where a fact goes. **The first row that fits wins**, so a fact that two rows accept goes to
the higher row: the `pkill` rule is the VM's, `tools/lines.py` and the test ratio are the tests',
and "What checks `.ft` source" is the tests' as well.

| what you learned | where it goes |
|---|---|
| a rule of the language | `spec/decisions.md`, then the `spec/` document that owns it |
| an invariant of a compiler pass | `notes/compiler.md` |
| the runtime or the standard library | `notes/compiler.md` |
| how a test is written or judged | `notes/testing.md` |
| the VM, the build, the shared folder | `notes/environment.md` |
| the VS Code extension | `editors/README.md` |
| a convention for code, text or commits | `notes/style.md` |
| process: tickets, agents, review, the change loop | `AGENTS.md` |
| a fact that one ticket needs | that ticket's Notes |

`AGENTS.md` keeps three kinds of knowledge -- the project layout, the process rules, and this
routing table -- and links to the rest. Its budget is 150 lines. T-099 moved the
`## Environment`, `## Build and test` and `## Technical Standards` sections into the four
`notes/` files above and routed each bullet by this table; the file measured 1538 lines before
that move and 223 after it. The 73 lines above the budget have one candidate and one only:
`## Writing: ASD-STE100 everywhere` is 27 lines and this table sends a text convention to
`notes/style.md`. Everything else here is the layout, the process or this table, so a move that
closes the last 46 lines would put process knowledge outside `AGENTS.md`. The user decides which
of the two the budget means; until then the number stands as a target and not as a rule (T-099).
The lint holds that number: `BUDGET_LIMIT` in `tools/knowledge_lint.py` is 252, the measured
value, so any growth trips it; `BUDGET_TARGET` is 150 and no test reads it (T-103).

Five rules for an entry:
- Say what happened in one sentence, with a number, a command or a file path.
- Say what to do in one sentence or two.
- Cite the ticket, as `T-098`.
- Match the shape of the neighbours in that section.
- **Read the section before you append.** Extend the entry that is already there rather than add
  a second entry on the same subject.

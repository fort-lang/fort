# fort
A safe(r) C-like systems programming language.

## Technical Standards
- **Markdown**: Line-wrap at 100 characters.

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
`feat/`, etc. as you see appropriate.

#### Change Implementation Loop
Always implement a change in small incremental commits. A commit MUST be composed of a
self-contained unit of logic that improves the overall system. No commit MUST break _any_ test in
the repository. Before committing a change to `git`, make sure all tests pertinent to the component
you are working on run successfully, and make sure that the code format and lint checks pass. Rebase
on top of `main` frequently to reduce the chances of merge conflicts.

Once done with a change, squash all commits on the branch into one via interactive rebase (`git
rebase -i origin/main`, mark all but the first as `squash`). Write a meaningful commit message that
describes _what_ and _why_ -- do not just collate the individual commit messages.

### Self-Updating Context (AGENTS.md Auto-Amendment)
AGENTS.md MUST be amended whenever a learning or course correction occurs. This applies in two
cases:
- **Autonomous**: When any process/agent discovers something important during development (e.g., a
  new convention, a gotcha, a pattern that works or fails), they MUST update the relevant section of
  AGENTS.md.
- **User-directed**: When the user gives an instruction that changes how the project works (e.g.,
  new tooling, changed workflow, updated conventions), the receiving agent MUST update AGENTS.md to
  reflect the change immediately.



# notes: the index

This directory holds the language specification and the project's engineering knowledge. Start at
`project-overview.md`.

`AGENTS.md` in the repository root holds the process rules and the routing table that says which
file below takes a new fact. Read that table before you write a fact anywhere.

## Normative

`decisions.md` and `grammar.md` win over every other file, including this one and the code (D1.2).

| file | holds |
|---|---|
| `decisions.md` | the numbered decision log, `Dn.m`; every rule of the language is settled here |
| `grammar.md` | the grammar of fort v1 |

## The specification

Each file implements the decisions it cites and loses to `decisions.md` and `grammar.md`.

| file | holds |
|---|---|
| `project-overview.md` | what fort is, the phases, and the reading order |
| `core-language.md` | statements, expressions, functions, and the core syntax |
| `type-system.md` | types, mutability, conversions and constants |
| `memory-model.md` | where values live, ownership, pointers, spans and the run-time checks |
| `module-system.md` | modules, imports, name resolution and the C foreign function interface |
| `stdlib.md` | the standard library |
| `toolchain.md` | the command line, the build pipeline, diagnostics, the runtime, codegen, tests |

## Engineering knowledge

These four files describe the machine, the tests, the compiler and the conventions. They describe
the implementation; a difference between one of them and the code is a bug in one of the two.

| file | holds |
|---|---|
| `environment.md` | the VM, `tools/vm`, the shared folder, the cross toolchain, the build |
| `testing.md` | the merge gate, each test corpus, how to write a test, what a test cannot see |
| `compiler.md` | the invariant of each pass, and the rules of the port to fort |
| `style.md` | comments, C and fort conventions, markdown, shell, commit messages |

`environment.md`, `testing.md` and `compiler.md` hold their headings, each with one sentence
that says what the heading takes, and no other text. T-098 created them; T-099 moves the text out
of `AGENTS.md` into them. `style.md` 1, the comment policy, is in force now and T-103 lints it.

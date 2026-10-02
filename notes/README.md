# notes: the index

This directory holds the project's engineering knowledge. `spec/` holds the language
specification; start there, at `spec/project-overview.md`.

`AGENTS.md` in the repository root holds the process rules and the routing table that says which
file below takes a new fact. Read that table before you write a fact anywhere.

## Normative

`spec/decisions.md` and `spec/grammar.md` win over every other file, including this one and the
code (D1.2).

| file | holds |
|---|---|
| `spec/decisions.md` | the numbered decision log, `Dn.m`; every rule of fort is settled here |
| `spec/grammar.md` | the grammar of fort v1 |

## The specification

The files below stand in `spec/`. Each one implements the decisions it cites and loses to
`spec/decisions.md` and `spec/grammar.md`.

| file | holds |
|---|---|
| `spec/project-overview.md` | what fort is, the phases, and the reading order |
| `spec/core-language.md` | statements, expressions, functions, and the core syntax |
| `spec/type-system.md` | types, mutability, conversions and constants |
| `spec/memory-model.md` | storage, ownership proof, foreign trust, and runtime checks |
| `spec/module-system.md` | modules, imports, name resolution and the C foreign function interface |
| `spec/stdlib.md` | the standard library |
| `spec/toolchain.md` | options, staged ownership delivery, diagnostics, runtime, and codegen |
| `spec/fir.md` | FIR, source and alias obligations, ordered effects, and LLVM translation (D19.8) |

## Engineering knowledge

These four files describe the machine, the tests, the compiler and the conventions. They describe
the implementation; a difference between one of them and the code is a bug in one of the two.

| file | holds |
|---|---|
| `environment.md` | the VM, `tools/vm`, the shared folder, the cross toolchain, the build |
| `testing.md` | the merge gate, each test corpus, how to write a test, what a test cannot see |
| `compiler.md` | the invariant of each pass, and the rules of the port to fort |
| `style.md` | comments, C and fort conventions, markdown, shell, commit messages |

T-098 created `environment.md`, `testing.md`, `compiler.md` and `style.md`; T-099 moved the text
of the `## Environment`, `## Build and test` and `## Technical Standards` sections of `AGENTS.md`
into them, and moved the nine specification files into `spec/`. `style.md` 1, the comment policy,
is in force now and T-103 lints it.

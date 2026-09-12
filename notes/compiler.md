# fort compiler: the invariants of the passes

This document holds the invariants of the compiler itself: what each pass owns, what it may read,
and the rules a change to it must keep. It describes the implementation. Where it disagrees with
`notes/decisions.md`, `notes/grammar.md` or the specification documents, those files win (D1.2),
and a difference between this file and the code is a bug in one of the two.

The headings below are empty. T-098 created them; T-099 moves the compiler bullets of the
`## Technical Standards` section of `AGENTS.md` into them, one bullet at a time and without a
rewrite. That section splits four ways: at least 6 of its bullets are test facts and go to
`notes/testing.md`, the convention bullets go to `notes/style.md`, and the rest come here. Each
heading names what it takes.

## 1. Terms

The words the compiler uses for one thing each: a `span` is the fort type `T@` (D3.5), so a byte
extent or a source extent is a range.

## 2. The lexer

Lexical error recovery, the line the lexer drops, the per-file diagnostic cap, and the token array
that always ends in the end-of-file token.

## 3. The parser

`p->failed` and what it means, the recovery points, the rules of a skip, the speculation
interface, and the node ranges of D20.4.

## 4. The checker

The annotations and when they are cleared, lazy struct layout and the demand graph, ownership and
`check_owning`, `expr_t.empty` against `expr_t.mut`, and the rules that belong here rather than in
the loader.

## 5. Modules, the driver and the diagnostics

`driver_analysis_t` and the window in which an annotated tree may be read, the pass order of
`module_set_pass_at`, and the ownership of a `diag_record_t`.

## 6. The IR emitter

The shared scratch buffer, `gen_todo`, the break and continue targets, the scope stack that expands
deferred statements, the deduplication of declarations by C name, and the calling convention rules
that only the emitted text can hold.

## 7. The runtime and the standard library

`std.rt`, the closure that holds it, the entry point signatures and the three places each one is
written down; and how a library module is written against C, which is the home of the "Writing a
library module against C" bullet of `AGENTS.md`.

## 8. The port to fort

The constructs the bootstrap subset lacks and what replaces each, the re-entrancy rules of
`src/fort` (D20.5), and the rule that a ported pass answers to its C oracle.

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

**A feature stage2 has and stage1 lacks is written without using it** (T-041, floats). The
compiler may accept a construct its own source may not hold, and the two halves of that are
separate: the checker and the emitter gain the construct, and the code that implements it stays in
the bootstrap subset. `src/fort/flt.ft` holds an IEEE 754 value as the `u64` of its binary64
pattern and computes on it with integer arithmetic over bignums, because a float variable in the
compiler's own source would stop stage1 building stage2. Three costs a ticket of this shape pays:
`tools/diff_ast.sh` cannot compare a file the two parsers disagree about, so it skips the ones
whose stage1 diagnostics carry the refusal and holds the number skipped as an equality; the corpus
needs one unsupported list per compiler (`notes/testing.md`); and the differential oracles see
less, so the new construct is pinned by named assertions -- `test/fort/gen_float_test.ft` for the
emitted text and `test/fort/check_float_test.ft` for the checker's answers -- rather than by
stage1's output.

**The standard library may use such a feature before `src/fort` can.** `std/rt_float.ft` holds the
float printers of D18.1 and is written with floats, because stage1 never loads it: the loader
takes it into a closure that holds a float and into no other (`src/fort/modules.ft`), so the
compiler that has no floats never reads it and `tools/diff_ir.sh` keeps comparing every program
both compilers build. What that costs is one ctest of its own, `fort_lint_float`, since the lint
runs the compiler without floats over `std/*.ft` and cannot check that one.

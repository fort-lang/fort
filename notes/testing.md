# fort testing: how a test is written and how a change is judged

This document holds every fact about the test corpora and the merge gate: what each suite is, how
to write one, what each oracle can see, and what it cannot see. `notes/toolchain.md` 7 specifies
the language test format and wins over this file; `notes/decisions.md` and `notes/grammar.md` win
over both (D1.2).

The headings below are empty. T-098 created them; T-099 moves the text of the test half of the
`## Build and test` section of `AGENTS.md` into them, and the test bullets of
`## Technical Standards` with it, one bullet at a time and without a rewrite. Each heading names
what it takes.

## 1. The merge gate

`tools/vm gate`, the three presets it runs, one gate for each worktree, and how to capture its
output so that the exit status is the tests' and not a pipe's.

## 2. Unit tests in C

`test/<component>_test.c`, `test/test.h`, the suite naming rule, the `TEST` macro traps, the
clang-tidy caps on `main`, `test/fork.h`, `test/fake_cc.sh` and the shared support library.

## 3. Language tests

`test/lang/run_tests.py`, the directives, the discovery rules, `xfail.txt`,
`bootstrap-unsupported.txt`, the `--lint`, `--check-json` and `--index` modes, and the sandbox a
run test executes in.

## 4. fort module tests

`test/fort/<module>_test.ft`, `test/fort/support/`, the panic tests, and the allocator and program
break probes that are the only leak oracle a fort module has.

## 5. Differential oracles and the fixed point

`tools/diff_tokens.sh`, `tools/diff_ast.sh`, `tools/diff_check.sh`, their `FT_FILES` and
`CLEAN_FILES` equalities, the ctest `bootstrap`, and `tools/diag_coverage.py`.

## 6. Generated code and the cross pipeline

The `test/gen*_test.c` suites, `verified()`, `test/ir/*.ll`, `test/pipeline_test.sh`, and
`--verify-ir`.

## 7. What a test cannot see

The rules that no corpus can judge: an internal ABI mutation, a `declare` whose types disagree with
its call site, an instruction after a terminator, a line buffering rule behind a pipe, and a
`//! stderr:` directive asked to prove that a line is absent.

## 8. The grammar and the extension

`test/highlight_test.py`, `CORPUS_DIRS` and `CORPUS_FILES`, and the `node --test` suites of
`editors/vscode`.

## 9. The test-to-code ratio

`tools/lines.py`, what it counts, the 3:1 target, and `--since main --min 3.0` as the measurement
each ticket makes on its own diff.

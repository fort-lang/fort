# FIR tests

A FIR test holds one or more functions in the textual form of `spec/fir.md` 13, the passes to
run, and the expected result (`spec/fir.md` 16.3). `run_tests.py` walks `test/fir/**/*.fir`. For
each file it runs `fort --fir-test <file>` from this directory and compares the result with the
expectation of the file. The ctest `fir` (label `unit`) runs the harness in both gates.

## The format

The header is the leading run of `//!` and `//|` lines, and the FIR text follows it. The FIR
parser reads every header line as a comment. A `//| ` line gives the text after the marker, and a
bare `//|` gives an empty line.

| Directive | Meaning |
|---|---|
| `//! pass: <name>` | Required and repeatable. A pass to run on the parsed module, in order. |
| `//! prelude:` then `//| ` lines | Optional. Fort declarations, checked as the module `main`. |
| `//! expect:` then `//| ` lines | The compiler exits 0 and writes exactly this text. |
| `//! error: <text>` | Repeatable. The compiler exits 1, and its stderr holds `<text>`. |
| `//! panic: <text>` | The compiler ends by SIGABRT, and its stderr holds `<text>`. |

A test states exactly one of `expect:`, `error:` and `panic:`. `panic:` is for a rule of the
verifier, which ends the compiler with a panic.

The passes run in the order of their `pass:` directives, each on every function in text order:
- `none` runs nothing, so the test holds the parser and the printer.
- `verify` runs the verifier of `spec/fir.md` 10. A module whose functions keep every rule is
  written as `none` writes it. The first violation ends the compiler with a panic that names the
  rule, the function, the block and the statement.
- `ownership-local` runs the local ownership analysis of `--ownership-check` on each function
  (`spec/toolchain.md` 1). It tests each function with the verifier first, and a broken rule
  makes the compiler exit 2 with no panic. A validated violation is an error and a note at its
  location, and an incomplete local proof is one `ownership proof is incomplete` error. After
  an error the compiler exits 1 and writes no FIR, so a test of a violation uses `error:`. A
  function whose local proof is complete is written as `none` writes it. The pass must come
  before every `build-mode` pass. **It gives no closure proof.** It runs the local analysis
  alone: no call summary, no global, no process exit. A function that stores an owner in a
  global and returns passes it with exit 0 (`ownership_local/006_global_store.fir`). Only
  `--ownership-check` on a program holds the obligation of that global (D17.19).
- `build-mode` runs the build-mode pass of `spec/fir.md` 11. Its arguments select the mode:
  `--release`, `--no-bounds-check`, both in either order, or none for the default mode. Spaces
  separate the arguments. The pass does not run the verifier, so a test of it names `verify`
  in a second `pass:` directive.

A pass that the compiler does not know, an argument of `none`, `verify` or `ownership-local`,
an `ownership-local` pass after a `build-mode` pass, and an argument of `build-mode` that is not
a mode make the compiler exit 2 before any pass runs, and the harness reports an `ERROR`.

The prelude keeps the lines and the columns of the file. The compiler replaces the `//|` of each
prelude line with three spaces and blanks every other line, so a diagnostic of the prelude names
the line and the column in the `.fir` file.

The names of the FIR text resolve against the prelude:
- A type spelling resolves in the scope of the prelude module alone (`spec/fir.md` 13). A struct
  or an enum of another module is named by its bare name, so declare it in the prelude.
- `fn main.f` of a header names the prelude's `f` when the prelude declares it, and then the
  header must have the type of that declaration. Otherwise the parser makes a new function `f`
  of the module `main`.
- `global m.NAME`, `fn m.f` and `enum_table m.E` name a declaration of the module `m`; `extern
  name` names an `extern fn` by its C name. `enum_table` takes the type `enum_member*` of the
  prelude.

Put a test in `test/fir/<pass>/NNN_name.fir`, where `<pass>` is the first word of its first
`pass:`, with `-` written as `_`: `test/fir/build_mode/001_overflow_release.fir`.
A test of the verifier names its rule instead of a number: `verify/vNN_name.fir` breaks rule
`VNN`, and `verify/green_name.fir` keeps every rule.

## Running

```sh
python3 test/fir/run_tests.py --fort build/Linux/debug/fort --std-dir build/Linux/debug/std
python3 test/fir/run_tests.py --list                  # the tests, and nothing runs
python3 test/fir/run_tests.py --bless none/003        # write the expect: block of a test
```

A filter selects the tests whose path holds it. Each test is `PASS`, `FAIL` with its reason (a
unified diff for a text that differs) or `ERROR` (exit 2, a timeout or a compiler that does not
start). The last line counts them, `run_tests.py: 3 tests: 3 passed, 0 failed, 0 errors`, and the
harness exits 1 when a test does not pass, and when a filter selects no test. A header with a
problem stops the run before any test starts. `--bless` rewrites the `expect:` block of each
selected test that exits 0; review the diff of the blessed files before a commit.

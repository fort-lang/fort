# FIR tests

A FIR test holds one or more functions in the textual form of `spec/fir.md` 13, the pass to run,
and the expected result (`spec/fir.md` 16.3). `run_tests.py` walks `test/fir/**/*.fir`. For each
file it runs `fort --fir-test <file>` from this directory and compares the result with the
expectation of the file. The ctest `fir` (label `unit`) runs the harness in both gates.

## The format

The header is the leading run of `//!` and `//|` lines, and the FIR text follows it. The FIR
parser reads every header line as a comment. A `//| ` line gives the text after the marker, and a
bare `//|` gives an empty line.

| Directive | Meaning |
|---|---|
| `//! pass: <name>` | Required. The pass to run on the parsed module. |
| `//! prelude:` then `//| ` lines | Optional. Fort declarations, checked as the module `main`. |
| `//! expect:` then `//| ` lines | The compiler exits 0 and writes exactly this text. |
| `//! error: <text>` | Repeatable. The compiler exits 1, and its stderr holds `<text>`. |
| `//! panic: <text>` | The compiler ends by SIGABRT, and its stderr holds `<text>`. |

A test states exactly one of `expect:`, `error:` and `panic:`. `panic:` is for a rule of the
verifier, which ends the compiler with a panic.

The passes: `none` runs nothing, so the test holds the parser and the printer. `verify`,
`build-mode --release` and `build-mode --no-bounds-check` are the names of the verifier and of
the build-mode pass. These passes do not exist yet: the compiler exits 2 for them, and the
harness reports an `ERROR`.

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

Put a test in `test/fir/<pass>/NNN_name.fir`, where `<pass>` is the first word of its `pass:`.

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

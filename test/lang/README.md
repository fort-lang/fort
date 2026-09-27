# Language tests

End-to-end tests for the `fort` compiler. The convention is fixed by decisions D14.4 and D14.5
in `spec/decisions.md` and described in `spec/toolchain.md` section 7; this file is the short
version. Each test is a `.ft` file whose expected behavior is encoded in `//!` directives at the
top of the file.

## Layout (D14.4)

- `run/<area>/NNN_name.ft`: compile, run, then compare stdout, exit status and stderr.
- `fail/<area>/NNN_name.ft`: must not compile; every annotated line must produce a diagnostic.
- `run/modules/<name>/main.ft` and `fail/modules/<name>/main.ft`: multi-file tests. The harness
  compiles `main.ft` with its directory as the search root, reads the directives from `main.ft`
  and collects `error` annotations from every `.ft` file in the directory. Sibling modules carry
  no `//!` directives.
- `ffi/*.c`: C helpers that tests link in with `//! link:`.
- `programs/*.ft`: larger programs that exercise many features at once, treated as run tests.
  `programs/wc.ft` is the worked example of `spec/stdlib.md` section 4 byte for byte below
  its directives, so a change to one is a change to the other; `programs/cat.ft` is the
  example of section 2.4 with its final `?:` written as an `if`.

`NNN` starts at `001` within each area, with no gaps. Tests use only the core language and the
builtins; standard-library tests go under the `stdlib` area once the library exists.

## Directives (D14.5)

All directives are `//!` lines at the top of the file before any code, except `error`, which
annotates a line.

| Directive                     | Meaning                                                         |
|-------------------------------|-----------------------------------------------------------------|
| `//! run` / `//! fail`        | Required on line 1.                                             |
| `//! flags: --release`        | Extra compiler flags.                                           |
| `//! args: a b c`             | Command-line arguments, visible as `args[1..]`.                 |
| `//! link: ffi/helpers.c`     | C file to compile and link, relative to `test/lang`; repeatable.|
| `//! stdin:` + `//< ` lines   | Standard input, one line per `//< ` line.                       |
| `//! stdout:` + `//| ` lines  | Expected stdout, compared exactly (see below).                  |
| `//! exit: N`                 | Expected exit status; default 0.                                |
| `//! abort`                   | Expect SIGABRT (runtime error, `panic`, failed `assert`).       |
| `//! stderr: <substring>`     | Substring that must appear in stderr; repeatable.               |
| `//! error: <substring>`      | `fail` only, at the end of the offending line.                  |
| `//! error-any: <substring>`  | `fail` only, at the top: an error with no useful line.          |

The expected stdout is each `//| ` line's text after `//| ` followed by a newline; a bare `//|`
is an empty line. The comparison is exact, trailing spaces included, so output without a final
newline cannot be expressed: use `println`. Each `//< ` line is fed to the program with a
newline. Every `stderr` substring given must appear in stderr.

Example:

```fort
//! run
//! args: x
//! stdout:
//| 2 x
fn main(string@ args) i32 {
    println(args.len, " ", args[1]);
    return 0;
}
```

## Running

`test/lang/run_tests.py [options] [filter...]` compiles and runs every test (or those whose
path contains a filter) and prints one `PASS`, `FAIL`, `XFAIL`, `XPASS` or `ERROR` line per
test plus a summary; `--lint` validates the directives without a compiler, `--list` lists the
tests, `-v` shows the commands and outputs of failures and `--keep` keeps the temporary
directories. From the VM, `tools/vm integration` runs it with the debug build. `xfail.txt` lists
the tests the product compiler cannot pass yet. A listed test that passes fails the run.

## How a test is judged

- `run`: the compiler must exit 0. The program is run with the given `args` and `stdin`. Its
  stdout must equal the `//| ` lines byte for byte, its exit status must equal `exit` (or the
  process must die with SIGABRT when `abort` is given), and its stderr must contain every
  `stderr` substring.
- `fail`: the compiler must exit 1. Every line carrying `//! error:` must produce a diagnostic
  on that line whose text contains the substring, and no diagnostic may appear on an unannotated
  line. `error-any` accepts a diagnostic on any line. `stderr` substrings apply to the compiler's
  stderr.

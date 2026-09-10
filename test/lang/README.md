# Language tests

End-to-end tests for the `fort` compiler. Each `.ft` file is a complete program whose expected
behavior is encoded in `//!` directives at the top of the file. The full convention is specified
in `notes/toolchain.md` (decisions D14.4 and D14.5 in `notes/decisions.md`); this file is the
short version.

## Layout

- `run/<area>/NNN_name.ft`: compile, run, then compare stdout, exit status and stderr.
- `fail/<area>/NNN_name.ft`: must not compile; every annotated line must produce a diagnostic.
- `run/modules/<name>/main.ft`: multi-file tests. The harness compiles `main.ft` with its
  directory as the search root, so sibling files and subdirectories are importable modules.
  `fail/modules/<name>/main.ft` is the same layout for tests that must not compile.
- `ffi/*.c`: C helpers that tests link in with `//! link:`.
- `programs/*.ft`: larger programs that exercise many features at once.

`NNN` starts at `001` within each area. Tests use only the core language and the builtins;
standard-library tests live elsewhere.

## Directives

All directives are at the top of the file before any code, except `error`.

| Directive                     | Meaning                                                        |
|-------------------------------|----------------------------------------------------------------|
| `//! run` / `//! fail`        | Required on line 1.                                            |
| `//! flags: --release`        | Extra compiler flags.                                          |
| `//! args: a b c`             | Command-line arguments, visible as `args[1..]`.                |
| `//! link: ffi/helpers.c`     | C file to compile and link, relative to `test/lang`; repeatable.|
| `//! stdin:` + `//< ` lines   | Standard input fed to the program.                             |
| `//! stdout:` + `//| ` lines  | Expected stdout, compared exactly (trailing spaces included).  |
| `//! exit: N`                 | Expected exit status; default 0.                               |
| `//! abort`                   | Expect SIGABRT (runtime error, `panic`, failed `assert`).      |
| `//! stderr: <substring>`     | Substring that must appear in stderr.                          |
| `//! error: <substring>`      | `fail` only, at the end of the offending line.                 |
| `//! error-any: <substring>`  | `fail` only, at the top: an error with no useful line.         |

Example:

```
//! run
//! args: x
//! stdout:
//| 2 x
fn i32 main(string[] args) {
    println(args.len, " ", args[1]);
    return 0;
}
```

## How a test is judged

- `run`: the compiler must exit 0. The program is run with the given `args` and `stdin`. Its
  stdout must equal the `//| ` lines byte for byte, its exit status must equal `exit` (or the
  process must die with SIGABRT when `abort` is given), and stderr must contain the `stderr`
  substring when one is given.
- `fail`: the compiler must exit 1. Every line carrying `//! error:` must produce a diagnostic on
  that line whose text contains the substring, and no diagnostic may appear on an unannotated
  line. `error-any` accepts a diagnostic on any line.
- Expected outputs were computed by hand from `notes/decisions.md` before any compiler existed;
  a disagreement between a test and the compiler is a bug in one of the two, decided by the notes.

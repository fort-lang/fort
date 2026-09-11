# fort toolchain

This document specifies the `fort` compiler's command line, build pipeline, build modes,
diagnostics, C runtime, code generation contract and test conventions for v1. It implements D14
together with D9.10, D10, D11, D12, D18 and the run-time side of D17. Where it disagrees with
`decisions.md` or `grammar.md`, those files win (D1.2).

Sections: 1 Command line; 2 Build pipeline; 3 Build modes; 4 Diagnostics; 5 The C runtime;
6 Code generation contract; 7 Testing; 8 Compiler architecture sketch; 9 Not in v1.

## 1. Command line

```sh
fort [options] entry.ft
```

Exactly one entry file is given; the first argument that does not start with `-` is the entry
file (D14.1). Options and the entry file may appear in any order.

| Option              | Meaning                                                    | Default    |
|---------------------|------------------------------------------------------------|------------|
| `-o <file>`         | output path                                                | see below  |
| `-S`                | stop after emitting the LLVM IR module (D19.1)             | off        |
| `-c`                | stop after compiling it to an object file                  | off        |
| `-I <dir>`          | add a search root after the entry directory; repeatable    | none       |
| `--std-dir <dir>`   | standard library directory                                 | see below  |
| `--release`         | release mode (section 3, D11.1)                            | checked    |
| `--no-bounds-check` | remove index and span checks (D10.6); unsafe               | checks on  |
| `-l<lib>`           | passed to the linker as given; repeatable, in order        | none       |
| `--cc <path>`       | the clang that compiles and links the IR (D14.3)           | `clang`    |
| `--target <triple>` | passed to `--cc` as `--target=<triple>` (D14.1)            | see below  |
| `-Xcc <arg>`        | passed to `--cc` verbatim, after the arguments below       | none       |
| `--check`           | run the front end only and stop (D20.1)                    | off        |
| `--json`            | write the check document to stdout (D20.2), needs `--check`| off        |
| `--index`           | fill the document's identifier index (D20.3)               | off        |
| `--help`            | print the usage line and exit 0                            |            |
| `--version`         | print the compiler version and exit 0                      |            |

- `-o`, `-I`, `--std-dir`, `--cc`, `--target` and `-Xcc` take the following argument; `-l<lib>`
  is one argument. `-I` roots are searched in command-line order (D9.2) and `-Xcc` arguments are
  passed in command-line order. The last `-o`, `--std-dir`, `--cc` and `--target` win.
- `--cc` must name a clang, since nothing else reads LLVM IR (D14.1, D19.1). The
  default target triple is `x86_64-linux-gnu` (D14.1).
- The default output is `a.out`; with `-c` it is `<entry>.o` and with `-S` `<entry>.ll` (D14.1),
  where `<entry>` is the entry file's base name without `.ft`, placed in the current directory as
  `cc` does.
- The default standard library directory is `$FORT_STD_DIR` when set, else `std` relative to
  the directory containing the `fort` binary.
- `-S` and `-c` together stop at the IR. With `-S`, `-l`, `--cc`, `--target` and `-Xcc` are
  unused.
- `--release` and `--no-bounds-check` are independent and may be combined.
- `--check` runs steps 1 to 3 of section 2 and stops there: no IR, no `--cc`, no temporary, and
  the entry module need not define `main`, since it is a module under inspection and not a
  program (D20.1, D8.6). With it, `-o`, `-S`, `-c`, `-l`, `--cc`, `--target` and `-Xcc` are
  unused. `--json` replaces the text diagnostics of section 4 with the document of section 4.1 on
  stdout and is a usage error without `--check`, since a build spawns a `--cc` that inherits
  stdout and could not promise a complete document or nothing (D20.2). `--index` fills the
  document's `"symbols"` array with the identifier index of section 9.1 and implies `--check` and
  `--json`, so `fort --index main.ft` is the whole of what an editor runs (D20.3).
- The entry file's directory is always a root and the current directory never is (D9.2).

Exit status (D14.1):

| Status | Meaning                                                                          |
|--------|----------------------------------------------------------------------------------|
| 0      | success                                                                          |
| 1      | at least one compile error was reported (section 4)                              |
| 2      | usage error, unreadable entry file, internal error, or failure of `--cc`         |

Usage errors, internal errors and failures of `--cc` are reported as `fort: error: <message>`
on stderr, for example `fort: error: cannot read 'x.ft': No such file or directory` or
`fort: error: cc failed with status 1`. `fort` with no arguments prints one usage line and exits
with 2; `--help` prints that line and then the table above, and exits 0.

These are all the `fort: error: <message>` texts, each of them exit status 2 (D14.1). Five report a
command line the compiler cannot use and are followed by the usage line: `missing argument for
option '<opt>'`, `unexpected argument '<arg>'` (a second entry file), `unknown option '<opt>'`, `no
entry file` and `--json requires --check` (D20.2). Four report an operation of section 2 that
failed, with the system's error text as `<reason>`: `cannot read '<file>': <reason>` (the entry
file), `cannot write '<file>': <reason>` (the LLVM IR module), `cannot create a temporary directory
in '<dir>': <reason>` (`mkdtemp` under `$TMPDIR`) and `cannot run '<cc>': <reason>` (`--cc` could
not be started). Two report the outcome of `--cc`: `cc failed with status <n>` and `cc failed with
signal <n>`. The last two are the compiler's own failures: `internal error: <what>` and `out of
memory`.

```sh
fort main.ft -o main                          # build ./main in checked mode
fort -S main.ft                               # write main.ll and stop
fort -c main.ft                               # write main.o and stop
clang --target=x86_64-linux-gnu -o main main.o "$FORT_STD_DIR/fort_rt.o"   # link -c by hand
fort --release -o main main.ft                # release mode
fort --release --no-bounds-check -o bench main.ft
fort -I lib -I vendor -lm main.ft             # extra roots, link libm
fort --cc clang-18 --target x86_64-linux-gnu -Xcc -fuse-ld=lld main.ft
fort --check lib/util.ft                      # check that module and its imports, print nothing
fort --check --json main.ft                   # one JSON document on stdout, for an editor
fort --index main.ft                          # the same document with the identifier index
FORT_STD_DIR=/opt/fort/std fort main.ft
```

The only environment variables read are `FORT_STD_DIR` (D14.1) and `TMPDIR`, which locates the
temporary directory for the intermediate IR file (D19.1).

## 2. Build pipeline

Compilation is whole-program (D9.10):

1. Read the entry file and derive its module path and root (exit 2 if unreadable, 1 if the base
   name contains a `.` or a `:`, the two characters a module path is spelled with, which would
   let it collide with that module's symbols). The base name need not otherwise be an identifier:
   the entry file is named on the command line rather than reached by an import path, so
   `007_case.ft` is the module `007_case` and nothing can import it, and `my-app.ft` is legal too
   (D9.1 as amended).
2. Parse it; resolve each import (module-system.md 2 and 3); parse each newly reached module
   until the closure is complete; reject cycles and duplicate identities (exit 1).
3. Check every module in dependency order, imported modules first (exit 1).
4. Emit one LLVM IR module for the closure to `<tmp>/<entry>.ll` (D19.1), or to the `-S` output
   and stop.
5. Run `<cc>` over that module once: it compiles and links in one invocation (D14.3), exit 2 on
   failure. The line is

   ```sh
   clang --target=x86_64-linux-gnu -O1 -fPIE -pie -Wno-override-module \
       -o <out> <tmp>/<entry>.ll <std-dir>/fort_rt.o <-l options> <-Xcc args>
   ```

   with `-O2` in place of `-O1` under `--release` (D14.3), and `-c` before `-o`, no `-pie`, no
   runtime object and no `-l` for `-c`. That clang finds the cross sysroot, its `Scrt1.o`,
   `crti.o` and `crtn.o` and `x86_64-linux-gnu-ld` by itself, so no `--sysroot`,
   `--gcc-toolchain` or `-fuse-ld` is needed; `-Wno-override-module` silences the warning about
   the module's own target triple, and `-x ir` must not be passed because `-x` is sticky and
   would also treat `fort_rt.o` as IR.
6. Remove the temporary directory.

`--check` stops after step 3 (D20.1): it emits no module, creates no temporary directory, runs no
`--cc`, and does not apply the entry-point rule of D8.6, since the file it is given is a module
under inspection rather than a program.

- The temporary directory comes from `mkdtemp` under `$TMPDIR` (default `/tmp`) and is removed
  whether or not `--cc` succeeded.
- `--cc` is invoked with exactly the arguments shown, in that order; `-fPIE -pie` and the
  position-independent code the IR compiles to make the output a position-independent executable
  (D14.3, D16).
- Every emitted module passes `opt -passes=verify` (D19.1). `test/ir/*.ll` are hand-written
  modules in the form the compiler emits and `test/pipeline_test.sh` runs this pipeline over
  them; the language-test harness verifies the module of every test that compiles
  (`run_tests.py --verify-ir`, section 7.3).
- An object from `-c` contains the whole program except the runtime; linking it needs
  `<std-dir>/fort_rt.o` and nothing else (D9.10).

Where things live: `<std-dir>/*.ft` holds the standard library modules of D13.2 (`std::sys`,
`std::libc`, `std::mem`, `std::io`, `std::str`, `std::strbuf`, `std::vec`, `std::strmap`,
`std::math`) as source, compiled with every program that imports them; `<std-dir>/fort_rt.o` is
the C runtime object, built from `runtime/fort_rt.c` by the compiler's own build; `<bindir>/fort` is
the compiler, and `<bindir>/std` its fallback `--std-dir`. Only modules in the import closure
are read (module-system.md 10).

## 3. Build modes

Two modes (D11.1); `--no-bounds-check` is an orthogonal switch (D10.6). The compiler itself
optimizes nothing in v1 and emits the same shape of IR in both modes; the only difference besides
the checks below is that `--cc` compiles the module with `-O2` instead of `-O1` (D14.3).

| Check                                              | checked | `--release` | `--no-bounds-check` |
|----------------------------------------------------|---------|-------------|---------------------|
| signed overflow: `+ - *`, unary `-`, `++ --`, `+= -= *=` | trap | wrap    | unchanged           |
| unsigned overflow, same operators                  | trap    | wrap        | unchanged           |
| `+% -% *%`, `+%= -%= *%=` (D11.2)                  | wrap    | wrap        | unchanged           |
| shift count negative or `>=` width (D11.1)         | trap    | masked      | unchanged           |
| `/ %` by zero, `MIN / -1`, `MIN % -1` (D6.13)      | trap    | trap        | unchanged           |
| index out of range (D6.8)                          | trap    | trap        | removed             |
| span bounds `0 <= lo <= hi <= len` (D6.9)          | trap    | trap        | removed             |
| `new`: negative count, size overflow, no memory    | trap    | trap        | unchanged           |
| store over a live `own` value (D17.11)             | trap    | no check    | unchanged           |
| `assert(cond)` (D12.2)                             | trap    | trap        | unchanged           |
| `panic(msg)`                                       | trap    | trap        | unchanged           |
| constant index or fold out of range (D4.4, D6.8)   | compile error in every mode                 |
| `cast` (D3.14)                                     | never traps in any mode                     |

"Trap" is the runtime error contract of D11.4: flush, one line on stderr, `abort()`. "Wrap" is
two's complement; "masked" means `count & (width - 1)`; "no check" means the store happens and
the allocation the old value designated leaks (D17.11). Programs must not rely on wrapping or
trapping for correctness (D11.1); the wrapping operators exist for code that needs wrap-around in
both modes. `p[lo..hi]` on a raw pointer is never checked (D6.9), and the undefined behaviors of
D10.7 are undefined in every mode.

## 4. Diagnostics

Compile-time diagnostics (D14.2) are written to stderr, one per line:

```sh
<file>:<line>:<col>: error: <message>
<file>:<line>:<col>: note: <message>
```

- `<file>` is the path the compiler opened: the entry file as given on the command line, an
  imported module as `<root>/<relative path>` with the root as given (`-I lib` gives
  `lib/util.ft`; the entry directory is the entry path's directory part, or nothing).
- `<line>` is 1-based; `<col>` is the 1-based column of the offending token's first byte, a tab
  counting as one column. A `note:` follows the `error:` it belongs to, with its own position.
- A position is the start of a range (D20.4): the diagnostic is about the bytes from `<line>`
  and `<col>` to one past the last byte of the construct's last token, and every syntax-tree
  node carries that range and the range of its name. The text form above prints the start only,
  so an end never appears in a diagnostic line.
- An error without a position in the file, such as a missing `main`, uses `1:1` (D14.2). The
  entry file's base name is not one of these: it need not be a valid module name at all (D9.1,
  `module-system.md` 2).
- A lexical error is reported and lexing resumes at the start of the next line (D14.2): the line
  the error stands on is dropped whole, the tokens already lexed on it included, so a file reports
  at most one lexical diagnostic per line and the token stream covers the rest of the file and
  ends at the end of it. The parser reads those tokens and recovers from what the missing line
  broke, which costs it nothing when the line was a statement of its own and costs it the
  enclosing construct when the line carried a `{`, a `(` or a declaration header. The
  declarations after a half-typed literal therefore still reach the syntax tree and an editor
  keeps its index of them; a file that reported anything is not checked.
- After a syntax error the parser reports it and unwinds the construct it was parsing, reporting
  nothing more until it reaches a recovery point: the statement loop of a block or of a `case`
  clause, the clause loop of a `switch`, the field loop of a struct body, or the declaration loop
  of the module. There it skips what is left
  of the failed construct, keeps the skipped tokens as an error node that every later pass skips,
  and parses on, so a file reports one diagnostic for each construct that failed (D14.2). A
  parameter list, an argument list, an import item list and an enum body have no recovery point
  of their own and recover through the construct that encloses them.
- A skip runs to the end of the failed construct. It consumes at least one token, so it always
  makes progress, and then, outside the `(` and `[` the construct left open, consumes a `;`, and
  consumes the `}` that closes a brace it saw opened; it stops before a `}` it did not see opened
  and before a token that starts a top-level declaration, the end of the file ending every skip.
  Which other tokens stop it depends on the recovery point: a skip that stands where a statement
  or a clause would stops before `case` and `default`, since a switch body holds nothing else,
  and one that stands where a statement would also stops before a statement keyword (`if while
  for switch defer return break continue do`); a struct field is skipped to its `;` or to the `}`
  of the body, and a skip at the top level, where a `}` closes nothing, consumes one. Two
  lookaheads settle the braces a skip did not see opened: a `}` that a `;` follows closes a brace
  initializer or a struct literal the construct opened, since no block is followed by a `;`
  (D7.3), and a `}` that a `)` or a `]` follows stands inside a bracket the construct left open,
  where it closes nothing; both go with the skipped region. A `{` a construct left open is not
  counted: which brace it was meant to be is not decidable from the tokens, so the skip leaves
  the next `}` to the body it belongs to and drops the unclosed construct instead.
- A block, a `case` clause, a struct body and an enum body also end where a top-level declaration
  starts: at `struct`, `enum`, `extern`, `import`, or a `fn` whose return type is followed by an
  identifier (a `fn` at statement level is a function type, whose return type is followed by `(`,
  D3.10). A file with a missing `}` therefore reports `expected '}', found 'fn'` once, at the
  declaration that follows it, instead of one diagnostic per following declaration (D14.2). The
  mirror holds for the `{` of a function body, a struct body or an enum body, which a complete
  declaration header precedes: a missing one is reported once and the body is read as though it
  were there, instead of the body being read as declarations.
- A file reports at most 20 diagnostics, its lexical and its syntax errors counted against one
  budget, and the parser drops an error that starts where the one reported before it started; the
  lexer and the parser go on silently after either, so the tokens and the tree cover the whole
  file whatever was reported (D14.2). Diagnostics come out in the order they are reported,
  which is source order except where a construct is judged after its parts are parsed: the
  features the bootstrap lacks (toolchain.md 7.3) are reported that way, so a `do`-`while` whose
  body has a mistake reports the body's line first. Nothing reads a diagnostic's position
  relative to another's: the test harness matches each `error:` line to the annotation on its own
  line (D14.5).
- Every module of the closure is checked in dependency order (D14.2); a file with a syntax error
  is parsed whole and not checked. A declaration whose check failed has the error type, which
  silences every later diagnostic involving it, so an importer sees only its own errors.
- The compiler emits no warnings (D14.2): unused imports, unused variables and statements after
  a terminating statement are not diagnosed.

```sh
main.ft:7:5: error: cannot assign to immutable 'x'
main.ft:3:9: note: 'x' declared here
util.ft:12:23: error: expected ';'
```

Runtime diagnostics (D11.4) use the same position syntax, followed by `abort()`, so the shell
reports status 134:

```sh
main.ft:12:14: runtime error: index 5 out of range for length 3
main.ft:16:9: runtime error: overwriting owned value
main.ft:20:5: panic: queue empty
main.ft:31:5: assertion failed: n > 0
```

The position of a runtime error is the operator token of the failing operation (`[`, `+`, `-`,
`*`, `/`, `%`, `<<`, `>>`, `++`, `--`, a compound-assignment operator, unary `-`, the `=` of an
assignment for the overwrite check of D17.11) or the builtin name for `new`, `assert` and
`panic`. The assertion text is the verbatim source text of the argument. Runtime messages are
listed in section 5.

### 4.1 The check document (D20.2)

`fort --check --json entry.ft` writes one JSON document to stdout instead of the
compile-time lines of this section and no text diagnostic; the `fort: error:` lines of section 1
stay on stderr. The document is built whole and written with one `fwrite`, so stdout holds a
complete document or nothing: a client reads exit 0 or 1 with a document as a verdict and exit 2
with empty stdout as a crash. The document is one line ended by a newline; it is shown here over
several:

```json
{"version": 1,
 "files": ["main.ft", "util.ft"],
 "diagnostics": [{"file": "main.ft", "line": 7, "col": 5, "end_line": 7, "end_col": 6,
                  "severity": "error", "message": "cannot assign to immutable 'x'",
                  "notes": [{"file": "main.ft", "line": 3, "col": 9, "end_line": 3, "end_col": 10,
                             "message": "'x' declared here"}]}],
 "symbols": []}
```

- `"version"` is 1 for this form; a client that reads another number stops.
- `"files"` lists every file the compiler read, in that order, each spelled as the `<file>` of a
  diagnostic is, so a client knows which files it may clear stale diagnostics for. A file that was
  reached but could not be read is not listed.
- A diagnostic carries the whole range of D20.4: `"line"` and `"col"` are the start the text form
  prints, `"end_line"` and `"end_col"` are one past its last byte, all 1-based with a tab counting
  as one column. Columns are byte columns; converting them to UTF-16 code units is the client's
  job. `"severity"` is `"error"` on every diagnostic the compiler reports as one, there being no
  warnings in v1 (D14.2, D20.2).
- The `note:` lines of an error are nested in its `"notes"`, in order, each with its own range; a
  note carries no severity there. A note that follows no error has no error to nest under and
  stands as a diagnostic of its own, whose `"severity"` is `"note"`.
- `"symbols"` is the identifier index of section 9.1, which `--index` fills and which is the
  empty array without it (D20.3).
- Diagnostics appear in the order they were reported, which is the order of the text form.
- `--json` is a usage error without `--check`: the `--cc` a build spawns inherits stdout, so the
  guarantee above is the check mode's alone (D20.2). The document is written only when the
  compiler reaches a verdict: a usage error, a toolchain error or an internal error leaves stdout
  empty and exits 2 (D14.1).

## 5. The C runtime

The runtime is `runtime/fort_rt.c`, compiled to `<std-dir>/fort_rt.o`; it is C and permanent
(D13.1). It owns process start and exit (D11.6), heap allocation (D10.2, D10.3), the
runtime-error and panic paths (D11.4, including the ownership overwrite check of D17.11),
formatting and buffering for the print family (D11.5, D11.7, D12.2, D18), and the program
arguments for `std::sys`. The compiler emits calls to the entry points below and declares each
one it uses in the module with the prototype shown, mapped to IR types by section 6 item 8 and
with `cold noreturn nounwind` on the `_Noreturn` ones; the standard library declares the ones it
needs with `extern fn` (module-system.md 7).

### 5.1 Entry points

`loc` abbreviates `const char* file, uint32_t line, uint32_t col`. Every `fort_rt_fail_*`
function, `fort_rt_panic`, `fort_rt_assert_fail` and `fort_rt_exit` is `_Noreturn`.

```c
// Types shared with generated code.
struct fort_string { const char* ptr; uint64_t len; };    // fort string, D3.7
struct fort_span   { void* ptr; uint64_t len; };          // fort T@, D3.5
struct fort_rt_enum_member { int32_t value; const char* name; };

// Allocation (D10.2, D10.3). fort_rt_new returns zeroed storage for count elements
// of elem_size bytes, at least one byte so the result is never null (new(T, 0) is
// non-null); an overflowing product or a failed calloc is a runtime error at loc.
// fort_rt_del is free(p); a null p is a no-op. Ownership (D17) is erased: the
// runtime sees plain pointers, and the compiler zeroes a del or move operand
// itself (section 6, items 17 and 18).
void* fort_rt_new(uint64_t elem_size, uint64_t count, loc);
void  fort_rt_del(void* p);

// Failures (D11.4): flush every buffer, write one line to stderr, abort().
// Values arrive sign-extended to 64 bits; hi is len for e[lo..]; type is the
// NUL-terminated name of the shifted operand's type; text is the NUL-terminated
// source text of the assert argument. fail_div_overflow is MIN / -1 and MIN % -1;
// fail_alloc_count is new(T, n) with a negative signed n; fail_overwrite is an
// assignment to an own reference-typed lvalue whose current value is not zero
// (D17.11), emitted in checked builds only.
void fort_rt_fail_bounds(int64_t index, uint64_t len, loc);
void fort_rt_fail_span(int64_t lo, int64_t hi, uint64_t len, loc);
void fort_rt_fail_overflow(loc);
void fort_rt_fail_shift(int64_t count, const char* type, loc);
void fort_rt_fail_div_zero(loc);
void fort_rt_fail_div_overflow(loc);
void fort_rt_fail_alloc_count(int64_t n, loc);
void fort_rt_fail_overwrite(loc);
void fort_rt_panic(const char* ptr, uint64_t len, loc);
void fort_rt_assert_fail(const char* text, loc);

// Printing (D11.5, D11.7, D12.2): format one value per D11.7 and append it to the
// buffer of fd. A float arrives in its own type and prints with the shortest digits
// that round-trip in that type, which the runtime obtains from the C library and
// lays out itself (D18); the two entry points are the only float ones (D18.4).
// fort_rt_flush writes out one buffer (io.close and io.flush call it);
// fort_rt_flush_all writes out every buffer, at exit and before every failure.
void fort_rt_print_i64(int32_t fd, int64_t v);
void fort_rt_print_u64(int32_t fd, uint64_t v);
void fort_rt_print_f32(int32_t fd, float v);
void fort_rt_print_f64(int32_t fd, double v);
void fort_rt_print_bool(int32_t fd, uint8_t v);
void fort_rt_print_char(int32_t fd, uint8_t c);
void fort_rt_print_ptr(int32_t fd, const void* p);
void fort_rt_print_str(int32_t fd, const char* ptr, uint64_t len);
void fort_rt_print_enum(int32_t fd, int32_t v, const struct fort_rt_enum_member* m,
                        uint64_t n);
void fort_rt_flush(int32_t fd);
void fort_rt_flush_all(void);

// Process (D11.6, D8.6). main calls fort_rt_args_init, which builds the args
// span from argv (one string per argument, NUL-terminated since it is the argv
// byte sequence itself), then fort_entry, then fort_rt_flush_all, and returns
// status & 0xFF. fort_entry is emitted by the compiler (module-system.md 11).
// The args span lives for the whole process and std::libc declares
// fort_rt_args_ptr and fort_rt_args_len for sys.args(); fort_rt_args_init is
// called by main only and exists so the native runtime object, built without
// main, can be tested. fort_rt_exit flushes every buffer, then
// exit(status & 0xFF); std::libc declares it for sys.exit.
int main(int argc, char** argv);
int32_t fort_entry(const struct fort_span* args);
void fort_rt_args_init(int argc, char** argv);
const struct fort_string* fort_rt_args_ptr(void);
uint64_t fort_rt_args_len(void);
void fort_rt_exit(int32_t status);
```

The float printers are the one place where the runtime uses the C library to format a value.
`fort_rt_print_f64` asks `snprintf("%.*e", ...)` for one significant digit, then two, and so on,
and keeps the first length whose text `strtod` reads back as the value; 17 digits for `f64` and
9 for `f32` (`strtof`) always read back, so the search ends. `printf` returns the nearest decimal
of the length asked for, which is not always the one to keep: for a normal power of two above the
minimum normal the values that read back as it reach half an ulp above and only a quarter below,
the binade below being coarser, so the nearest decimal can fall short of that interval while the
next one up falls inside it, and the runtime tries that neighbour before lengthening. The
neighbour below never needs trying, the gap below a float never being wider than the gap above.
This asks two things of the C library that the standard permits but does not require and glibc
provides: a correctly rounded `printf` (ties to even) and a correctly rounded `strtod`, down to
the subnormals, where it also reports `ERANGE`, which the runtime ignores because only the value
matters. The runtime lays the digits out itself. A rewrite of the runtime in fort must reproduce
the text D18.2 fixes; it need not reproduce this search.

This list is complete (D11.6): the standard library declares no other `fort_rt_*` symbol. It
declares `fort_rt_args_ptr`, `fort_rt_args_len`, `fort_rt_flush`, `fort_rt_flush_all` and
`fort_rt_exit` in `std::libc` (`stdlib.md` 3) and reaches `errno` through libc's
`__errno_location`.

### 5.2 Messages

Each failure writes exactly one line, after `fort_rt_flush_all`, then calls `abort()` (D11.4):

| Entry point                 | Line after `<file>:<line>:<col>: ` (D11.4)                 |
|-----------------------------|------------------------------------------------------------|
| `fort_rt_fail_bounds`       | `runtime error: index 5 out of range for length 3`         |
| `fort_rt_fail_span`         | `runtime error: span bounds 2..7 out of range for length 3`  |
| `fort_rt_fail_overflow`     | `runtime error: integer overflow`                          |
| `fort_rt_fail_shift`        | `runtime error: shift count 64 out of range for i64`       |
| `fort_rt_fail_div_zero`     | `runtime error: division by zero`                          |
| `fort_rt_fail_div_overflow` | `runtime error: division overflow`                         |
| `fort_rt_fail_alloc_count`  | `runtime error: negative allocation count -1`              |
| `fort_rt_new` (overflow)    | `runtime error: allocation size overflow`                  |
| `fort_rt_new` (no memory)   | `runtime error: out of memory`                             |
| `fort_rt_fail_overwrite`    | `runtime error: overwriting owned value`                   |
| `fort_rt_panic`             | `panic: <message bytes>`                                   |
| `fort_rt_assert_fail`       | `assertion failed: <expression text>`                      |

`<file>` is as in section 4. Numbers in messages are decimal; the index, the span bounds and
the allocation count are printed as signed values. Falling off the end of a `noreturn` function
executes the trap of section 6 item 20 (D8.5, D19.7): the process dies with SIGILL and no
message.

### 5.3 Buffering

| fd    | Used by                              | Policy (D11.5)                                   |
|-------|--------------------------------------|--------------------------------------------------|
| 1     | `print`, `println`, `fprint(1, ...)` | buffered; flushed when full, at exit, on failure |
| 2     | `eprint`, `eprintln`                 | unbuffered; every call writes immediately        |
| other | `fprint(fd, ...)`, `fprintln`        | one buffer per descriptor, same policy as 1      |

`fprint(1, ...)` shares the stdout buffer with `print` (D11.5). The runtime tells descriptors
apart by number alone, so `fprint(2, ...)` behaves as `eprint`. An `extern` write to a
descriptor bypasses the buffers; a program that mixes the two on one descriptor flushes first
(`io.flush`, D11.5). A write error on any descriptor is ignored; the bytes are dropped. The
buffer size is the runtime's choice.

## 6. Code generation contract

This section is normative for the compiler. The LLVM IR module it emits must satisfy every item
and must pass `opt -passes=verify` (D19.1). The two examples at the end are `test/ir/hello.ll`
and `test/ir/abort.ll` byte for byte; the pipeline test builds and runs them (section 2).

1. **Form and module header.** One textual module (`.ll`, LLVM 18 syntax, opaque pointers) holds
   the whole program (D9.10, D19.1) and is built by appending text in one forward pass. It
   begins with

   ```llvm
   target triple = "x86_64-unknown-linux-gnu"
   ```

   in clang's normalized spelling, and carries no `target datalayout`, no `!llvm.module.flags`,
   no `!llvm.ident`, no `source_filename` and no comments (D19.1): clang derives the layout from
   the triple, and position independence comes from the `--cc` line (section 2), not from module
   flags. Sections appear in this order and nowhere else: the triple, the named types, the
   module-level globals and constants (D7.10), the function definitions, the private data, the
   declarations, the attribute groups. Forward references to globals are legal in `.ll`, which
   is what lets one pass emit a function before the data it names. Naming and ordering inside
   each section follow D19.5, so the text is a function of the program alone.

2. **Type mapping** (D19.2). The value type is what a temporary holds; the memory type is what
   an `alloca`, a global or a field holds.

   | fort                     | value type        | memory type     | notes                        |
   |--------------------------|-------------------|-----------------|------------------------------|
   | `i8 i16 i32 i64`         | `i8 i16 i32 i64`  | same            | `sdiv`, `sext`, `icmp slt`   |
   | `u8 u16 u32 u64`         | `i8 i16 i32 i64`  | same            | `udiv`, `zext`, `icmp ult`   |
   | `bool`                   | `i1`              | `i8`            | 0 or 1 in memory (D3.3)      |
   | `char`                   | `i8`              | `i8`            | unsigned byte (D3.2)         |
   | `f32`, `f64`             | `float`, `double` | same            | D3.1                         |
   | `T*`, `void*`, `fn R(P)` | `ptr`             | `ptr`           | opaque (D3.10, D3.11)        |
   | `T[N]`                   | none              | `[N x T]`       | outside in (D3.6)            |
   | `T@`, `string`           | none              | `%fort.span`    | `type { ptr, i64 }`          |
   | `struct a::b::s`         | none              | `%struct.a.b.s` | fields in order, no `packed` |
   | `enum`                   | `i32`             | `i32`           | D3.9                         |
   | `void`                   | `void`            | none            | result type only             |

   Signedness is in the instruction, never in the type (D3.1), and an array type nests outside
   in, so `i32[3][4]` is `[3 x [4 x i32]]` (D3.6). Every load of a `bool` place is a
   `load i8` and a `trunc`, every store a `zext` and a `store i8`, so a `bool` field has C's
   `_Bool` layout and `fort_rt_print_bool(int32_t, uint8_t)` needs no special case; the
   `trunc`/`zext` pairs disappear in the optimizer. One `%fort.span` serves every span and
   `string`, because with opaque pointers `i32@`, `u8@` and `string` have identical IR (D3.5,
   D3.7). `%fort.span` and `%fort.enum_member = type { i32, ptr }` are emitted in every module,
   used or not, so the emitter tracks nothing; unused named types are legal.

3. **Aggregates live in memory** (D19.3). Only scalars are SSA values: a struct, fixed array,
   span or `string` always occupies a place, is copied with `llvm.memcpy.p0.p0.i64`, zeroed
   (`{}`, `del`, `move`) with `llvm.memset.p0.i64`, and reached field by field or element by
   element with `getelementptr`. The emitter never loads or stores an aggregate as one value and
   never writes `insertvalue` or `extractvalue` on one; its only `extractvalue` takes apart the
   `{iN, i1}` of an overflow intrinsic (item 15). Exactly three `getelementptr` shapes exist,
   and all three always carry `inbounds`: `inbounds <arrty>, ptr %a, i64 0, i64 %i` for an
   element of a fixed array, `inbounds <elemty>, ptr %p, i64 %i` for an element reached through
   a pointer or a span's `.ptr`, and `inbounds %struct.x, ptr %s, i32 0, i32 <k>` for a field.
   `inbounds` is always true: an element access is preceded by its bounds check (item 16) and a
   field or header access is in bounds by construction, and `--no-bounds-check` removes the
   check's branch, never the `inbounds` (D10.6). This is D9.9's model spelled in IR.

4. **Symbols, linkage, visibility** (D9.7). The dotted names of D9.7 are quoted:
   `@"main.add"`, `@"std.io.read_file"`, `@"main.LIMIT"`; quoting is uniform and does not change
   the ELF symbol, which is `main.add`. C names (`extern` declarations, `fort_rt_*`,
   `fort_entry`) are unquoted. Fort functions, constants and globals are `dso_local` with the
   default external linkage (D9.6), so fort-to-fort calls are direct and fort data is addressed
   PC-relative; `extern` and `fort_rt_*` symbols carry no `dso_local` and go through the
   procedure linkage and global offset tables. Private data (`@.str.N`, `@.file.N`,
   `@.enum.<path.name>`) is `private unnamed_addr`.

5. **Data emission.** Private data follows the function definitions, `@.file.N` constants before
   `@.str.N` before `@.enum.*` (D19.5):

   ```llvm
   @.file.0 = private unnamed_addr constant [9 x i8] c"abort.ft\00", align 1
   @.str.0  = private unnamed_addr constant [7 x i8] c"before\00", align 1
   @"main.LIMIT"   = dso_local constant i32 100, align 4
   @"main.TABLE"   = dso_local constant [2 x ptr] [ptr @"main.f", ptr @"main.g"], align 8
   @"main.counter" = dso_local global i64 0, align 8
   @"main.origin"  = dso_local global %struct.main.point zeroinitializer, align 4
   ```

   - A string literal is `[len + 1 x i8]` with the trailing NUL that `len` excludes (D3.7);
     non-printable bytes are written as `\XX` hex pairs.
   - `constant` for module constants (D7.10) and for private data, `global` for `mut` globals;
     an all-zero `global` lands in `.bss` and a nonzero one in `.data` by itself, and a
     `constant` whose initializer holds a relocation lands in `.data.rel.ro` because the module
     is compiled as position-independent code (item 6).
   - `unnamed_addr` marks private data only: a named fort constant keeps its address significant
     because `&CONST` is expressible (D6.7).
   - Every global, `alloca`, `load` and `store` carries an explicit `align N` from D3.1 and
     D3.8.
   - Only referenced private data is emitted: a program with no check has no `@.file.N`
     (`test/ir/hello.ll`), and an enum table exists only if some `print` of that enum type is
     compiled (item 21).

6. **Position independence** (D14.3, D16). Nothing in the IR expresses it: `dso_local` (item 4)
   and the `-fPIE -pie` of section 2 give RIP-relative data, direct fort-to-fort calls and
   linkage-table calls to `extern` and runtime symbols. The one requirement the module carries
   is that an address is never an integer constant derived from a symbol; addresses appear only
   as `ptr` values and as `ptr` constants in initializers.

7. **Calling convention, fort to fort** (D9.9). Scalars (integers, `bool`, `char`, enums,
   pointers, function pointers, floats) are ordinary parameters and results and LLVM applies
   System V. A struct, fixed array, span or `string` argument is a plain `ptr` parameter: the
   caller allocates a copy in its entry block, `llvm.memcpy`s into it and passes its address,
   and `byval` is never used, since it would mean a callee-visible copy on the stack rather than
   the pointer in the integer slot D9.9 requires. An aggregate result is a leading
   `ptr sret(%T) %ret.sret` parameter on a function whose result type is `void`; the pointer
   arrives in `rdi` and is echoed in `rax`, which is D9.9's ABI. The attribute is written on the
   definition and not at the call site, which passes the destination as a plain `ptr`: on
   x86-64 the two are identical and only tail-call eligibility can tell them apart (D9.9).
   A span or `string` is one hidden pointer and is never split into two scalars, so
   `fort_entry`'s C prototype stays literally true (D11.6). `bool`, `char`, `u8` and `u16`
   parameters and results carry `zeroext` and `i8` and `i16` carry `signext`, in fort and extern
   signatures alike, so an extern-legal signature is a valid C callback by construction (D9.9).
   Every fort definition is `define dso_local <ret> @"m.f"(...) #0`, where `#0` is
   `{ nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }`: `nounwind` because fort has
   no exceptions, the frame pointer because it is what a debugger gets without DWARF (section
   9), and `probe-stack` for item 13.

   A call through a function pointer is an ordinary `call` whose callee is the `ptr` value and
   whose function type is written out, because an opaque pointer carries none (D19.2):
   `call i32 (i32, i32) %t0(i32 %t1, i32 %t2)`, and `call void (ptr, i32) %t0(ptr %r.0, i32 3)`
   for an aggregate result. The callee is evaluated before the arguments (D6.3) and every rule
   above holds at that call site unchanged, aggregate arguments and the extension attributes
   included, so it differs from the call of a name only in the callee and that type (D3.10). The
   callee is always a fort function, since an `extern fn` in value position is an error (D3.10):
   an extern is called through the variadic type of item 8, which only its declaration can
   supply.

8. **Extern and runtime declarations** (D9.8). An `extern` function is declared with its C types,
   unmangled, and with a variadic tail, and is called through the matching variadic call type:

   ```llvm
   declare i32 @printf(ptr, ...)
   declare signext i8 @c_narrow(i8 signext, i16 zeroext, ...)
   ```

   ```llvm
     %t10 = call i32 (ptr, ...) @printf(ptr @.str.0) #3
     %t11 = call signext i8 (i8, i16, ...) @c_narrow(i8 signext %t8, i16 zeroext %t9) #3
   ```

   LLVM passes the vector-register count a variadic callee reads exactly when the call-site type
   is variadic, so declaring every extern variadic is what makes a fixed-prototype declaration
   of a variadic C function safe (D9.8); a non-variadic callee ignores that count, so the
   declaration is ABI-identical for it. `#3 = { nobuiltin }` on every extern call site keeps
   LLVM from rewriting a declared symbol into another library call, and is preferred to a
   driver-wide `-fno-builtin`, which would also change how our `llvm.memcpy` and `llvm.memset`
   are lowered. A call through a function pointer is not variadic (D3.10 has no variadic
   function type) and needs no such declaration; it is also never a call of an extern, whose name
   is not a value, so every extern call in the module carries the variadic type above (D3.10).

   The runtime entry points are declared with the C prototypes of section 5.1 and are never
   variadic, whether the compiler emits the call itself or the standard library reached the
   entry point with an `extern fn` (D13.1), which is the rule the paragraph below the intrinsics
   states in full:

   ```llvm
   declare ptr @fort_rt_new(i64, i64, ptr, i32, i32)
   declare void @fort_rt_del(ptr)
   declare void @fort_rt_fail_bounds(i64, i64, ptr, i32, i32) #2
   declare void @fort_rt_fail_span(i64, i64, i64, ptr, i32, i32) #2
   declare void @fort_rt_fail_overflow(ptr, i32, i32) #2
   declare void @fort_rt_fail_shift(i64, ptr, ptr, i32, i32) #2
   declare void @fort_rt_fail_div_zero(ptr, i32, i32) #2
   declare void @fort_rt_fail_div_overflow(ptr, i32, i32) #2
   declare void @fort_rt_fail_alloc_count(i64, ptr, i32, i32) #2
   declare void @fort_rt_fail_overwrite(ptr, i32, i32) #2
   declare void @fort_rt_panic(ptr, i64, ptr, i32, i32) #2
   declare void @fort_rt_assert_fail(ptr, ptr, i32, i32) #2
   declare void @fort_rt_print_i64(i32, i64)
   declare void @fort_rt_print_u64(i32, i64)
   declare void @fort_rt_print_f32(i32, float)
   declare void @fort_rt_print_f64(i32, double)
   declare void @fort_rt_print_bool(i32, i8 zeroext)
   declare void @fort_rt_print_char(i32, i8 zeroext)
   declare void @fort_rt_print_ptr(i32, ptr)
   declare void @fort_rt_print_str(i32, ptr, i64)
   declare void @fort_rt_print_enum(i32, i32, ptr, i64)
   declare void @fort_rt_flush(i32)
   declare void @fort_rt_flush_all()
   declare void @fort_rt_args_init(i32, ptr)
   declare ptr @fort_rt_args_ptr()
   declare i64 @fort_rt_args_len()
   declare void @fort_rt_exit(i32) #2
   ```

   The intrinsics are declared with the spellings LLVM 18 prints, in this fixed order, one per
   type actually used and none otherwise:

   ```llvm
   declare void @llvm.memcpy.p0.p0.i64(ptr noalias nocapture writeonly,
       ptr noalias nocapture readonly, i64, i1 immarg) #5
   declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #6
   declare { i8, i1 } @llvm.sadd.with.overflow.i8(i8, i8) #4
   declare i32 @llvm.fptosi.sat.i32.f64(double) #4
   declare void @llvm.trap() #7
   ```

   The `llvm.memcpy` declaration is one line in the module and is wrapped here only to fit the
   page. The order is: `llvm.memcpy.p0.p0.i64`, then `llvm.memset.p0.i64`, then the overflow
   family in the order `sadd ssub smul uadd usub umul` and, within each, the widths
   `i8 i16 i32 i64` (item 15), then `llvm.fptosi.sat.i<N>.f32`, `llvm.fptosi.sat.i<N>.f64`,
   `llvm.fptoui.sat.i<N>.f32` and `llvm.fptoui.sat.i<N>.f64` by ascending target width (item
   12), then `llvm.trap` (item 20). The parameter attributes shown are part of the spelling.

   Only referenced declarations are emitted, in a fixed order (D19.5): `extern` C functions in
   first-use order, then the runtime entry points in the order of section 5.1 above, then the
   intrinsics in the order of the table above, each group separated from the next by a blank
   line. A symbol is declared exactly once, so an `extern fn` naming a runtime entry point
   (`fort_rt_flush`, `fort_rt_exit`, the rest of section 5.1 that `std::libc` declares, D13.1)
   is emitted in the runtime group with that group's prototype and attributes and is left out of
   the extern group, variadic tail included. A plain runtime declaration carries no attribute
   group; the `_Noreturn` entry points of section 5.1 carry `#2` (item 14), `fort_rt_exit`
   included, whether the compiler or an `extern fn` brought them in.

9. **Normalization** (D9.8, D19.2). A narrow value is not widened to 32 bits: an `i8` value has
   type `i8` and its width is in the type. The only extensions the emitter produces are the
   `zeroext` and `signext` attributes of item 7, which make LLVM normalize on both sides of a
   call, the `trunc` and `zext` of `bool`'s value and memory types, and the explicit conversions
   of item 12.

10. **Locals, control flow, evaluation order** (D19.4). Every local, scalar parameter copy and
    compiler temporary is an `alloca` in the entry block, before any other instruction, in
    declaration order; nothing is variable-length. Names are fixed by D19.5, per function and
    reset at each definition: `%t<N>` for an instruction result in emission order, `%L<N>` for a
    block in creation order with the entry block always literally `entry`, `%<ident>.<slot>` for
    a local or parameter slot, `%<ident>.in` for an incoming parameter, `%ret.sret` for an
    aggregate result pointer, and `%tmp<K>` from a third counter for a place the compiler
    invents (an aggregate argument copy, a short-circuit slot). A name that embeds a fort
    identifier always contains a dot and an invented one never does, so a local named `tmp` is
    `%tmp.0` and cannot collide with `%tmp0` (D19.5).

    A scalar parameter arrives as `%<name>.in` and is stored into its slot immediately. An
    aggregate parameter is not copied again: its place is the caller-made copy the incoming
    `ptr` designates (item 7), which the callee may write to, since D8.2's by-value rule is
    satisfied by the caller's copy. `fort_entry` is the exception, because its caller is the C
    runtime rather than fort code, which is why item 22 copies the argument span. Control flow
    is explicit blocks: `if`, `while`,
    `for`, `break` and `continue` become `br`; a fort `switch` on an integer, `char` or enum
    becomes an LLVM `switch` with one case per label and a default block (D7.7); `&&`, `||` and
    `?:` short-circuit through a stack slot rather than a `phi`, so the tree walk never has to
    know its predecessors; after a terminating statement the emitter opens a fresh `%L<N>` block
    for the unreachable statements D14.2 allows. Evaluation order needs nothing: LLVM keeps the
    order of side effects and the walk emits calls, loads and stores in source order, `fd` once
    (D6.3, D12.2). Deferred statements are already expanded at each exit by the front end
    (D7.8).

11. **SSA discipline** (D19.4). Because every user-visible value lives in an `alloca`, a
    temporary `%tN` is used only in the block that defines it or in a block that block dominates
    (its own check's continuation and failure blocks). The emitter never carries a value across
    a merge point and never builds a `phi`; the optimizer does. This is what satisfies the
    verifier's dominance rule without any analysis in the compiler.

12. **Casts** (D3.14). `trunc`, `zext` and `sext` for integer to integer, widening by the
    source's signedness; `zext i1` for `bool` to integer; `ptrtoint` and `inttoptr` for pointer
    to and from `u64`; nothing at all for pointer to pointer and for casts that only drop
    mutability or ownership (D5.4, D17.4); `sitofp` and `uitofp` for integer to float; `fptrunc`
    and `fpext` for float to float; and the saturating intrinsics for float to integer, because
    plain `fptosi` is poison out of range while D3.14 requires truncation toward zero,
    saturation at the target's range, 0 for NaN, and no trap:

    ```llvm
      %t3 = call i32 @llvm.fptosi.sat.i32.f64(double %t2)
    ```

13. **Stack probing** (D10.8). The `"probe-stack"="inline-asm"` attribute on every fort
    definition (item 7) makes the backend establish a frame larger than a page one page at a
    time. It is in the IR rather than on the `--cc` line so that a module written by `-S`
    carries the guarantee by itself.

14. **Checks and failure blocks** (D19.6). Every runtime check computes one `i1` that is true on
    failure and branches with the failure label first:

    ```llvm
      br i1 %t4, label %L1, label %L0
    ```

    `assert` is the one exception: its operand is already the success condition, so it branches
    to the continuation first (D12.2). The continuation label is allocated before the failure
    label, and failure blocks are emitted after every normal block of the function, in ascending
    label order (D19.5). Each failure block holds exactly one call to the section 5.1 entry
    point, with the check's values, `ptr @.file.N` and the `i32` line and column of section 4's
    position rule, followed by `unreachable`; nothing else, because the callee aborts (D11.4).
    Every `_Noreturn` entry point of section 5.1 is declared `#2 = { cold noreturn nounwind }`:
    the `fort_rt_fail_*` family, `fort_rt_panic` and `fort_rt_assert_fail`, which the failure
    blocks call, and `fort_rt_exit`, which only `std::libc` reaches. `noreturn` is truthful,
    since each is `_Noreturn` in `runtime/fort_rt.h`, and `cold` lays the block out of line,
    which on an exit path is a layout hint and nothing more. No attribute is put on a failure
    call site.

15. **Integer checks** (D11.1, D11.3). In checked mode one intrinsic per operation, at the
    operand's width:

    ```llvm
      %t2 = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %t0, i32 %t1)
      %t3 = extractvalue { i32, i1 } %t2, 0
      %t4 = extractvalue { i32, i1 } %t2, 1
      br i1 %t4, label %L1, label %L0
    ```

    `llvm.sadd`, `llvm.ssub` and `llvm.smul` `.with.overflow.iN` for signed `+ - *`, the
    `llvm.uadd`, `llvm.usub` and `llvm.umul` family for unsigned; unary `-`, `++`, `--` and the
    compound assignments use the same intrinsics (negation is `llvm.ssub.with.overflow.iN(0,
    x)`). Release mode and the wrapping operators `+% -% *%` emit plain `add`, `sub` and `mul`.
    `nsw`, `nuw` and `exact` are never emitted (D16): release mode's wrapping is defined
    behavior, and in checked mode the check has already proved the absence of overflow. No
    float instruction carries a fast-math flag, since floats are IEEE 754 (D6.12, D16).

    A shift count is materialized at 64 bits (`sext` for a signed count type, `zext` for an
    unsigned one) so that a negative count is reported with its signed value, then
    `icmp uge i64 %cnt, <width>` branches to a `fort_rt_fail_shift(i64 %cnt, ptr @.str.T, ...)`
    block, where `@.str.T` is the shifted operand's type name; release mode replaces the check
    with `and i64 %cnt, <width - 1>` (D11.1). The count is then truncated to the operand's type
    and the shift is `shl`, `ashr` for a signed operand or `lshr` for an unsigned one (D6.2), so
    a shift never produces poison. Division and remainder, in both modes (D6.13): `icmp eq %d,
    0` branches to `fort_rt_fail_div_zero`; for a signed type the conjunction of
    `icmp eq %d, -1` and `icmp eq %n, <MIN>` branches to `fort_rt_fail_div_overflow`; then
    `sdiv`, `srem`, `udiv` or `urem`.

16. **Bounds checks** (D6.8, D6.9). The index is sign-extended (signed) or zero-extended
    (unsigned) to `i64`, then one `icmp uge i64 %idx, %len` branches to
    `fort_rt_fail_bounds(i64 %idx, i64 %len, ...)`, so a negative index fails the same compare.
    Element addressing is `getelementptr inbounds` (item 3):

    ```llvm
      %t2 = getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 %t0
      %t5 = getelementptr inbounds i32, ptr %t4, i64 %t3
    ```

    A span expression checks both bounds with one branch (`icmp ugt i64 %hi, %len`,
    `icmp ugt i64 %lo, %hi`, `or i1`) into `fort_rt_fail_span(i64 %lo, i64 %hi, i64 %len, ...)`. A
    fixed array's length is an `i64` literal. `--no-bounds-check` removes exactly these branches and
    keeps the `inbounds`, which is what makes it unsafe (D10.6).

17. **`new` and `del`** (D10.2, D10.3, D17.9).

    ```llvm
      %t0 = call ptr @fort_rt_new(i64 4, i64 1, ptr @.file.0, i32 7, i32 13)
    ```

    `new(T, n)` materializes `n` as `i64` first and, when its fort type is signed, branches on
    `icmp slt i64 %n, 0` to `fort_rt_fail_alloc_count(i64 %n, ...)`; it then calls
    `fort_rt_new(sizeof(T), %n, loc)` and writes the header field by field into the destination
    place (`getelementptr inbounds %fort.span, ptr %d, i32 0, i32 0` for `ptr`, `i32 0, i32 1`
    for `len`). `del(x)` loads the pointer (field 0 for a span or `string`), calls `fort_rt_del`
    and, on an lvalue operand, zeroes the place: `store ptr null` for a pointer, a 16-byte
    `llvm.memset` for a span or `string`. On an rvalue nothing is stored.

18. **Ownership** (D17). `own` is erased: same types, same ABI, same normalization, and neither
    the module nor the runtime carries ownership information. `move(lv)` copies the operand's
    value to the destination (`load` and `store` for a reference, `llvm.memcpy` for an owning
    aggregate) and then zeroes the operand (`store ptr null` or `llvm.memset`), in both build
    modes (D17.6). The overwrite check (D17.11), in checked mode only, runs after the
    right-hand side is evaluated and immediately before the store:

    ```llvm
      %t7 = getelementptr inbounds %fort.span, ptr %v.2, i32 0, i32 0
      %t8 = load ptr, ptr %t7, align 8
      %t9 = icmp ne ptr %t8, null
      br i1 %t9, label %L5, label %L4
    ```

    with `fort_rt_fail_overwrite(ptr @.file.N, i32 line, i32 col)` at the `=` token. Release
    mode emits the plain store; `--no-bounds-check` does not affect the check; declarations,
    `move`, `del` and assignments of owning aggregates never emit it. The check's own load
    cannot be optimized away, since it reads the location a later store writes.

19. **Builtins** (D12.2). The print family evaluates `fd` once (`1`, `2`, or the first argument)
    and then each argument left to right, one call per argument (D11.5): `i8 i16 i32 i64`
    sign-extended to `i64` to `fort_rt_print_i64`; `u8 u16 u32 u64` zero-extended to `i64` to
    `fort_rt_print_u64`; `f32` and `f64` to `_f32` and `_f64`; `bool` `zext`ed from `i1` to `i8`
    to `_bool`; `char` to `_char`; an enum as `(i32 %v, ptr @.enum.<path.name>, i64 <count>)` to
    `_enum`; a pointer, `void*` or function pointer to `_ptr`; a `string` as its `ptr` and `len`
    fields, or as `(ptr @.str.N, i64 <len>)` for a literal, to `_str`. `println` and its
    relatives end with `fort_rt_print_char(i32 %fd, i8 zeroext 10)`. `assert(cond)` branches to
    a block that calls `fort_rt_assert_fail(ptr @.str.N, ptr @.file.N, i32 line, i32 col)` and
    is followed by `unreachable`, in both build modes, where `@.str.N` is the verbatim source
    text of the argument; `panic(msg)` calls `fort_rt_panic(ptr, i64, ptr, i32, i32)` and is
    followed by `unreachable` (D11.4).

20. **`noreturn`** (D8.5, D19.7). A `noreturn` fort function is
    `define dso_local void @"m.f"(...) #1` with `#1 = { noreturn nounwind "frame-pointer"="all"
    "probe-stack"="inline-asm" }`, and the block that would fall off the end of its body ends
    with

    ```llvm
      call void @llvm.trap()
      unreachable
    ```

    as does every call site of such a function (D8.5 requires the trap in both places).
    `llvm.trap` is the trap instruction D8.5 asks for, and `unreachable` alone would not be one,
    since LLVM may let control fall through it. Reaching either raises SIGILL with no message
    (D11.4).

    `noreturn` is emitted on a fort definition, as `#1` above, and never on a declaration of a
    C function the program wrote with `extern fn`, whatever its fort return type; the runtime
    entry points are the exception, since their `_Noreturn` is the runtime's own guarantee
    (section 5.1, item 14). The reason is that the optimizer deletes the trap after a call to a
    function it is told never returns: for a fort definition that is harmless, because the trap
    at the end of the body survives, but an `extern` that returns anyway must still hit a trap
    at the call site (`memory-model.md` 6), which only an unadorned declaration preserves.

21. **Enum tables** (D3.9, D12.2).

    ```llvm
    @.enum.main.color = private unnamed_addr constant [3 x %fort.enum_member]
        [%fort.enum_member { i32 0, ptr @.str.3 },
         %fort.enum_member { i32 5, ptr @.str.4 },
         %fort.enum_member { i32 6, ptr @.str.5 }], align 8
    ```

    The table is one line in the module and is wrapped here only to fit the page, as the
    `llvm.memcpy` declaration of item 8 is. One entry per member in declaration order;
    `%fort.enum_member = type { i32, ptr }` has C's 16-byte layout with its 4 bytes of padding, so
    it matches `struct fort_rt_enum_member` (section 5.1). A table is emitted only for an enum
    some `print` of that type reaches.

22. **`fort_entry`** (D11.6, D8.6). Emitted in the entry module, it receives the argument span
    by hidden pointer, copies it into its own frame when `main` declares the parameter, and
    returns what `main` returns:

    ```llvm
    define dso_local i32 @fort_entry(ptr %args.in) #0 {
    entry:
      %args.0 = alloca %fort.span, align 8
      call void @llvm.memcpy.p0.p0.i64(ptr align 8 %args.0, ptr align 8 %args.in, i64 16, i1 false)
      %t0 = call i32 @"main.main"(ptr %args.0)
      ret i32 %t0
    }
    ```

    When `main` takes no parameter there is no `alloca` and no copy, only the call and the
    `ret` (the second example below).

23. **`-S` and `-c`** (D14.1). `-S` writes the module and stops, so the text above is exactly
    what a user reads; `-c` writes it into the temporary directory and runs `--cc -c` over it
    (section 2). The compiler never writes a `.s` file; `llc` over the `-S` output is how a
    human reads the machine code.

The attribute groups have fixed indices, and only the used ones are emitted, so gaps in the
numbering are normal (D19.5):

- `#0 = { nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }` on every fort definition
  (item 7), and `#1`, the same set plus `noreturn`, on a `noreturn` definition (item 20).
- `#2 = { cold noreturn nounwind }` on the `_Noreturn` entry points of section 5.1 (item 14).
- `#3 = { nobuiltin }` on every extern call site (item 8).
- `#4 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }` on the
  overflow intrinsics (item 15) and on `llvm.fptosi.sat` and `llvm.fptoui.sat` (item 12).
- `#5 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }` on `llvm.memcpy`
  and `#6 = { nocallback nofree nounwind willreturn memory(argmem: write) }` on `llvm.memset`.
- `#7 = { cold noreturn nounwind memory(inaccessiblemem: write) }` on `llvm.trap` (item 20).

`mustprogress` is deliberately absent everywhere, from `#4`, `#5` and `#6`, where clang would
print it, and from fort definitions: it licenses the optimizer to delete a loop with no side
effects, and a fort `while (true) { }` must keep running (D8.4 counts it as terminating, and
D14.2 emits no warning about what follows it).

### 6.1 A program without checks

```fort
fn i32 main() { println("hello, world!"); return 0; }
```

in `main.ft` compiles to `test/ir/hello.ll`:

```llvm
target triple = "x86_64-unknown-linux-gnu"

%fort.span = type { ptr, i64 }
%fort.enum_member = type { i32, ptr }

define dso_local i32 @"main.main"() #0 {
entry:
  call void @fort_rt_print_str(i32 1, ptr @.str.0, i64 13)
  call void @fort_rt_print_char(i32 1, i8 zeroext 10)
  ret i32 0
}

define dso_local i32 @fort_entry(ptr %args.in) #0 {
entry:
  %t0 = call i32 @"main.main"()
  ret i32 %t0
}

@.str.0 = private unnamed_addr constant [14 x i8] c"hello, world!\00", align 1

declare void @fort_rt_print_char(i32, i8 zeroext)
declare void @fort_rt_print_str(i32, ptr, i64)

attributes #0 = { nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }
```

### 6.2 A program with a check

```fort
fn i32 main() {
    println("before");
    i32[3] a = {};
    i64 mut i = 5;
    return a[i];
}
```

in `abort.ft`, whose module path is therefore `abort` (D9.1) and whose `main` is the symbol
`abort.main` (D9.7), with the `[` of `a[i]` at line 12, column 13, compiles to
`test/ir/abort.ll`:

```llvm
target triple = "x86_64-unknown-linux-gnu"

%fort.span = type { ptr, i64 }
%fort.enum_member = type { i32, ptr }

define dso_local i32 @"abort.main"() #0 {
entry:
  %a.0 = alloca [3 x i32], align 4
  %i.1 = alloca i64, align 8
  call void @fort_rt_print_str(i32 1, ptr @.str.0, i64 6)
  call void @fort_rt_print_char(i32 1, i8 zeroext 10)
  call void @llvm.memset.p0.i64(ptr align 4 %a.0, i8 0, i64 12, i1 false)
  store i64 5, ptr %i.1, align 8
  %t0 = load i64, ptr %i.1, align 8
  %t1 = icmp uge i64 %t0, 3
  br i1 %t1, label %L1, label %L0

L0:
  %t2 = getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 %t0
  %t3 = load i32, ptr %t2, align 4
  ret i32 %t3

L1:
  call void @fort_rt_fail_bounds(i64 %t0, i64 3, ptr @.file.0, i32 12, i32 13)
  unreachable
}

define dso_local i32 @fort_entry(ptr %args.in) #0 {
entry:
  %t0 = call i32 @"abort.main"()
  ret i32 %t0
}

@.file.0 = private unnamed_addr constant [9 x i8] c"abort.ft\00", align 1
@.str.0 = private unnamed_addr constant [7 x i8] c"before\00", align 1

declare void @fort_rt_fail_bounds(i64, i64, ptr, i32, i32) #2
declare void @fort_rt_print_char(i32, i8 zeroext)
declare void @fort_rt_print_str(i32, ptr, i64)

declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #6

attributes #0 = { nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }
attributes #2 = { cold noreturn nounwind }
attributes #6 = { nocallback nofree nounwind willreturn memory(argmem: write) }
```

The locals are entry-block allocas, the array is zeroed with `llvm.memset`, the bounds check of
item 16 branches to a failure block at the end of the function, and `%fort.span` and
`%fort.enum_member` are emitted although nothing uses them (item 2). The program prints
`before`, then `abort.ft:12:13: runtime error: index 5 out of range for length 3`, and dies with
SIGABRT (D11.4).

## 7. Testing

### 7.1 Layout (D14.4)

```sh
test/
  test.h                       C unit-test macros
  common.h                     TEST_UNUSED and shared helpers, provided by the implementation
  <component>_test.c           one C suite per compiler component (lexer_test.c, ...)
  lang/
    run_tests.py               runs every language test below
    run_tests_test.py          the harness's own unit tests
    xfail.txt                  tests the compiler cannot pass yet
    bootstrap-unsupported.txt  tests the C bootstrap must reject
    run/<area>/NNN_name.ft     compile, run, compare
    fail/<area>/NNN_name.ft    must not compile, with annotated errors
    run/modules/<name>/main.ft multi-file run test; the directory is the root
    fail/modules/<name>/main.ft multi-file fail test, same rule
    ffi/*.c                    C helpers for `link:` directives
    programs/*.ft              larger programs, treated as run tests
```

`<area>` is one of `lexical constants operators casts mutability declarations control switch
defer functions structs enums arrays spans strings pointers globals builtins errors modes modules
ffi stdlib ownership`: `errors` holds the `abort` tests of the runtime checks (the overwrite
check of D17.11 included, with its `--release` twin under `modes`), `modes` the `--release`
tests, `stdlib` the tests of the standard library once it exists, and `ownership` the run tests
of `own`, `move`, lending and `del` and the fail tests of every rule of D17. `NNN` is a three-digit
sequence number and `name` a short snake-case description. Paths in `link:` are relative to
`test/lang/`.

### 7.2 Directives (D14.5)

All directives are `//!` lines at the top of the file, except `error`, which annotates a line:

| Directive                       | Meaning                                                  |
|---------------------------------|----------------------------------------------------------|
| `//! run` or `//! fail`         | required, first line                                     |
| `//! flags: --release`          | extra compiler flags                                     |
| `//! args: a b c`               | program arguments, split on spaces                       |
| `//! link: ffi/helpers.c`       | C helper to link, relative to `test/lang`; repeatable    |
| `//! stdin:` then `//< ` lines  | standard input, one line per `//< ` line                 |
| `//! stdout:` then `//| ` lines | expected stdout, compared exactly, trailing spaces included |
| `//! exit: N`                   | expected exit status, default 0                          |
| `//! abort`                     | expect termination by SIGABRT                            |
| `//! stderr: <substring>`       | `<substring>` must appear in stderr; repeatable          |
| `//! error: <substring>`        | `fail` tests only, at the end of the offending line      |
| `//! error-any: <substring>`    | `fail` tests only, at the top                            |

The expected output is each `//| ` line's text after the marker followed by a newline; a bare
`//|` is an empty line. Output without a final newline cannot be expressed, so tests end their
output with `println`. Every `stderr:` substring must appear in stderr.

In a `fail` test every `//! error:` line must produce a diagnostic on that line containing the
substring, and no unannotated diagnostic may occur; `//! error-any:` requires some diagnostic to
contain the substring and is for errors without a useful line, such as circular imports.

### 7.3 What the harness does

`test/lang/run_tests.py [options] [filter...]` (Python 3, standard library only) runs the
compiler named by `--fort` (default `$FORT`, else `build/debug/fort`) with `--std-dir` from
`--std-dir` (default `$FORT_STD_DIR`, else `std` beside the compiler) and `--cc` from `--cc`
(default `clang`, which must be a clang as `--cc` is, D14.1). `--target` (default
`x86_64-linux-gnu`) is the triple the harness passes to `--cc` as `--target=<triple>` when it
links a test's C helpers itself. `--verify-ir` runs `fort -S -o prog.ll <test>` for every test
whose compilation succeeds and verifies the module with `<opt> -passes=verify -disable-output`
(D19.1); `--opt` names that program (default `opt-18`). A module the verifier rejects is a FAIL;
an `opt` that cannot be launched, times out or dies by a signal is an ERROR, like a compiler exit
2 (D14.1). `--runner` names a command that runs the programs when binfmt does not, `-j` the
number of parallel tests, `--timeout` the seconds per step, `-v` prints the commands and outputs
of failures and `--keep` keeps the temporary directories. The
compiler runs with `test/lang` as its working directory (D14.4). For each test, in a fresh
temporary directory that is also `TMPDIR`, with `LC_ALL=C`, `QEMU_LD_PREFIX` set unless
inherited and core dumps disabled:

| Directive    | Harness action                                                              |
|--------------|-----------------------------------------------------------------------------|
| `run`        | `fort <flags> -o prog <test>` must exit 0; run `prog`; compare its output    |
| `fail`       | `fort <flags> -o prog <test>` must exit 1 with only annotated errors        |
| `flags:`     | appended to the `fort` command line                                         |
| `args:`      | appended to the program's command line                                      |
| `link:`      | `fort -c`, then `cc -o prog prog.o <helpers> <std-dir>/fort_rt.o`           |
| `stdin:`     | the `//< ` lines, each with a newline, are the program's stdin; else empty   |
| `stdout:`    | the program's stdout must equal the `//| ` lines; with no directive, empty   |
| `exit:`      | the program's exit status must equal `N`                                    |
| `abort`      | the program must die with SIGABRT (status 134 from the shell)               |
| `stderr:`    | each substring must occur in the program's (`run`) or compiler's (`fail`) stderr |
| `error:`     | an `error:` line with that file and line must contain the substring         |
| `error-any:` | some `error:` line must contain the substring                               |

`<test>` is the test file, or `main.ft` in a multi-file test; `-o` names a file in the temporary
directory even for a `fail` test, so a compiler that wrongly succeeds never writes `a.out` into
`test/lang`. Before the `stderr:` substrings of a `run` test are looked for, the lines qemu-user
adds when a signal kills the program (`qemu: uncaught target signal 6 (Abort) - core dumped`)
are dropped, since native execution prints nothing there. For `error:` the harness also fails
the test when the compiler reports an `error:` for a line that carries no annotation; a
diagnostic matched by an `error-any:` counts as annotated, and further diagnostics on an
annotated line are accepted. In multi-file tests, directives are read from `main.ft`,
`//! error:` annotations from every `.ft` file in the directory (D14.4), and no `-I` is passed
because the directory is the root (D9.2). A compiler exit status other than 0 or 1 (2 is a
usage, toolchain or internal error, D14.1), a compiler crash, a compiler timeout, a failure of
the harness's own `link:` step and a program that cannot be started are `ERROR`, not a verdict
about the test; a program that times out is a `FAIL`.

Two expectation files beside the harness list path prefixes of tests (relative to `test/lang`,
`#` comments allowed). `xfail.txt` names the tests the compiler cannot pass yet: a listed test
that fails or errors is `XFAIL`, a listed test that passes is `XPASS` and fails the run, so the
list shrinks in the commit that makes tests pass. `bootstrap-unsupported.txt` names the tests
that use features the C bootstrap deliberately lacks (floats, multi-dimensional arrays,
`do`-`while` and `?:`; function pointers are in its subset, D3.10); each is judged as a `fail`
test whose only expectation is a diagnostic containing `not supported by the bootstrap
compiler`, whatever its own kind, and `--no-unsupported` (for the self-hosted compiler) judges
them normally.
`--xfail` and `--unsupported` name other lists; `--no-xfail` ignores the first.

The harness prints one `PASS`, `FAIL`, `XFAIL`, `XPASS` or `ERROR` line per test with the
reason where there is one, then a summary, and exits with 1 if any test is `FAIL`, `XPASS` or
`ERROR`; each `filter` selects the tests whose path contains it. `--list` prints the selected
tests and their count. `--lint` validates the corpus without a compiler and fails on: a first
line other than `//! run` or `//! fail` or one that does not match the directory; an unknown,
malformed, duplicated or empty directive; `exit` together with `abort`; a run-only directive in
a `fail` test or `error`/`error-any` in a `run` test; a `link:` file that does not exist; a
`//<` or `//|` not followed by a space or outside its block; a directive after the header or in
a sibling module; a `fail` test with neither `error:` nor `error-any:`; an unknown area, a
badly named test, a stray file, a directory test outside `modules`, and a gap or duplicate in
the `NNN` numbering of an area; and an expectation-list entry that matches no test. A run
performs the same checks first and stops when they fail, and fails when the filters select no
test.

### 7.4 Examples

The examples are abbreviated; the seed corpus holds full versions in the areas named. A run
test under `test/lang/run/arrays/`:

```fort
//! run
//! stdout:
//| 3 2
fn i32 main() {
    i32[3] a = {1, 2, 3};
    println(a[2], " ", a[1]);
    return 0;
}
```

A fail test, `test/lang/fail/mutability/001_assign_immutable.ft`:

```fort
//! fail
fn i32 main() {
    i32 x = 1;
    x = 2;                       //! error: immutable
    return x;
}
```

An abort test with `stderr:` under `test/lang/run/errors/`; `before` is flushed by the failure
path (D11.4):

```fort
//! run
//! abort
//! stderr: runtime error: index 5 out of range for length 3
//! stdout:
//| before
fn i32 main() {
    i32 mut@ own xs = new(i32, 3);
    defer del(xs);
    println("before");
    xs[5] = 1;
    return 0;
}
```

An ownership fail test under `test/lang/fail/ownership/`, one annotated line per rule (D17.5,
D17.8, D17.9):

```fort
//! fail
struct node { i32 v; }

fn i32 main() {
    node mut* own a = new(node);
    node mut* own b = a;         //! error: move
    node mut* c = new(node);     //! error: would leak
    node* view = a;
    del(view);                   //! error: own
    del(a);
    return 0;
}
```

A multi-file test, `test/lang/run/modules/<name>/main.ft` with `util.ft` beside it:

```fort
//! run
//! stdout:
//| 7
import util;
import util::twice;

fn i32 main() {
    println(util.inc(twice(3)));
    return 0;
}
```

```fort
// util.ft
fn i32 twice(i32 x) {
    return x * 2;
}

fn i32 inc(i32 x) {
    return x + 1;
}
```

A release-mode test under `test/lang/run/modes/`, whose checked-mode twin uses `//! abort` and
`//! stderr: runtime error: integer overflow` instead:

```fort
//! run
//! flags: --release
//! stdout:
//| -2147483648
fn i32 main() {
    i32 mut x = 2147483647;
    x += 1;
    println(x);
    return 0;
}
```

An FFI test under `test/lang/run/ffi/`, with its helper in `test/lang/ffi/helpers.c`:

```fort
//! run
//! link: ffi/helpers.c
//! stdout:
//| 12
extern fn i32 helper_add(i32 a, i32 b);

fn i32 main() {
    println(helper_add(5, 7));
    return 0;
}
```

```c
int32_t helper_add(int32_t a, int32_t b) { return a + b; }
```

`args:` and `exit:` are exercised by a `main(string@ args)` that returns `cast(args.len, i32)`
under `//! args: one two` and `//! exit: 3`.

### 7.5 Compiler unit tests in C

Each `test/<component>_test.c` is one suite built against the compiler's sources and `test.h`:
tests are defined with `TEST(name, body)`, registered with `TEST_RUN`, and the suite exits through
`TEST_EXIT` with status 0 (ok), 1 (a failed assertion) or 2 (a test error). The first command-line
argument, when present, is a name prefix selecting tests. `TEST_ASSERT_*` macros log file, line,
expression, actual and expected values and return `TEST_RESULT_FAIL`. A body is one macro
argument, so a comma outside parentheses inside it must be parenthesized.

```c
#include "test.h"
#include "lexer.h"

TEST(lexes_hex_literal, {
    lexer_t lx;
    lexer_init(&lx, "0x7F");
    token_t t = lexer_next(&lx);
    TEST_ASSERT_EQ_INT32(t.kind, TOK_INT);
    TEST_ASSERT_EQ_INT64(t.value, 127);
})

int main(int argc, char** argv) {
    TEST_INIT("lexer", argc, argv);
    TEST_RUN(lexes_hex_literal);
    TEST_EXIT();
}
```

The names `lexer_t`, `lexer_init`, `lexer_next`, `token_t` and `TOK_INT` are chosen in the
implementation phase (section 8). Unit tests cover what language tests cannot observe directly:
the token stream, the parser's speculative rewinds, constant folding at the edges of the
`[-2^63, 2^64 - 1]` range (D4.4), the mutability-drop check (D5.4) and option parsing.

### 7.6 Coverage target (D14.6)

The corpus aims at about three lines of test for each line of compiler source: `wc -l` over
`test/*.c`, `test/lang/**/*.ft` and `test/lang/ffi/*.c` against `wc -l` over `src/bootstrap/*.c`,
`src/bootstrap/*.h` and `src/fort/*.ft`, the runtime and standard library excluded from both
sides. The seed tests of the design phase establish the format with one example per area; the
full corpus is sized as follows,
in files:

| Area                    | run | fail |
|-------------------------|-----|------|
| lexer and literals      | 30  | 30   |
| operators and constants | 50  | 35   |
| casts                   | 25  | 20   |
| mutability              | 20  | 40   |
| control flow            | 45  | 25   |
| functions               | 30  | 25   |
| structs                 | 30  | 20   |
| enums                   | 20  | 15   |
| arrays                  | 30  | 15   |
| spans and `new`/`del`   | 35  | 20   |
| strings                 | 30  | 10   |
| pointers                | 25  | 20   |
| defer                   | 20  | 10   |
| modules                 | 30  | 30   |
| FFI                     | 20  | 10   |
| globals                 | 15  | 10   |
| builtins                | 30  | 10   |
| stdlib                  | 45  | 5    |
| ownership               | 15  | 20   |
| programs                | 20  | 0    |
| total                   | 565 | 370  |

Every operator, keyword and builtin appears in at least one run test and, where it can be
misused, one fail test (decisions.md checklist). Every runtime check has an `abort` test in
checked mode and, for the checks that release mode removes, a release-mode twin.

## 8. Compiler architecture sketch

This section is not normative. It records the intended shape so that the other sections are
implementable; the design is to be planned in the implementation phase.

- **Language and dependencies.** C11, POSIX, no external libraries; one binary `fort`. The
  repository holds `src/` (compiler), `runtime/fort_rt.c` (runtime), `std/*.ft` (standard library),
  `test/` (section 7) and a build script producing `build/fort` and `build/std/` with the library
  sources and `fort_rt.o`.
- **Driver.** Parses options (section 1), owns the module table keyed by real path, runs the
  passes below, invokes `--cc` over the emitted module (D14.3) and maps failures to exit
  statuses.
- **Lexer.** A complete token array per file (kind, position, literal value) applying D2; the
  array makes each speculative parse of `grammar.md` section 7 a saved and restored index.
- **Parser.** Recursive descent over the token array, one AST per module, with the nesting
  limit of D2.11; imports go back to the driver, which loads modules until the closure is
  complete and acyclic.
- **Checker.** Two-phase top-level resolution per D7.10: phase one enters every module-level
  name; phase two resolves types, signatures, constant values and struct sizes lazily with cycle
  detection, so declaration order never matters; then each body is checked against D3 to D8 and
  D17 and the AST is annotated with types, constant values, lvalue mutability, resolved symbols
  and, on each assignment, whether the target is an `own` lvalue that needs the overwrite check.
- **Codegen.** One forward pass over the annotated AST appending text to a single LLVM IR
  module (D19.1, section 6), with no intermediate representation of its own, no libLLVM and no
  register allocation: locals are `alloca`s in the entry block, intermediates are SSA
  temporaries, and deferred statements are expanded statically at each exit (D7.8).
- **Memory.** Arenas per compilation; nothing is freed before exit.

## 9. Editor support

An editor asks the compiler two questions about a saved file: what is wrong with it, and what
does this name mean. Section 4.1 answers the first; the identifier index below answers the second.
Both are one batch run of `fort`, and the check mode is the compiler's whole editor interface: it
never grows a server (D20).

### 9.1 The identifier index (D20.3)

`fort --index entry.ft` runs the check mode and fills the document's `"symbols"` array with one
record per identifier occurrence the checker resolved, in every module of the closure that was
checked. `--index` implies `--check` and `--json`, so the option is the whole command line an
editor needs; without it `"symbols"` is the empty array.

```json
{"file": "main.ft", "line": 7, "col": 16, "end_line": 7, "end_col": 19,
 "name": "add", "kind": "fn", "type": "fn i32(i32, i32)", "is_decl": false,
 "decl": {"file": "mathx.ft", "line": 12, "col": 8, "end_line": 12, "end_col": 11}}
```

- The record's own range is the range of that one name token, never the construct's first token
  (D20.4): the range of `add` in `mathx.add(1, 2)` covers `add` alone, so an editor underlines the
  name the reader pointed at. Positions are the 1-based byte columns of section 4, the end
  exclusive; converting them to UTF-16 code units is the client's job, as it is for a diagnostic.
- `"name"` is the identifier as it is spelled at that occurrence, so an `as` alias reads as the
  alias and the declaration it binds reads as its own name (D9.3).
- `"kind"` is what the name denotes, spelled as a diagnostic spells it: `module`, `fn`,
  `extern fn`, `struct`, `enum`, `enum member`, `field`, `constant`, `global`, `local`, `parameter`
  or `builtin` (D7.9, D7.10, D3.9, D12.2).
- `"type"` is the declaration's type as a declaration spells it, the `mut` of level 0 included
  (D5.2, D5.3): `i32`, `i32 mut* own`, `fn i32(i32, i32)`. It is the empty string for a name that
  denotes no value type, which is a module, a struct name, an enum name and a builtin, and `null`
  when the declaration failed to check, which a client renders as unknown: the type it has is the
  poison of section 4 and says nothing a reader wants (D20.3).
- `"is_decl"` is true on the occurrence that declares the name in this file and false on every use
  of it, and `"decl"` is the range of the declaring name token: for a declaration, its own range.
  An `as` alias declares its name in the importing module (D9.3), so it is the one record with
  `"is_decl"` true whose `"decl"` lies elsewhere: going to the definition of `double` in
  `import util::twice as double;` lands on `twice`, while the alias is still the anchor a rename of
  `double` in this file starts from. An import without an alias introduces the name its declaration
  already has, so its occurrence is a use.
- `"decl"` is `null` for a builtin, which no source declares (D12.2), and the empty range at 1:1 of
  the module's file for a module, which a file declares and which has no name token (D9.1), so
  jumping to the definition of `mathx` in `mathx.add(1, 2)` opens `mathx.ft` at the top rather than
  reading as a builtin's nothing to jump to. No record is therefore a module's declaration, and a
  module has no rename anchor.
- Records are ordered by file, an imported module before its importers (D9.10), and within a file
  by the start of the occurrence, so a client may bisect the records of a file by position.
- A name the checker could not resolve carries no record, and a file that did not parse
  contributes none, so a file with errors still indexes everything that resolved. A construct with
  no name token of its own carries none either, and neither does a segment of an import path
  before its last: those name search directories, not modules (D9.2, D9.3). The pseudo-fields
  `.len` and `.ptr` carry none: they are a property of the type and not a declaration of any
  module (D3.4, D3.5, D3.7).
- Every record repeats the type and the declaration range of the name it resolves, so a client
  answers hover and go-to-definition from the record under the cursor alone.

### 9.2 What an editor does with it

The index is a batch answer about the file as it was saved, which is what an editor built on
`--index` can promise: it publishes the diagnostics of `"files"`, clearing the files that no longer
have any, and serves hover and definition from the records of the last run whose closure contained
the file. `"files"` is the set a client may clear, not the set it publishes: every entry of
`"diagnostics"` is published whether or not its file is listed, since an error about a file
the compiler never read names a file that `"files"` cannot hold (section 4.1, D20.2). Between
two saves the answers are stale, and a client says so rather than guessing. The VS Code
extension in `editors/vscode` is the client this repository ships; `editors/README.md` is
its install guide and its list of limitations.

## 10. Not in v1

Deferred by D15 and the design reviews: debugger support (no DWARF, no `!dbg` metadata; the
frame pointer of section 6 item 7 and the symbol names are what a debugger gets), an optimizer
of the compiler's own and `-O` options on `fort`'s command line (`--cc` optimizes the module at
`-O1`, or `-O2` under `--release`, D14.3), building the module through the LLVM C API in process
(D15), warnings, separate compilation and incremental builds, a package manager, documentation
generation, cross-compilation and any target other than x86-64 Linux, `--help` text beyond the
usage line, and conditional compilation. The idioms that replace the deferred language features
are listed with each item in D15.
